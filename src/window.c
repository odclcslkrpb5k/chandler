#include "window.h"
#include "aliases.h"
#include "catalog.h"
#include "lifetime.h"
#include "packages.h"
#include "row.h"
#include "text.h"

/* Typing re-searches once this long has passed without another key (plan §3.6). */
#define SEARCH_DELAY_MS 200
/* At most this many rows: each is widgets, an icon load and package names to resolve, all
 * on the main loop (a one-letter search would otherwise build hundreds). */
#define ROW_LIMIT 100
/* A text search needs this many characters. */
#define TEXT_MIN_CHARS 2

typedef enum
{
  CATALOG_LOADING,
  CATALOG_FAILED,
  CATALOG_EMPTY,
  CATALOG_READY,
} CatalogState;

typedef enum
{
  PACKAGES_PENDING,
  PACKAGES_READY,
  PACKAGES_ABSENT, /* NO_DAEMON */
} PackagesState;

typedef enum
{
  QUERY_EMPTY,
  QUERY_TYPE,
  QUERY_TEXT,
} QueryKind;

struct _ChandlerWindow
{
  GtkApplicationWindow parent;

  GtkEntry *entry;
  GtkLabel *heading;
  GtkInfoBar *info;
  GtkLabel *info_label;
  GtkStack *stack; /* "status" or "results" */
  GtkLabel *status;
  GtkListBox *list;

  ChandlerAliases *aliases; /* nullptr until loaded */
  ChandlerCatalog *catalog;
  CatalogState catalog_state;
  ChandlerPackages *packages;
  PackagesState packages_state;

  QueryKind kind;
  char *query;       /* the entry's text when the search ran */
  char *unsupported; /* a launch asked for something Chandler doesn't do */
  ChandlerOutcome problem; /* the last resolve's failure, or OK */
  GObject *ticket; /* owns the running resolve: replacing it cancels that resolve */
  guint row_limit;
  bool truncated; /* more apps matched than row_limit */
  guint debounce;
};

G_DEFINE_FINAL_TYPE (ChandlerWindow, chandler_window, GTK_TYPE_APPLICATION_WINDOW)

static void
chandler_window_dispose (GObject *object)
{
  ChandlerWindow *self = CHANDLER_WINDOW (object);
  g_clear_object (&self->ticket);
  g_clear_object (&self->catalog);
  g_clear_object (&self->packages);
  G_OBJECT_CLASS (chandler_window_parent_class)->dispose (object);
}

static void
chandler_window_finalize (GObject *object)
{
  ChandlerWindow *self = CHANDLER_WINDOW (object);
  g_clear_pointer (&self->aliases, chandler_aliases_free);
  g_free (self->query);
  g_free (self->unsupported);
  G_OBJECT_CLASS (chandler_window_parent_class)->finalize (object);
}

static void
chandler_window_class_init (ChandlerWindowClass *klass)
{
  G_OBJECT_CLASS (klass)->dispose = chandler_window_dispose;
  G_OBJECT_CLASS (klass)->finalize = chandler_window_finalize;
}

/* --- What is shown --- */

static void
show_info (ChandlerWindow *self)
{
  const char *lines[] = {
    self->unsupported != nullptr
        ? "Chandler only finds apps by file type or name; use apt for this."
        : nullptr,
    self->packages_state == PACKAGES_ABSENT ? chandler_outcome_text (CHANDLER_OUTCOME_NO_DAEMON)
                                            : nullptr,
    chandler_outcome_text (self->problem),
    self->truncated ? "Showing the first matches only: make the search longer to narrow it."
                    : nullptr,
  };
  g_autoptr (GString) text = g_string_new (nullptr);
  for (size_t i = 0; i < G_N_ELEMENTS (lines); i++)
    if (lines[i] != nullptr)
      g_string_append_printf (text, "%s%s", text->len > 0 ? "\n" : "", lines[i]);
  gtk_label_set_text (self->info_label, text->str);
  gtk_widget_set_visible (GTK_WIDGET (self->info), text->len > 0);
}

static void
show_status (ChandlerWindow *self, const char *text)
{
  gtk_label_set_text (self->status, text);
  gtk_stack_set_visible_child_name (self->stack, "status");
}

static void
show_heading (ChandlerWindow *self)
{
  g_autofree char *text = nullptr;
  switch (self->kind)
    {
    case QUERY_EMPTY: text = g_strdup ("Find an app by file type or name"); break;
    case QUERY_TYPE:
      {
        g_autofree char *description = g_content_type_get_description (self->query);
        g_autofree char *clean = chandler_text_for_terminal (description);
        text = g_strdup_printf ("Apps that open %s (%s)", clean, self->query);
        break;
      }
    case QUERY_TEXT:
      {
        g_autofree char *clean = chandler_text_for_terminal (self->query);
        text = g_strdup_printf ("Apps matching \"%s\"", clean);
        break;
      }
    }
  gtk_label_set_text (self->heading, text);
}

static GPtrArray *
rows (ChandlerWindow *self)
{
  GPtrArray *out = g_ptr_array_new ();
  g_autoptr (GList) children = gtk_container_get_children (GTK_CONTAINER (self->list));
  for (GList *l = children; l != nullptr; l = l->next)
    g_ptr_array_add (out, l->data);
  return out;
}

static void
set_all_rows (ChandlerWindow *self, ChandlerRowState state)
{
  g_autoptr (GPtrArray) all = rows (self);
  for (guint i = 0; i < all->len; i++)
    chandler_row_set_state (g_ptr_array_index (all, i), state);
}

/* --- Package states --- */

static void
on_resolved (const ChandlerResolved *resolved, ChandlerOutcome outcome,
             GObject *owner G_GNUC_UNUSED, void *data)
{
  ChandlerWindow *self = data; /* alive: it owns the ticket that owns this call */
  if (resolved == nullptr)
    {
      self->problem = outcome;
      set_all_rows (self, CHANDLER_ROW_UNKNOWN);
      show_info (self);
      return;
    }
  g_autoptr (GPtrArray) all = rows (self);
  for (guint i = 0; i < all->len; i++)
    {
      ChandlerRow *row = g_ptr_array_index (all, i);
      chandler_row_set_state (row, chandler_row_state_from_pkg (chandler_resolved_app_state (
                                       resolved, chandler_row_get_pkgnames (row))));
    }
}

static void
resolve_rows (ChandlerWindow *self)
{
  g_clear_object (&self->ticket);
  switch (self->packages_state)
    {
    case PACKAGES_PENDING: set_all_rows (self, CHANDLER_ROW_CHECKING); return;
    case PACKAGES_ABSENT: set_all_rows (self, CHANDLER_ROW_UNKNOWN); return;
    case PACKAGES_READY: break;
    }
  g_autoptr (GPtrArray) all = rows (self);
  if (all->len == 0)
    return;
  g_autoptr (GStrvBuilder) builder = g_strv_builder_new ();
  for (guint i = 0; i < all->len; i++)
    {
      ChandlerRow *row = g_ptr_array_index (all, i);
      chandler_row_set_state (row, CHANDLER_ROW_CHECKING);
      for (const char *const *p = chandler_row_get_pkgnames (row); *p != nullptr; p++)
        g_strv_builder_add (builder, *p);
    }
  g_auto (GStrv) names = g_strv_builder_end (builder);
  self->ticket = g_object_new (G_TYPE_OBJECT, nullptr);
  chandler_packages_resolve (self->packages, self->ticket, (const char *const *) names, on_resolved,
                             self);
}

/* --- Searching --- */

static GPtrArray *
find_apps (ChandlerWindow *self)
{
  switch (self->kind)
    {
    case QUERY_EMPTY: return g_ptr_array_new ();
    case QUERY_TYPE:
      {
        ChandlerMediaType type;
        if (!chandler_mediatype_parse (self->query, &type))
          gbk_fatal ("a TYPE query that isn't a media type");
        g_auto (GStrv) types = chandler_aliases_expand (self->aliases, &type);
        return chandler_catalog_for_types (self->catalog, (const char *const *) types);
      }
    case QUERY_TEXT: return chandler_catalog_search (self->catalog, self->query);
    }
  gbk_fatal ("unknown query kind %d", (int) self->kind);
}

static const char *
no_results (QueryKind kind)
{
  switch (kind)
    {
    case QUERY_EMPTY: return "Type a file type, such as image/png, or part of an app's name.";
    case QUERY_TYPE: return "No app in your software sources opens this type.";
    case QUERY_TEXT: return "No app in your software sources matches.";
    }
  gbk_fatal ("unknown query kind %d", (int) kind);
}

/* Shows the current query's results, or why there are none. */
static void
refresh (ChandlerWindow *self)
{
  g_clear_object (&self->ticket);
  self->problem = CHANDLER_OUTCOME_OK;
  self->truncated = false;
  g_autoptr (GPtrArray) old = rows (self);
  for (guint i = 0; i < old->len; i++)
    gtk_widget_destroy (g_ptr_array_index (old, i));
  show_heading (self);
  show_info (self);
  switch (self->catalog_state)
    {
    case CATALOG_LOADING: show_status (self, "Loading the software catalog…"); return;
    case CATALOG_FAILED: show_status (self, "Couldn't read the software catalog."); return;
    case CATALOG_EMPTY: show_status (self, "No app metadata: run \"sudo apt update\"."); return;
    case CATALOG_READY: break;
    }
  if (self->aliases == nullptr)
    {
      show_status (self, "Loading the software catalog…");
      return;
    }
  if (self->kind == QUERY_TEXT && g_utf8_strlen (self->query, -1) < TEXT_MIN_CHARS)
    {
      show_status (self, "Type at least two characters of an app's name.");
      return;
    }
  g_autoptr (GPtrArray) apps = find_apps (self);
  if (apps->len == 0)
    {
      show_status (self, no_results (self->kind));
      return;
    }
  if (apps->len > self->row_limit)
    {
      g_ptr_array_remove_range (apps, self->row_limit, apps->len - self->row_limit);
      self->truncated = true;
      show_info (self);
    }
  for (guint i = 0; i < apps->len; i++)
    gtk_container_add (GTK_CONTAINER (self->list), chandler_row_new (g_ptr_array_index (apps, i)));
  gtk_stack_set_visible_child_name (self->stack, "results");
  resolve_rows (self);
}

static void
publish_query (ChandlerWindow *self)
{
  /* The application's disabled "query" action carries the search as its state, so a test
   * can see that a second launch reached this window (tests/check-unique.py). */
  GtkApplication *app = gtk_window_get_application (GTK_WINDOW (self));
  GAction *action
      = app != nullptr ? g_action_map_lookup_action (G_ACTION_MAP (app), "query") : nullptr;
  if (action != nullptr)
    g_simple_action_set_state (G_SIMPLE_ACTION (action), g_variant_new_string (self->query));
}

/* Runs a search for `text` as the entry has it. */
static void
search (ChandlerWindow *self, const char *text)
{
  gbk_source_cancel (&self->debounce);
  g_autofree char *trimmed = g_strstrip (g_strdup (text));
  ChandlerMediaType type;
  if (trimmed[0] == '\0')
    self->kind = QUERY_EMPTY;
  else if (chandler_mediatype_parse (trimmed, &type))
    self->kind = QUERY_TYPE;
  else
    self->kind = QUERY_TEXT;
  g_free (self->query);
  self->query = self->kind == QUERY_TYPE ? g_strdup (type.value) : g_steal_pointer (&trimmed);
  publish_query (self);
  refresh (self);
}

/* The user searched: a launch's note about a flag Chandler doesn't handle no longer
 * applies. */
static void
typed (ChandlerWindow *self, const char *text)
{
  g_clear_pointer (&self->unsupported, g_free);
  search (self, text);
}

static bool
on_debounce (void *data)
{
  ChandlerWindow *self = data;
  self->debounce = 0;
  typed (self, gtk_entry_get_text (self->entry));
  return false;
}

static void
on_entry_changed (GtkEntry *entry G_GNUC_UNUSED, void *data)
{
  ChandlerWindow *self = data;
  gbk_source_cancel (&self->debounce);
  self->debounce = gbk_timeout_add (G_OBJECT (self), SEARCH_DELAY_MS, on_debounce, self);
}

static void
on_entry_activate (GtkEntry *entry, void *data)
{
  ChandlerWindow *self = data;
  g_autofree char *trimmed = g_strstrip (g_strdup (gtk_entry_get_text (entry)));
  /* Enter on the search already shown keeps it (and its rows' states). */
  if (self->debounce == 0 && g_str_equal (trimmed, self->query))
    return;
  typed (self, trimmed);
}

void
chandler_window_show_request (ChandlerWindow *self, const ChandlerRequest *request)
{
  const char *text = "";
  switch (request->kind)
    {
    case CHANDLER_REQUEST_EMPTY: break;
    case CHANDLER_REQUEST_TYPE: text = request->type.value; break;
    case CHANDLER_REQUEST_TEXT: text = request->text; break;
    case CHANDLER_REQUEST_LIST:
    case CHANDLER_REQUEST_VERSION:
    case CHANDLER_REQUEST_USAGE:
      gbk_fatal ("the window can't show request kind %d", (int) request->kind);
    }
  g_free (self->unsupported);
  self->unsupported = g_strdup (request->unsupported);
  /* Another process's text: shown without controls or reordering characters. */
  g_autofree char *clean = chandler_text_for_terminal (text);
  gtk_entry_set_text (self->entry, clean);
  /* search() also cancels the typing delay that setting the entry has just started. */
  search (self, clean);
}

const char *
chandler_window_get_query (const ChandlerWindow *self)
{
  return self->query;
}

/* --- Loading --- */

static void
on_packages (ChandlerPackages *packages, ChandlerOutcome outcome, GObject *owner,
             void *data G_GNUC_UNUSED)
{
  ChandlerWindow *self = CHANDLER_WINDOW (owner);
  if (outcome == CHANDLER_OUTCOME_OK)
    {
      self->packages = g_object_ref (packages);
      self->packages_state = PACKAGES_READY;
    }
  else
    self->packages_state = PACKAGES_ABSENT;
  show_info (self);
  resolve_rows (self);
}

static void
on_catalog (ChandlerCatalog *catalog, const GError *error, GObject *owner, void *data G_GNUC_UNUSED)
{
  ChandlerWindow *self = CHANDLER_WINDOW (owner);
  if (catalog == nullptr)
    {
      g_debug ("catalog: %s %d", g_quark_to_string (error->domain), error->code);
      self->catalog_state = CATALOG_FAILED;
    }
  else
    {
      self->catalog = g_object_ref (catalog);
      self->catalog_state = chandler_catalog_is_empty (catalog) ? CATALOG_EMPTY : CATALOG_READY;
    }
  refresh (self);
}

typedef struct
{
  char *catalog_dir; /* nullptr: the system's */
} Start;

static void
start_free (Start *start)
{
  g_free (start->catalog_dir);
  g_free (start);
}

static void
on_aliases (ChandlerAliases *aliases, GObject *owner, void *data)
{
  ChandlerWindow *self = CHANDLER_WINDOW (owner);
  const Start *start = data;
  self->aliases = aliases;
  if (start->catalog_dir != nullptr)
    chandler_catalog_open_at (owner, start->catalog_dir, on_catalog, nullptr);
  else
    chandler_catalog_open (owner, on_catalog, nullptr);
}

/* --- Keys --- */

static gboolean
on_key_press (GtkWidget *widget, GdkEventKey *event, void *data G_GNUC_UNUSED)
{
  if (event->keyval != GDK_KEY_Escape
      || (event->state & gtk_accelerator_get_default_mod_mask ()) != 0)
    return false;
  gtk_window_close (GTK_WINDOW (widget));
  return true;
}

static void
on_row_activated (GtkListBox *list G_GNUC_UNUSED, GtkListBoxRow *row, void *data G_GNUC_UNUSED)
{
  chandler_row_focus_action (CHANDLER_ROW (row));
}

/* --- Building --- */

static GtkLabel *
wrapping (const char *text)
{
  GtkLabel *label = GTK_LABEL (gtk_label_new (text));
  gtk_label_set_line_wrap (label, true);
  gtk_label_set_line_wrap_mode (label, PANGO_WRAP_WORD_CHAR);
  gtk_label_set_xalign (label, 0);
  return label;
}

static void
chandler_window_init (ChandlerWindow *self)
{
  self->problem = CHANDLER_OUTCOME_OK;
  self->row_limit = ROW_LIMIT;
  self->query = g_strdup ("");
  gtk_window_set_title (GTK_WINDOW (self), "Chandler");
  gtk_window_set_default_size (GTK_WINDOW (self), 640, 560);
  gtk_window_set_icon_name (GTK_WINDOW (self), "system-software-install");

  GtkBox *box = GTK_BOX (gtk_box_new (GTK_ORIENTATION_VERTICAL, 6));
  g_object_set (box, "margin", 12, nullptr);

  /* A plain entry: GtkSearchEntry takes Esc for itself, and Esc closes the window. */
  self->entry = GTK_ENTRY (gtk_entry_new ());
  gtk_entry_set_icon_from_icon_name (self->entry, GTK_ENTRY_ICON_PRIMARY, "edit-find-symbolic");
  gtk_entry_set_placeholder_text (self->entry, "File type or app name");
  gtk_box_pack_start (box, GTK_WIDGET (self->entry), false, false, 0);

  self->heading = wrapping ("");
  PangoAttrList *large = pango_attr_list_new ();
  pango_attr_list_insert (large, pango_attr_scale_new (PANGO_SCALE_LARGE));
  pango_attr_list_insert (large, pango_attr_weight_new (PANGO_WEIGHT_BOLD));
  gtk_label_set_attributes (self->heading, large);
  pango_attr_list_unref (large);
  gtk_box_pack_start (box, GTK_WIDGET (self->heading), false, false, 0);

  self->info = GTK_INFO_BAR (gtk_info_bar_new ());
  gtk_info_bar_set_message_type (self->info, GTK_MESSAGE_WARNING);
  self->info_label = wrapping ("");
  gtk_container_add (GTK_CONTAINER (gtk_info_bar_get_content_area (self->info)),
                     GTK_WIDGET (self->info_label));
  gtk_box_pack_start (box, GTK_WIDGET (self->info), false, false, 0);

  self->stack = GTK_STACK (gtk_stack_new ());
  self->status = wrapping ("");
  gtk_label_set_xalign (self->status, 0.5F);
  gtk_label_set_justify (self->status, GTK_JUSTIFY_CENTER);
  gtk_stack_add_named (self->stack, GTK_WIDGET (self->status), "status");
  self->list = GTK_LIST_BOX (gtk_list_box_new ());
  gtk_list_box_set_selection_mode (self->list, GTK_SELECTION_NONE);
  GtkWidget *scrolled = gtk_scrolled_window_new (nullptr, nullptr);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled), GTK_POLICY_NEVER,
                                  GTK_POLICY_AUTOMATIC);
  gtk_container_add (GTK_CONTAINER (scrolled), GTK_WIDGET (self->list));
  gtk_stack_add_named (self->stack, scrolled, "results");
  gtk_box_pack_start (box, GTK_WIDGET (self->stack), true, true, 0);

  gtk_container_add (GTK_CONTAINER (self), GTK_WIDGET (box));
  gtk_widget_show_all (GTK_WIDGET (box));
  gtk_widget_hide (GTK_WIDGET (self->info));

  gbk_connect (G_OBJECT (self), self->entry, "changed", G_CALLBACK (on_entry_changed), self);
  gbk_connect (G_OBJECT (self), self->entry, "activate", G_CALLBACK (on_entry_activate), self);
  gbk_connect (G_OBJECT (self), self->list, "row-activated", G_CALLBACK (on_row_activated),
               nullptr);
  gbk_connect (G_OBJECT (self), self, "key-press-event", G_CALLBACK (on_key_press), nullptr);
  refresh (self);
}

static ChandlerWindow *
open_window (GtkApplication *app, const char *catalog_dir)
{
  ChandlerWindow *self = g_object_new (CHANDLER_TYPE_WINDOW, nullptr);
  gtk_window_set_application (GTK_WINDOW (self), app);
  Start *start = g_new0 (Start, 1);
  start->catalog_dir = g_strdup (catalog_dir);
  chandler_aliases_load (G_OBJECT (self), on_aliases, start);
  /* on_aliases runs while the window lives; the Start goes with the window. */
  g_object_set_data_full (G_OBJECT (self), "chandler-start", start, (GDestroyNotify) start_free);
  chandler_packages_open (G_OBJECT (self), on_packages, nullptr);
  return self;
}

ChandlerWindow *
chandler_window_new (GtkApplication *app)
{
  return open_window (app, nullptr);
}

ChandlerWindow *
chandler_window_new_at (GtkApplication *app, const char *catalog_dir)
{
  return open_window (app, catalog_dir);
}

/* --- Tests --- */

char *
chandler_window_describe_for_test (ChandlerWindow *self)
{
  GString *out = g_string_new (nullptr);
  g_string_append_printf (out, "heading\t%s\n", gtk_label_get_text (self->heading));
  g_string_append_printf (out, "info\t%s\n",
                          gtk_widget_get_visible (GTK_WIDGET (self->info))
                              ? gtk_label_get_text (self->info_label)
                              : "");
  if (g_str_equal (gtk_stack_get_visible_child_name (self->stack), "status"))
    g_string_append_printf (out, "status\t%s\n", gtk_label_get_text (self->status));
  g_autoptr (GPtrArray) all = rows (self);
  for (guint i = 0; i < all->len; i++)
    {
      g_autofree char *row = chandler_row_describe_for_test (g_ptr_array_index (all, i));
      g_string_append_printf (out, "row\t%s\n", row);
    }
  return g_string_free (out, false);
}

void
chandler_window_activate_row_for_test (ChandlerWindow *self, int index)
{
  GtkListBoxRow *row = gtk_list_box_get_row_at_index (self->list, index);
  if (row == nullptr)
    gbk_fatal ("no row %d", index);
  g_signal_emit_by_name (self->list, "row-activated", row);
}

void
chandler_window_type_for_test (ChandlerWindow *self, const char *text)
{
  gtk_entry_set_text (self->entry, text);
}

void
chandler_window_set_row_limit_for_test (ChandlerWindow *self, guint limit)
{
  self->row_limit = limit;
}

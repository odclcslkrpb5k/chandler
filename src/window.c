#include "window.h"
#include "aliases.h"
#include "catalog.h"
#include "keys.h"
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

/* One install at a time (plan §3.3): where it is. */
typedef enum
{
  JOB_NONE,
  JOB_CHECKING, /* resolving again and simulating: no prompt can appear (also after a confirm,
                   to check that the plan still stands) */
  JOB_CONFIRM,  /* it brings other packages: waiting for the second click */
  JOB_PROMPT,   /* installing, not yet authorized: the window stays (no orphan prompt) */
  JOB_WORKING,  /* installing, authorized: closing hides the window until it ends */
} JobPhase;

struct _ChandlerWindow
{
  GtkApplicationWindow parent;

  GtkEntry *entry;
  GtkLabel *heading;
  GtkInfoBar *info;
  GtkLabel *info_label;
  GtkExpander *details; /* PackageKit's own words, for OTHER */
  GtkLabel *details_label;
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

  JobPhase phase;
  char *job_app;         /* the application being installed */
  char **job_pkgnames;
  GObject *job_ticket;   /* owns the check: clearing it cancels that */
  ChandlerPlan *plan;    /* JOB_CONFIRM; kept while a confirm is checked again */
  bool plan_changed;     /* the check after a confirm found a different plan */
  char *detail; /* goes with `problem`: what the install would do, or PackageKit's words */
  bool close_refused; /* the user tried to close during the password prompt */
  bool asking;        /* PackageKit is waiting for the password */
  bool closed; /* closed during the install: hidden, and destroyed when it ends */
};

G_DEFINE_FINAL_TYPE (ChandlerWindow, chandler_window, GTK_TYPE_APPLICATION_WINDOW)

static void
chandler_window_dispose (GObject *object)
{
  ChandlerWindow *self = CHANDLER_WINDOW (object);
  g_clear_object (&self->ticket);
  g_clear_object (&self->job_ticket);
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
  g_free (self->job_app);
  g_strfreev (self->job_pkgnames);
  g_clear_pointer (&self->plan, chandler_plan_free);
  g_free (self->detail);
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
    self->close_refused ? "Answer the password prompt first: Chandler can close once it has gone."
                        : nullptr,
    self->plan_changed
        ? "What the install brings has changed since you looked: check it and confirm again."
        : nullptr,
    chandler_outcome_text (self->problem),
    self->truncated ? "Showing the first matches only: make the search longer to narrow it."
                    : nullptr,
  };
  g_autoptr (GString) text = g_string_new (nullptr);
  for (size_t i = 0; i < G_N_ELEMENTS (lines); i++)
    if (lines[i] != nullptr)
      g_string_append_printf (text, "%s%s", text->len > 0 ? "\n" : "", lines[i]);
  /* What a refused install would have done is part of the sentence; PackageKit's own words
   * sit behind an expander (plan §3.4). Both are outside text: filtered. */
  bool changes = self->problem == CHANDLER_OUTCOME_WOULD_CHANGE
                 || self->problem == CHANDLER_OUTCOME_UNTRUSTED;
  g_autofree char *detail
      = self->detail != nullptr ? chandler_text_for_terminal (self->detail) : nullptr;
  if (changes && detail != nullptr)
    g_string_append_printf (text, "\nIt would %s.", detail);
  bool details = self->problem == CHANDLER_OUTCOME_OTHER && detail != nullptr;
  gtk_label_set_text (self->details_label, details ? detail : "");
  gtk_widget_set_visible (GTK_WIDGET (self->details), details);
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

/* --- The install under way, over the rows' own states --- */

static bool
job_busy (const ChandlerWindow *self)
{
  return self->phase == JOB_CHECKING || self->phase == JOB_PROMPT || self->phase == JOB_WORKING;
}

static void
apply_job (ChandlerWindow *self)
{
  g_autoptr (GPtrArray) all = rows (self);
  for (guint i = 0; i < all->len; i++)
    {
      ChandlerRow *row = g_ptr_array_index (all, i);
      bool mine
          = self->job_app != nullptr && g_str_equal (chandler_row_get_id (row), self->job_app);
      chandler_row_set_blocked (row, job_busy (self) && !mine);
      if (!mine)
        continue;
      ChandlerRowState state = chandler_row_get_state (row);
      switch (self->phase)
        {
        case JOB_NONE: break;
        case JOB_CHECKING:
          if (state != CHANDLER_ROW_CHECKING)
            chandler_row_set_state (row, CHANDLER_ROW_CHECKING);
          break;
        case JOB_CONFIRM:
          if (state != CHANDLER_ROW_CONFIRM)
            chandler_row_set_confirm (row, chandler_plan_extra (self->plan),
                                      chandler_plan_updates (self->plan));
          break;
        case JOB_PROMPT:
          chandler_row_set_installing (row,
                                       self->asking ? "Waiting for authentication…" : "Starting…");
          break;
        case JOB_WORKING: chandler_row_set_installing (row, "Installing…"); break;
        }
    }
}

/* Forgets the install: done, refused, or dropped before it began. */
static void
end_job (ChandlerWindow *self)
{
  /* An install that never began leaves its row as it was: Install. */
  g_autoptr (GPtrArray) all = rows (self);
  for (guint i = 0; i < all->len && self->job_app != nullptr; i++)
    {
      ChandlerRow *row = g_ptr_array_index (all, i);
      ChandlerRowState state = chandler_row_get_state (row);
      if (g_str_equal (chandler_row_get_id (row), self->job_app)
          && (state == CHANDLER_ROW_CHECKING || state == CHANDLER_ROW_CONFIRM))
        chandler_row_set_state (row, CHANDLER_ROW_AVAILABLE);
    }
  self->phase = JOB_NONE;
  self->close_refused = false;
  self->asking = false;
  self->plan_changed = false;
  g_clear_pointer (&self->job_app, g_free);
  g_clear_pointer (&self->job_pkgnames, g_strfreev);
  g_clear_pointer (&self->plan, chandler_plan_free);
  g_clear_object (&self->job_ticket);
  apply_job (self);
}

static void
set_problem (ChandlerWindow *self, ChandlerOutcome outcome, const char *detail)
{
  self->problem = outcome;
  g_free (self->detail);
  self->detail = g_strdup (detail);
  show_info (self);
}

/* --- Package states --- */

static void
on_resolved (const ChandlerResolved *resolved, ChandlerOutcome outcome,
             GObject *owner G_GNUC_UNUSED, void *data)
{
  ChandlerWindow *self = data; /* alive: it owns the ticket that owns this call */
  if (resolved == nullptr)
    {
      /* After an install, its own outcome says more than this one. */
      if (self->problem == CHANDLER_OUTCOME_OK)
        set_problem (self, outcome, nullptr);
      set_all_rows (self, CHANDLER_ROW_UNKNOWN);
      apply_job (self);
      return;
    }
  g_autoptr (GPtrArray) all = rows (self);
  for (guint i = 0; i < all->len; i++)
    {
      ChandlerRow *row = g_ptr_array_index (all, i);
      chandler_row_set_state (row, chandler_row_state_from_pkg (chandler_resolved_app_state (
                                       resolved, chandler_row_get_pkgnames (row))));
    }
  apply_job (self);
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
  apply_job (self);
  g_auto (GStrv) names = g_strv_builder_end (builder);
  self->ticket = g_object_new (G_TYPE_OBJECT, nullptr);
  chandler_packages_resolve (self->packages, self->ticket, (const char *const *) names, on_resolved,
                             self);
}

/* --- Installing: click, check, confirm, install (plan §3.3) --- */

static void
on_installed (ChandlerOutcome outcome, const char *detail, GObject *owner G_GNUC_UNUSED, void *data)
{
  ChandlerWindow *self = data; /* the owner */
  end_job (self);
  set_problem (self, outcome == CHANDLER_OUTCOME_ALREADY ? CHANDLER_OUTCOME_OK : outcome, detail);
  if (self->closed)
    {
      gtk_widget_destroy (GTK_WIDGET (self));
      return;
    }
  /* The row reads Installed only if PackageKit now says so. */
  resolve_rows (self);
}

static void
on_stage (ChandlerInstallStage stage, GObject *owner G_GNUC_UNUSED, void *data)
{
  ChandlerWindow *self = data;
  switch (stage)
    {
    case CHANDLER_INSTALL_ASKING:
      self->asking = true;
      apply_job (self);
      return;
    case CHANDLER_INSTALL_WORKING: break;
    }
  self->phase = JOB_WORKING;
  self->close_refused = false;
  show_info (self);
  apply_job (self);
}

static void
start_install (ChandlerWindow *self)
{
  self->phase = JOB_PROMPT;
  apply_job (self);
  chandler_packages_install (self->packages, G_OBJECT (self), self->plan, on_stage, on_installed,
                             self);
}

static void
on_checked (ChandlerPlan *plan, ChandlerOutcome outcome, const char *detail,
            GObject *owner G_GNUC_UNUSED, void *data)
{
  ChandlerWindow *self = data; /* alive: it owns the ticket that owns this call */
  g_autoptr (ChandlerPlan) confirmed = g_steal_pointer (&self->plan);
  self->plan = plan;
  if (outcome != CHANDLER_OUTCOME_OK)
    {
      end_job (self);
      set_problem (self, outcome == CHANDLER_OUTCOME_ALREADY ? CHANDLER_OUTCOME_OK : outcome,
                   detail);
      resolve_rows (self);
      return;
    }
  /* Only the app's own packages: straight on. Anything more is the user's to agree to,
   * before any password prompt, and only as it was when they agreed: a confirm is checked
   * again, and a plan that has changed since is shown for a new confirm (plan §3.3). */
  if (chandler_plan_extra (plan) == 0
      || (confirmed != nullptr && chandler_plan_same (confirmed, plan)))
    {
      self->plan_changed = false;
      start_install (self);
      return;
    }
  self->plan_changed = confirmed != nullptr;
  self->phase = JOB_CONFIRM;
  show_info (self);
  apply_job (self);
}

static void
check (ChandlerWindow *self)
{
  self->phase = JOB_CHECKING;
  apply_job (self);
  g_clear_object (&self->job_ticket);
  self->job_ticket = g_object_new (G_TYPE_OBJECT, nullptr);
  chandler_packages_check (self->packages, self->job_ticket,
                           (const char *const *) self->job_pkgnames, on_checked, self);
}

static void
start_check (ChandlerWindow *self, const ChandlerRow *row)
{
  self->job_app = g_strdup (chandler_row_get_id (row));
  g_autoptr (GStrvBuilder) names = g_strv_builder_new ();
  for (const char *const *p = chandler_row_get_pkgnames (row); *p != nullptr; p++)
    g_strv_builder_add (names, *p);
  self->job_pkgnames = g_strv_builder_end (names);
  set_problem (self, CHANDLER_OUTCOME_OK, nullptr);
  check (self);
}

static void
on_row_install (ChandlerRow *row, void *data)
{
  ChandlerWindow *self = data;
  if (self->packages_state != PACKAGES_READY)
    return;
  bool mine = self->job_app != nullptr && g_str_equal (chandler_row_get_id (row), self->job_app);
  switch (self->phase)
    {
    case JOB_NONE: start_check (self, row); return;
    case JOB_CONFIRM:
      if (mine)
        {
          check (self); /* with the confirmed plan kept, to compare */
          return;
        }
      /* Another app instead: the first one's plan no longer applies. */
      end_job (self);
      start_check (self, row);
      return;
    case JOB_CHECKING:
    case JOB_PROMPT:
    case JOB_WORKING: return; /* its button is insensitive: unreachable */
    }
}

/* Closing during an install (plan §3.3): before authorization the window stays, so the
 * password prompt is never left without its owner on screen; after it, the window hides
 * and goes when the install ends. Otherwise it closes, cancelling what it was asking. */
static gboolean
on_delete (GtkWidget *widget, GdkEvent *event G_GNUC_UNUSED, void *data G_GNUC_UNUSED)
{
  ChandlerWindow *self = CHANDLER_WINDOW (widget);
  switch (self->phase)
    {
    case JOB_NONE:
    case JOB_CHECKING:
    case JOB_CONFIRM: return false;
    case JOB_PROMPT:
      self->close_refused = true;
      show_info (self);
      return true;
    case JOB_WORKING:
      self->closed = true;
      gtk_widget_hide (widget);
      return true;
    }
  return false;
}

/* Shown again (a second launch): it no longer goes when the install ends. */
static void
on_map (GtkWidget *widget, void *data G_GNUC_UNUSED)
{
  CHANDLER_WINDOW (widget)->closed = false;
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
  /* A new search drops an install that hasn't begun; one that has goes on, and its row
   * shows it if the new search lists that app. */
  if (self->phase == JOB_CHECKING || self->phase == JOB_CONFIRM)
    end_job (self);
  self->problem = CHANDLER_OUTCOME_OK;
  g_clear_pointer (&self->detail, g_free);
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
    {
      GtkWidget *row = chandler_row_new (g_ptr_array_index (apps, i));
      gbk_connect (G_OBJECT (self), row, "install", G_CALLBACK (on_row_install), self);
      gtk_container_add (GTK_CONTAINER (self->list), row);
    }
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
  gtk_window_set_icon_name (GTK_WINDOW (self), CHANDLER_APP_ID);

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
  GtkBox *info_box = GTK_BOX (gtk_box_new (GTK_ORIENTATION_VERTICAL, 4));
  gtk_box_pack_start (info_box, GTK_WIDGET (self->info_label), false, false, 0);
  self->details = GTK_EXPANDER (gtk_expander_new ("Details"));
  self->details_label = wrapping ("");
  gtk_label_set_selectable (self->details_label, true);
  gtk_container_add (GTK_CONTAINER (self->details), GTK_WIDGET (self->details_label));
  gtk_box_pack_start (info_box, GTK_WIDGET (self->details), false, false, 0);
  gtk_container_add (GTK_CONTAINER (gtk_info_bar_get_content_area (self->info)),
                     GTK_WIDGET (info_box));
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
  gtk_widget_hide (GTK_WIDGET (self->details));

  gbk_connect (G_OBJECT (self), self->entry, "changed", G_CALLBACK (on_entry_changed), self);
  gbk_connect (G_OBJECT (self), self->entry, "activate", G_CALLBACK (on_entry_activate), self);
  gbk_connect (G_OBJECT (self), self->list, "row-activated", G_CALLBACK (on_row_activated),
               nullptr);
  gbk_connect (G_OBJECT (self), self, "key-press-event", G_CALLBACK (on_key_press), nullptr);
  chandler_keys_watch (GTK_WINDOW (self));
  gbk_connect (G_OBJECT (self), self, "delete-event", G_CALLBACK (on_delete), nullptr);
  gbk_connect (G_OBJECT (self), self, "map", G_CALLBACK (on_map), nullptr);
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
  if (gtk_widget_get_visible (GTK_WIDGET (self->details)))
    g_string_append_printf (out, "details\t%s\n", gtk_label_get_text (self->details_label));
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

void
chandler_window_click_for_test (ChandlerWindow *self, int index)
{
  GtkListBoxRow *row = gtk_list_box_get_row_at_index (self->list, index);
  if (row == nullptr)
    gbk_fatal ("no row %d", index);
  chandler_row_click_for_test (CHANDLER_ROW (row));
}

void
chandler_window_focus_row_for_test (ChandlerWindow *self, int index)
{
  GtkListBoxRow *row = gtk_list_box_get_row_at_index (self->list, index);
  if (row == nullptr)
    gbk_fatal ("no row %d", index);
  gtk_widget_grab_focus (GTK_WIDGET (row));
}

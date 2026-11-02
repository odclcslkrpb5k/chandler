#include "row.h"
#include "icon.h"
#include "keys.h"
#include "lifetime.h"
#include "text.h"

struct _ChandlerRow
{
  GtkListBoxRow parent;
  char *id;
  char **pkgnames;
  ChandlerRowState state;
  /* A key's activation of the shown button counts only when the press that began it was not
   * an auto-repeat (plan §3.6): a held Enter never presses Install, nor the confirmation
   * that follows. GtkButton finishes such an activation when the key is released, or after
   * a delay, so the click is judged by the press, not by the event that delivers it. Clicks
   * no key began (a pointer's, an accessibility action's) count. */
  bool key_pending; /* a key began an activation not yet finished */
  bool key_fresh;   /* ...and no press since then was an auto-repeat */
  GtkWidget *icon;
  GtkLabel *name;
  GtkLabel *summary;
  GtkLabel *packages;
  GtkStack *action;  /* one page per state */
  GtkWidget *install; /* the AVAILABLE page's button */
  GtkWidget *confirm; /* the CONFIRM page's button */
  GtkLabel *progress; /* the INSTALLING page's words */
};

enum
{
  SIGNAL_INSTALL,
  N_SIGNALS,
};
static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE (ChandlerRow, chandler_row, GTK_TYPE_LIST_BOX_ROW)

static void
chandler_row_finalize (GObject *object)
{
  ChandlerRow *self = CHANDLER_ROW (object);
  g_free (self->id);
  g_strfreev (self->pkgnames);
  G_OBJECT_CLASS (chandler_row_parent_class)->finalize (object);
}

static void
chandler_row_class_init (ChandlerRowClass *klass)
{
  G_OBJECT_CLASS (klass)->finalize = chandler_row_finalize;
  /* The user asked to install (AVAILABLE) or confirmed (CONFIRM). */
  signals[SIGNAL_INSTALL] = g_signal_new ("install", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                                          0, nullptr, nullptr, nullptr, G_TYPE_NONE, 0);
}

static void
chandler_row_init (ChandlerRow *self)
{
  self->state = CHANDLER_ROW_UNKNOWN;
}

static const char *
state_name (ChandlerRowState state)
{
  switch (state)
    {
    case CHANDLER_ROW_UNKNOWN: return "unknown";
    case CHANDLER_ROW_CHECKING: return "checking";
    case CHANDLER_ROW_INSTALLED: return "installed";
    case CHANDLER_ROW_AVAILABLE: return "available";
    case CHANDLER_ROW_UNAVAILABLE: return "unavailable";
    case CHANDLER_ROW_AMBIGUOUS: return "ambiguous";
    case CHANDLER_ROW_CONFIRM: return "confirm";
    case CHANDLER_ROW_INSTALLING: return "installing";
    }
  gbk_fatal ("unknown row state %d", (int) state);
}

ChandlerRowState
chandler_row_state_from_pkg (ChandlerPkgState state)
{
  switch (state)
    {
    case CHANDLER_PKG_INSTALLED: return CHANDLER_ROW_INSTALLED;
    case CHANDLER_PKG_AVAILABLE: return CHANDLER_ROW_AVAILABLE;
    case CHANDLER_PKG_UNAVAILABLE: return CHANDLER_ROW_UNAVAILABLE;
    case CHANDLER_PKG_AMBIGUOUS: return CHANDLER_ROW_AMBIGUOUS;
    }
  gbk_fatal ("unknown package state %d", (int) state);
}

/* The button of the page shown, or nullptr. */
static GtkWidget *
shown_button (const ChandlerRow *self)
{
  if (self->state == CHANDLER_ROW_AVAILABLE)
    return self->install;
  if (self->state == CHANDLER_ROW_CONFIRM)
    return self->confirm;
  return nullptr;
}

/* The keys GtkButton activates on (its "activate" key bindings). */
static bool
activates (guint keyval)
{
  return keyval == GDK_KEY_Return || keyval == GDK_KEY_ISO_Enter || keyval == GDK_KEY_KP_Enter
         || keyval == GDK_KEY_space || keyval == GDK_KEY_KP_Space;
}

/* Before GtkButton's own handler, which begins the activation. */
static gboolean
on_key_press (GtkWidget *button, const GdkEventKey *event, void *data)
{
  if (activates (event->keyval))
    {
      ChandlerRow *self = data;
      self->key_pending = true;
      self->key_fresh = !chandler_keys_is_repeat (button);
    }
  return false;
}

static gboolean
on_pointer_press (GtkWidget *button G_GNUC_UNUSED, GdkEvent *event G_GNUC_UNUSED, void *data)
{
  ChandlerRow *self = data;
  self->key_pending = false;
  return false;
}

/* An activation that ends without a click (focus lost mid-press) must not judge a later
 * click. */
static void
forget_key (ChandlerRow *self)
{
  self->key_pending = false;
}

static gboolean
on_focus_out (GtkWidget *button G_GNUC_UNUSED, GdkEvent *event G_GNUC_UNUSED, void *data)
{
  forget_key (data);
  return false;
}

static void
on_clicked (GtkButton *button, void *data)
{
  ChandlerRow *self = data;
  if (GTK_WIDGET (button) != shown_button (self))
    return;
  if (self->key_pending)
    {
      forget_key (self);
      if (!self->key_fresh)
        {
          g_debug ("a click begun by an auto-repeated key: ignored");
          return;
        }
    }
  g_signal_emit (self, signals[SIGNAL_INSTALL], 0);
}

/* A wrapping label of display text. */
static GtkLabel *
text_label (const char *text)
{
  g_autofree char *clean = chandler_text_for_terminal (text);
  GtkLabel *label = GTK_LABEL (gtk_label_new (clean));
  gtk_label_set_xalign (label, 0);
  gtk_label_set_line_wrap (label, true);
  gtk_label_set_line_wrap_mode (label, PANGO_WRAP_WORD_CHAR);
  gtk_label_set_max_width_chars (label, 60);
  return label;
}

static void
add_page (ChandlerRow *self, ChandlerRowState state, GtkWidget *child)
{
  gtk_widget_set_valign (child, GTK_ALIGN_CENTER);
  gtk_stack_add_named (self->action, child, state_name (state));
}

static GtkLabel *
note_label (const char *text)
{
  GtkLabel *label = GTK_LABEL (gtk_label_new (text));
  gtk_label_set_line_wrap (label, true);
  gtk_label_set_max_width_chars (label, 18);
  gtk_label_set_justify (label, GTK_JUSTIFY_RIGHT);
  gtk_label_set_xalign (label, 1);
  gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (label)), "dim-label");
  return label;
}

static GtkWidget *
note (const char *text)
{
  return GTK_WIDGET (note_label (text));
}

static GtkWidget *
spinning (GtkLabel *label)
{
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  GtkWidget *spinner = gtk_spinner_new ();
  gtk_spinner_start (GTK_SPINNER (spinner));
  gtk_container_add (GTK_CONTAINER (box), spinner);
  gtk_container_add (GTK_CONTAINER (box), GTK_WIDGET (label));
  return box;
}

static GtkWidget *
action_button (ChandlerRow *self, const char *label)
{
  GtkWidget *button = gtk_button_new_with_label (label);
  gbk_connect (G_OBJECT (self), button, "clicked", G_CALLBACK (on_clicked), self);
  gbk_connect (G_OBJECT (self), button, "button-press-event", G_CALLBACK (on_pointer_press), self);
  gbk_connect (G_OBJECT (self), button, "key-press-event", G_CALLBACK (on_key_press), self);
  gbk_connect (G_OBJECT (self), button, "focus-out-event", G_CALLBACK (on_focus_out), self);
  return button;
}

GtkWidget *
chandler_row_new (const ChandlerApp *app)
{
  ChandlerRow *self = g_object_new (CHANDLER_TYPE_ROW, nullptr);
  self->id = g_strdup (app->id);
  self->pkgnames = g_strdupv (app->pkgnames);

  GtkGrid *grid = GTK_GRID (gtk_grid_new ());
  gtk_grid_set_column_spacing (grid, 12);
  gtk_grid_set_row_spacing (grid, 2);
  g_object_set (grid, "margin", 8, nullptr);

  self->icon = chandler_icon_new (app);
  gtk_grid_attach (grid, self->icon, 0, 0, 1, 3);

  self->name = text_label (app->name);
  PangoAttrList *bold = pango_attr_list_new ();
  pango_attr_list_insert (bold, pango_attr_weight_new (PANGO_WEIGHT_BOLD));
  gtk_label_set_attributes (self->name, bold);
  pango_attr_list_unref (bold);
  gtk_widget_set_hexpand (GTK_WIDGET (self->name), true);
  gtk_grid_attach (grid, GTK_WIDGET (self->name), 1, 0, 1, 1);

  self->summary = text_label (app->summary);
  gtk_grid_attach (grid, GTK_WIDGET (self->summary), 1, 1, 1, 1);

  g_autofree char *joined = g_strjoinv (", ", app->pkgnames);
  self->packages = text_label (joined);
  gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (self->packages)),
                               "dim-label");
  gtk_grid_attach (grid, GTK_WIDGET (self->packages), 1, 2, 1, 1);

  self->action = GTK_STACK (gtk_stack_new ());
  gtk_stack_set_homogeneous (self->action, false);
  gtk_stack_set_transition_type (self->action, GTK_STACK_TRANSITION_TYPE_NONE);
  add_page (self, CHANDLER_ROW_UNKNOWN, gtk_label_new (""));
  add_page (self, CHANDLER_ROW_CHECKING, spinning (note_label ("Checking…")));
  add_page (self, CHANDLER_ROW_INSTALLED, note ("Installed"));
  self->install = action_button (self, "Install");
  add_page (self, CHANDLER_ROW_AVAILABLE, self->install);
  add_page (self, CHANDLER_ROW_UNAVAILABLE, note ("Not available from your software sources"));
  add_page (self, CHANDLER_ROW_AMBIGUOUS, note ("Several candidate packages: use Synaptic or apt"));
  self->confirm = action_button (self, "Install");
  gtk_label_set_line_wrap (GTK_LABEL (gtk_bin_get_child (GTK_BIN (self->confirm))), true);
  gtk_label_set_max_width_chars (GTK_LABEL (gtk_bin_get_child (GTK_BIN (self->confirm))), 18);
  add_page (self, CHANDLER_ROW_CONFIRM, self->confirm);
  self->progress = note_label ("");
  add_page (self, CHANDLER_ROW_INSTALLING, spinning (self->progress));
  gtk_grid_attach (grid, GTK_WIDGET (self->action), 2, 0, 1, 3);

  gtk_container_add (GTK_CONTAINER (self), GTK_WIDGET (grid));
  gtk_widget_show_all (GTK_WIDGET (self));
  chandler_row_set_state (self, CHANDLER_ROW_UNKNOWN);
  return GTK_WIDGET (self);
}

const char *
chandler_row_get_id (const ChandlerRow *row)
{
  return row->id;
}

const char *const *
chandler_row_get_pkgnames (const ChandlerRow *row)
{
  return (const char *const *) row->pkgnames;
}

ChandlerRowState
chandler_row_get_state (const ChandlerRow *row)
{
  return row->state;
}

void
chandler_row_set_state (ChandlerRow *row, ChandlerRowState state)
{
  row->state = state;
  forget_key (row);
  gtk_stack_set_visible_child_name (row->action, state_name (state));
}

void
chandler_row_set_confirm (ChandlerRow *row, guint extra, bool updates)
{
  g_autofree char *text
      = g_strdup_printf (updates ? "Install (also installs or updates %u package%s)"
                                 : "Install (also installs %u package%s)",
                         extra, extra == 1 ? "" : "s");
  gtk_button_set_label (GTK_BUTTON (row->confirm), text);
  chandler_row_set_state (row, CHANDLER_ROW_CONFIRM);
}

void
chandler_row_set_installing (ChandlerRow *row, const char *text)
{
  gtk_label_set_text (row->progress, text);
  if (row->state != CHANDLER_ROW_INSTALLING)
    chandler_row_set_state (row, CHANDLER_ROW_INSTALLING);
}

void
chandler_row_set_blocked (ChandlerRow *row, bool blocked)
{
  gtk_widget_set_sensitive (row->install, !blocked);
  gtk_widget_set_sensitive (row->confirm, !blocked);
}

void
chandler_row_focus_action (const ChandlerRow *row)
{
  GtkWidget *button = shown_button (row);
  if (button != nullptr && gtk_widget_get_sensitive (button))
    {
      /* The Enter that got here may be held: its repeats now reach the button, and don't
       * count (on_clicked). */
      gtk_widget_grab_focus (button);
    }
}

char *
chandler_row_describe_for_test (ChandlerRow *row)
{
  const char *button = "-";
  const char *action = "-";
  GtkWidget *shown = shown_button (row);
  if (shown != nullptr)
    {
      bool sensitive = gtk_widget_get_sensitive (shown);
      bool focus = gtk_widget_is_focus (shown);
      button = sensitive ? (focus ? "sensitive+focus" : "sensitive")
                         : (focus ? "insensitive+focus" : "insensitive");
      action = gtk_button_get_label (GTK_BUTTON (shown));
    }
  else if (row->state == CHANDLER_ROW_INSTALLING)
    action = gtk_label_get_text (row->progress);
  return g_strdup_printf ("%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s", row->id, state_name (row->state),
                          chandler_icon_source_for_test (row->icon), gtk_label_get_text (row->name),
                          gtk_label_get_text (row->summary), gtk_label_get_text (row->packages),
                          button, action);
}

void
chandler_row_click_for_test (ChandlerRow *row)
{
  GtkWidget *button = shown_button (row);
  if (button == nullptr || !gtk_widget_get_sensitive (button))
    gbk_fatal ("row %s has no button to click", row->id);
  /* As a pointer's or an accessibility action's click: no key began it. */
  forget_key (row);
  gtk_button_clicked (GTK_BUTTON (button));
}

#include "row.h"
#include "icon.h"
#include "text.h"

struct _ChandlerRow
{
  GtkListBoxRow parent;
  char *id;
  char **pkgnames;
  ChandlerRowState state;
  GtkWidget *icon;
  GtkLabel *name;
  GtkLabel *summary;
  GtkLabel *packages;
  GtkStack *action;  /* one page per state */
  GtkWidget *install; /* the AVAILABLE page's button */
};

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

static GtkWidget *
note (const char *text)
{
  GtkLabel *label = GTK_LABEL (gtk_label_new (text));
  gtk_label_set_line_wrap (label, true);
  gtk_label_set_max_width_chars (label, 18);
  gtk_label_set_justify (label, GTK_JUSTIFY_RIGHT);
  gtk_label_set_xalign (label, 1);
  gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (label)), "dim-label");
  return GTK_WIDGET (label);
}

static GtkWidget *
checking (void)
{
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  GtkWidget *spinner = gtk_spinner_new ();
  gtk_spinner_start (GTK_SPINNER (spinner));
  gtk_container_add (GTK_CONTAINER (box), spinner);
  gtk_container_add (GTK_CONTAINER (box), note ("Checking…"));
  return box;
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
  add_page (self, CHANDLER_ROW_CHECKING, checking ());
  add_page (self, CHANDLER_ROW_INSTALLED, note ("Installed"));
  /* Milestone 4 gives the button its click; until then it can't be pressed. */
  self->install = gtk_button_new_with_label ("Install");
  gtk_widget_set_sensitive (self->install, false);
  add_page (self, CHANDLER_ROW_AVAILABLE, self->install);
  add_page (self, CHANDLER_ROW_UNAVAILABLE, note ("Not available from your software sources"));
  add_page (self, CHANDLER_ROW_AMBIGUOUS, note ("Several candidate packages: use Synaptic or apt"));
  gtk_grid_attach (grid, GTK_WIDGET (self->action), 2, 0, 1, 3);

  gtk_container_add (GTK_CONTAINER (self), GTK_WIDGET (grid));
  gtk_widget_show_all (GTK_WIDGET (self));
  chandler_row_set_state (self, CHANDLER_ROW_UNKNOWN);
  return GTK_WIDGET (self);
}

const char *const *
chandler_row_get_pkgnames (const ChandlerRow *row)
{
  return (const char *const *) row->pkgnames;
}

void
chandler_row_set_state (ChandlerRow *row, ChandlerRowState state)
{
  row->state = state;
  gtk_stack_set_visible_child_name (row->action, state_name (state));
}

void
chandler_row_focus_action (ChandlerRow *row)
{
  if (row->state == CHANDLER_ROW_AVAILABLE)
    gtk_widget_grab_focus (row->install);
}

char *
chandler_row_describe_for_test (ChandlerRow *row)
{
  const char *button = "-";
  if (row->state == CHANDLER_ROW_AVAILABLE)
    {
      bool sensitive = gtk_widget_get_sensitive (row->install);
      bool focus = gtk_widget_is_focus (row->install);
      button = sensitive ? (focus ? "sensitive+focus" : "sensitive")
                         : (focus ? "insensitive+focus" : "insensitive");
    }
  return g_strdup_printf ("%s\t%s\t%s\t%s\t%s\t%s\t%s", row->id, state_name (row->state),
                          chandler_icon_source_for_test (row->icon), gtk_label_get_text (row->name),
                          gtk_label_get_text (row->summary), gtk_label_get_text (row->packages),
                          button);
}

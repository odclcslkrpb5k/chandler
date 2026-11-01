#include "window.h"

struct _ChandlerWindow
{
  GtkApplicationWindow parent;
};

G_DEFINE_FINAL_TYPE (ChandlerWindow, chandler_window, GTK_TYPE_APPLICATION_WINDOW)

static void
chandler_window_class_init (ChandlerWindowClass *klass G_GNUC_UNUSED)
{
}

static void
chandler_window_init (ChandlerWindow *self)
{
  gtk_window_set_title (GTK_WINDOW (self), "Chandler");
  gtk_window_set_default_size (GTK_WINDOW (self), 640, 480);
  gtk_window_set_icon_name (GTK_WINDOW (self), "system-software-install");
}

ChandlerWindow *
chandler_window_new (GtkApplication *app)
{
  ChandlerWindow *self = g_object_new (CHANDLER_TYPE_WINDOW, nullptr);
  gtk_window_set_application (GTK_WINDOW (self), app);
  return self;
}

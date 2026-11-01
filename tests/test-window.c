#include "lifetime.h"
#include "window.h"

static void
on_title_changed (GObject *unused G_GNUC_UNUSED, GParamSpec *pspec G_GNUC_UNUSED,
                  void *data G_GNUC_UNUSED)
{
}

static void
spin (void)
{
  while (g_main_context_iteration (nullptr, false))
    ;
}

/* The window opens under its application and, once destroyed, leaves nothing behind: it
 * is finalized and no callback registration outlives it. */
static void
test_open_close (void)
{
  g_autoptr (GtkApplication) app
      = gtk_application_new ("io.github.odclcslkrpb5k.ChandlerTest", G_APPLICATION_NON_UNIQUE);
  g_autoptr (GError) error = nullptr;
  if (!g_application_register (G_APPLICATION (app), nullptr, &error))
    gbk_fatal ("register: %s", error->message);

  ChandlerWindow *win = chandler_window_new (app);
  GtkWidget *widget = GTK_WIDGET (win);
  g_object_add_weak_pointer (G_OBJECT (win), (gpointer *) &widget);
  /* One registration owned by the window, so "none left" below means the window's
   * dispose removed it, not that there never was one. */
  gbk_connect (G_OBJECT (win), win, "notify::title", G_CALLBACK (on_title_changed), nullptr);
  g_assert_cmpint (gbk_lifetime_live (), ==, 1);
  gtk_widget_show_all (widget);
  spin ();
  g_assert_true (gtk_widget_get_visible (widget));
  g_assert_cmpstr (gtk_window_get_title (GTK_WINDOW (win)), ==, "Chandler");

  gtk_widget_destroy (widget);
  spin ();
  g_assert_null (widget);
  g_assert_cmpint (gbk_lifetime_live (), ==, 0);
}

int
main (int argc, char **argv)
{
  gtk_test_init (&argc, &argv, nullptr);
  g_test_add_func ("/window/open-close", test_open_close);
  return g_test_run ();
}

#include "lifetime.h"
#include "window.h"

#define APP_ID "io.github.odclcslkrpb5k.Chandler"

static void
on_activate (GtkApplication *app, void *unused G_GNUC_UNUSED)
{
  GtkWindow *existing = gtk_application_get_active_window (app);
  if (existing != nullptr)
    {
      gtk_window_present (existing);
      return;
    }
  gtk_widget_show_all (GTK_WIDGET (chandler_window_new (app)));
}

int
main (int argc, char **argv)
{
  if (argc == 2 && g_str_equal (argv[1], "--version"))
    {
      printf ("chandler %s\n", CHANDLER_VERSION);
      return 0;
    }
  /* Unique (plan §3.3): a second launch raises the window this process already has, so
   * there is only ever one install at a time. Arguments are not handed to GApplication,
   * whose option parser refuses unknown flags; milestone 2 parses them (request.c) and
   * forwards the request to the running instance. */
  g_autoptr (GtkApplication) app = gtk_application_new (APP_ID, G_APPLICATION_DEFAULT_FLAGS);
  gbk_connect (G_OBJECT (app), app, "activate", G_CALLBACK (on_activate), nullptr);
  char *only_name[] = { argv[0], nullptr };
  int status = g_application_run (G_APPLICATION (app), 1, only_name);
  if (gbk_lifetime_live () != 0)
    g_critical ("%d registrations still live at exit", gbk_lifetime_live ());
  return status;
}

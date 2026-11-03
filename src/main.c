#include "cli.h"
#include "lifetime.h"
#include "request.h"
#include "window.h"

/* One Chandler process (plan §3.3): every launch registers the application name; the first
 * keeps it and shows the window, a later one hands its command line to the first through the
 * "request" action and exits. GApplication is never given the arguments themselves (only
 * argv[0]), so none of its own options (--gapplication-service, --help-*) can be reached. */

static void
show_request (GtkApplication *app, int argc, const char *const *argv)
{
  ChandlerRequest request;
  chandler_request_parse (argc, argv, &request);
  GtkWindow *window = gtk_application_get_active_window (app);
  if (window == nullptr)
    {
      window = GTK_WINDOW (chandler_window_new (app));
      gtk_widget_show (GTK_WIDGET (window));
    }
  switch (request.kind)
    {
    case CHANDLER_REQUEST_EMPTY:
    case CHANDLER_REQUEST_TYPE:
    case CHANDLER_REQUEST_TEXT:
      chandler_window_show_request (CHANDLER_WINDOW (window), &request);
      break;
    case CHANDLER_REQUEST_LIST:
    case CHANDLER_REQUEST_VERSION:
    case CHANDLER_REQUEST_USAGE:
      /* Another launch's --list or --help is answered by that process (main): reaching here
       * means a crafted action call. Ignore it, but show the window. */
      break;
    }
  gtk_window_present (window);
  chandler_request_clear (&request);
}

/* The parameter is another process's argv, over D-Bus: outside data, parsed by the same
 * parser as our own (BD-1). Byte strings, not text: an argument need not be UTF-8. GAction
 * has already checked the parameter's type. */
static void
on_request (GSimpleAction *action G_GNUC_UNUSED, GVariant *parameter, void *app)
{
  gsize len = 0;
  g_autofree const char **argv = g_variant_get_bytestring_array (parameter, &len);
  if (len > INT_MAX)
    return;
  show_request (GTK_APPLICATION (app), (int) len, argv);
}

static void
on_activate (GtkApplication *app, void *unused G_GNUC_UNUSED)
{
  GtkWindow *window = gtk_application_get_active_window (app);
  if (window != nullptr)
    gtk_window_present (window);
}

static int
run_window (int argc, char **argv)
{
  g_autoptr (GtkApplication) app
      = gtk_application_new (CHANDLER_APP_ID, G_APPLICATION_DEFAULT_FLAGS);
  g_autoptr (GSimpleAction) request
      = g_simple_action_new ("request", G_VARIANT_TYPE_BYTESTRING_ARRAY);
  gbk_connect (G_OBJECT (app), request, "activate", G_CALLBACK (on_request), app);
  gbk_connect (G_OBJECT (app), app, "activate", G_CALLBACK (on_activate), nullptr);
  g_action_map_add_action (G_ACTION_MAP (app), G_ACTION (request));
  /* The window's current search, readable over D-Bus (tests/check-unique.py); disabled, so
   * nobody can activate it. */
  g_autoptr (GSimpleAction) query
      = g_simple_action_new_stateful ("query", nullptr, g_variant_new_string (""));
  g_simple_action_set_enabled (query, false);
  g_action_map_add_action (G_ACTION_MAP (app), G_ACTION (query));
  g_autoptr (GError) error = nullptr;
  if (!g_application_register (G_APPLICATION (app), nullptr, &error))
    {
      fprintf (stderr, "chandler: couldn't register with the session bus (%s %d)\n",
               g_quark_to_string (error->domain), error->code);
      return 1;
    }
  /* Here when this is the first Chandler, or to the first over D-Bus otherwise. */
  g_action_group_activate_action (
      G_ACTION_GROUP (app), "request",
      g_variant_new_bytestring_array ((const char *const *) argv, argc));
  char *only_name[] = { argv[0], nullptr };
  return g_application_run (G_APPLICATION (app), 1, only_name);
}

int
main (int argc, char **argv)
{
  ChandlerRequest request;
  chandler_request_parse (argc, (const char *const *) argv, &request);
  int status = 0;
  switch (request.kind)
    {
    case CHANDLER_REQUEST_VERSION: printf ("chandler %s\n", CHANDLER_VERSION); break;
    case CHANDLER_REQUEST_USAGE:
      if (request.problem != nullptr)
        fprintf (stderr, "chandler: %s\n", request.problem);
      fputs (chandler_usage, request.problem != nullptr ? stderr : stdout);
      status = request.problem != nullptr ? 2 : 0;
      break;
    case CHANDLER_REQUEST_LIST: status = chandler_cli_list (&request.type); break;
    case CHANDLER_REQUEST_EMPTY:
    case CHANDLER_REQUEST_TYPE:
    case CHANDLER_REQUEST_TEXT: status = run_window (argc, argv); break;
    }
  chandler_request_clear (&request);
  if (gbk_lifetime_live () != 0)
    g_critical ("%d registrations still live at exit", gbk_lifetime_live ());
  return status;
}

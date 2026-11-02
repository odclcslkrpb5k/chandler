/* The window on the fixture catalog, with no PackageKit at all: the container has no system
 * bus, so every case also shows the no-daemon state (plan §3.4). The states that need
 * PackageKit's answers are in test-window-pk.c. */
#include "request.h"
#include "window-test.h"

#define NO_DAEMON_TEXT "PackageKit isn't available"

static ChandlerWindow *
show (GtkApplication *app, const char *const *argv)
{
  ChandlerWindow *win = open_fixture (app);
  ChandlerRequest request;
  int argc = 0;
  while (argv[argc] != nullptr)
    argc++;
  chandler_request_parse (argc, argv, &request);
  chandler_window_show_request (win, &request);
  chandler_request_clear (&request);
  return win;
}

/* The window opens under its application and, once destroyed, leaves nothing behind: it
 * is finalized, its loads are cancelled, and no callback registration outlives it. */
static void
test_open_close (void)
{
  GtkApplication *app = test_app ();
  ChandlerWindow *win = open_fixture (app);
  g_assert_cmpstr (gtk_window_get_title (GTK_WINDOW (win)), ==, "Chandler");
  g_assert_cmpint (gbk_lifetime_live (), >, 0);
  /* Closed while its loads are still running. */
  close_window (win);
}

static void
test_type (void)
{
  GtkApplication *app = test_app ();
  const char *const argv[] = { "chandler", "--search=application/pdf", nullptr };
  ChandlerWindow *win = show (app, argv);
  g_autofree char *shown = wait_for (win, "row\torg.gnome.Evince\t");
  g_assert_nonnull (strstr (shown, "(application/pdf)\n"));
  g_assert_nonnull (strstr (shown, NO_DAEMON_TEXT));
  g_assert_cmpint (count_rows (shown), ==, 4);
  g_autofree char *evince = row_of (shown, "org.gnome.Evince");
  g_autofree char *state = field (evince, 2);
  g_assert_cmpstr (state, ==, "unknown");
  g_autofree char *button = field (evince, 7);
  g_assert_cmpstr (button, ==, "-");
  g_autofree char *packages = field (evince, 6);
  g_assert_cmpstr (packages, ==, "evince");
  g_assert_cmpstr (chandler_window_get_query (win), ==, "application/pdf");
  close_window (win);
}

/* Metadata is shown as text: markup appears literally, and controls (ESC, TAB) are gone. */
static void
test_text_not_markup (void)
{
  GtkApplication *app = test_app ();
  const char *const argv[] = { "chandler", "application/pdf", nullptr };
  ChandlerWindow *win = show (app, argv);
  g_autofree char *shown = wait_for (win, "row\tchandler.test.Markup\t");
  g_autofree char *row = row_of (shown, "chandler.test.Markup");
  g_autofree char *name = field (row, 4);
  g_assert_cmpstr (name, ==, "<b>Bold</b> & Viewer");
  g_autofree char *summary = field (row, 5);
  g_assert_null (strchr (summary, '\x1b'));
  g_assert_nonnull (strstr (summary, "<i>PDF</i>"));
  close_window (win);
}

/* A cached icon is read and shown; one outside the icon root never is (plan §3.2). */
static void
test_icons (void)
{
  GtkApplication *app = test_app ();
  const char *const argv[] = { "chandler", "application/pdf", nullptr };
  ChandlerWindow *win = show (app, argv);
  g_autofree char *shown = wait_for (win, "row\torg.gnome.Evince\t");
  g_autofree char *evince = row_of (shown, "org.gnome.Evince");
  g_autofree char *icon = field (evince, 3);
  g_assert_cmpstr (icon, ==, "cached");
  g_autofree char *escape = row_of (shown, "chandler.test.Escape");
  g_autofree char *bait = field (escape, 3);
  g_assert_cmpstr (bait, ==, "generic");
  close_window (win);
}

static void
test_no_results (void)
{
  GtkApplication *app = test_app ();
  const char *const argv[] = { "chandler", "--search=application/x-chandler-nothing", nullptr };
  ChandlerWindow *win = show (app, argv);
  g_autofree char *shown = wait_for (win, "status\tNo app in your software sources opens");
  g_assert_cmpint (count_rows (shown), ==, 0);
  close_window (win);
}

static void
test_empty_catalog (void)
{
  GtkApplication *app = test_app ();
  g_autoptr (GError) error = nullptr;
  g_autofree char *dir = g_dir_make_tmp ("chandler-empty-XXXXXX", &error);
  if (dir == nullptr)
    gbk_fatal ("tmp: %s", error->message);
  ChandlerWindow *win = chandler_window_new_at (app, dir);
  ChandlerRequest request;
  const char *const argv[] = { "chandler", "application/pdf", nullptr };
  chandler_request_parse (2, argv, &request);
  chandler_window_show_request (win, &request);
  chandler_request_clear (&request);
  g_autofree char *shown = wait_for (win, "status\tNo app metadata");
  g_assert_cmpint (count_rows (shown), ==, 0);
  close_window (win);
  g_assert_cmpint (g_rmdir (dir), ==, 0);
}

/* Typing searches only after the typing delay; text that isn't a media type searches names. */
static void
test_typing (void)
{
  GtkApplication *app = test_app ();
  const char *const argv[] = { "chandler", nullptr };
  ChandlerWindow *win = show (app, argv);
  g_autofree char *empty = wait_for (win, "status\tType a file type");
  /* Keys typed within the delay of each other make one search, of the last text. */
  chandler_window_type_for_test (win, "Ev");
  gint64 until = g_get_monotonic_time () + 120 * G_TIME_SPAN_MILLISECOND;
  while (g_get_monotonic_time () < until)
    g_main_context_iteration (nullptr, false);
  g_assert_cmpstr (chandler_window_get_query (win), ==, "");
  chandler_window_type_for_test (win, "Evince");
  until = g_get_monotonic_time () + 120 * G_TIME_SPAN_MILLISECOND;
  while (g_get_monotonic_time () < until)
    g_main_context_iteration (nullptr, false);
  g_assert_cmpstr (chandler_window_get_query (win), ==, "");
  g_autofree char *shown = wait_for (win, "row\torg.gnome.Evince\t");
  g_assert_nonnull (strstr (shown, "heading\tApps matching \"Evince\"\n"));
  g_assert_cmpstr (chandler_window_get_query (win), ==, "Evince");
  /* The no-daemon line belongs to the window, not the search: it stays. */
  g_assert_nonnull (strstr (shown, NO_DAEMON_TEXT));
  /* One character is not a search. */
  chandler_window_type_for_test (win, "E");
  g_autofree char *one = wait_for (win, "status\tType at least two characters");
  g_assert_cmpint (count_rows (one), ==, 0);
  /* A media type typed in looks the type up. */
  chandler_window_type_for_test (win, "text/markdown");
  g_autofree char *markdown = wait_for (win, "(text/markdown)\n");
  g_autofree char *retext = row_of (markdown, "me.mitya57.ReText");
  g_assert_nonnull (retext);
  close_window (win);
}

/* A second request (a second launch) replaces the search, the unsupported-flag note with it. */
static void
test_second_request (void)
{
  GtkApplication *app = test_app ();
  const char *const install[] = { "chandler", "--install=evince", nullptr };
  ChandlerWindow *win = show (app, install);
  g_autofree char *first = wait_for (win, "status\tType a file type");
  g_assert_nonnull (strstr (first, "only finds apps by file type or name"));
  ChandlerRequest request;
  const char *const markdown[] = { "chandler", "--search=text/markdown", nullptr };
  chandler_request_parse (2, markdown, &request);
  chandler_window_show_request (win, &request);
  chandler_request_clear (&request);
  g_autofree char *second = wait_for (win, "row\tme.mitya57.ReText\t");
  g_assert_null (strstr (second, "only finds apps by file type or name"));
  g_assert_cmpint (count_rows (second), ==, 1);
  close_window (win);
}

/* Typing a search drops the launch's note about a flag Chandler doesn't handle. */
static void
test_typing_clears_note (void)
{
  GtkApplication *app = test_app ();
  const char *const install[] = { "chandler", "--install=evince", nullptr };
  ChandlerWindow *win = show (app, install);
  g_autofree char *first = wait_for (win, "only finds apps by file type or name");
  chandler_window_type_for_test (win, "text/markdown");
  g_autofree char *typed = wait_for (win, "row\tme.mitya57.ReText\t");
  g_assert_null (strstr (typed, "only finds apps by file type or name"));
  close_window (win);
}

/* More matches than the window shows: the first ones, and a line saying so. */
static void
test_row_limit (void)
{
  GtkApplication *app = test_app ();
  ChandlerWindow *win = open_fixture (app);
  chandler_window_set_row_limit_for_test (win, 2);
  ChandlerRequest request;
  const char *const argv[] = { "chandler", "application/pdf", nullptr };
  chandler_request_parse (2, argv, &request);
  chandler_window_show_request (win, &request);
  chandler_request_clear (&request);
  g_autofree char *shown = wait_for (win, "Showing the first matches only");
  g_assert_cmpint (count_rows (shown), ==, 2);
  close_window (win);
}

/* Esc with a modifier is not Esc. */
static void
test_modified_escape (void)
{
  GtkApplication *app = test_app ();
  const char *const argv[] = { "chandler", nullptr };
  ChandlerWindow *win = show (app, argv);
  g_autofree char *shown = wait_for (win, "status\tType a file type");
  GtkWidget *widget = GTK_WIDGET (win);
  GdkEvent *press = gdk_event_new (GDK_KEY_PRESS);
  press->key.window = g_object_ref (gtk_widget_get_window (widget));
  press->key.keyval = GDK_KEY_Escape;
  press->key.state = GDK_CONTROL_MASK;
  gdk_event_set_device (press, gdk_seat_get_keyboard (
                                   gdk_display_get_default_seat (gtk_widget_get_display (widget))));
  gtk_widget_event (widget, press);
  gdk_event_free (press);
  while (g_main_context_iteration (nullptr, false))
    ;
  g_assert_true (gtk_widget_get_visible (widget));
  close_window (win);
}

static void
test_escape_closes (void)
{
  GtkApplication *app = test_app ();
  const char *const argv[] = { "chandler", "application/pdf", nullptr };
  ChandlerWindow *win = show (app, argv);
  g_autofree char *shown = wait_for (win, "row\torg.gnome.Evince\t");
  GtkWidget *widget = GTK_WIDGET (win);
  g_object_add_weak_pointer (G_OBJECT (win), (gpointer *) &widget);
  /* The press alone: gtk_test_widget_send_key would also send the release, to a window the
   * press has closed. */
  GdkEvent *press = gdk_event_new (GDK_KEY_PRESS);
  press->key.window = g_object_ref (gtk_widget_get_window (widget));
  press->key.keyval = GDK_KEY_Escape;
  gdk_event_set_device (press, gdk_seat_get_keyboard (
                                   gdk_display_get_default_seat (gtk_widget_get_display (widget))));
  g_assert_true (gtk_widget_event (widget, press));
  gdk_event_free (press);
  while (g_main_context_iteration (nullptr, false))
    ;
  g_assert_null (widget);
  while (chandler_async_live () > 0)
    g_main_context_iteration (nullptr, true);
  g_assert_cmpint (gbk_lifetime_live (), ==, 0);
}

/* TS-14: with the longest real summary showing, the window still fits a narrow screen: its
 * minimum width doesn't grow with the text. */
static void
test_narrow (void)
{
  GtkApplication *app = test_app ();
  const char *const argv[] = { "chandler", "application/pdf", nullptr };
  ChandlerWindow *win = show (app, argv);
  g_autofree char *shown = wait_for (win, "row\torg.gnome.Evince\t");
  int minimum = 0;
  gtk_widget_get_preferred_width (GTK_WIDGET (win), &minimum, nullptr);
  g_assert_cmpint (minimum, <=, 360);
  close_window (win);
}

int
main (int argc, char **argv)
{
  gtk_test_init (&argc, &argv, nullptr);
  g_test_add_func ("/window/open-close", test_open_close);
  g_test_add_func ("/window/type", test_type);
  g_test_add_func ("/window/text-not-markup", test_text_not_markup);
  g_test_add_func ("/window/icons", test_icons);
  g_test_add_func ("/window/no-results", test_no_results);
  g_test_add_func ("/window/empty-catalog", test_empty_catalog);
  g_test_add_func ("/window/typing", test_typing);
  g_test_add_func ("/window/second-request", test_second_request);
  g_test_add_func ("/window/escape-closes", test_escape_closes);
  g_test_add_func ("/window/modified-escape", test_modified_escape);
  g_test_add_func ("/window/typing-clears-note", test_typing_clears_note);
  g_test_add_func ("/window/row-limit", test_row_limit);
  g_test_add_func ("/window/narrow", test_narrow);
  return g_test_run ();
}

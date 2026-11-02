/* The window against the fake PackageKit (plan §4.3): run only by tests/with-fake-pk.py, one
 * case per scenario in tests/data/pk/window/. */
#include "request.h"
#include "scenario.h"
#include "window-test.h"

static ChandlerWindow *
show_type (GtkApplication *app, const char *type)
{
  ChandlerWindow *win = open_fixture (app);
  ChandlerRequest request;
  const char *const argv[] = { "chandler", type, nullptr };
  chandler_request_parse (2, argv, &request);
  chandler_window_show_request (win, &request);
  chandler_request_clear (&request);
  return win;
}

static void
expect_row (const char *shown, const char *id, const char *state, const char *button)
{
  g_autofree char *row = row_of (shown, id);
  g_autofree char *got_state = field (row, 2);
  g_autofree char *got_button = field (row, 7);
  g_assert_cmpstr (got_state, ==, state);
  g_assert_cmpstr (got_button, ==, button);
}

static void
case_row_states (void)
{
  GtkApplication *app = test_app ();
  ChandlerWindow *win = show_type (app, "application/pdf");
  g_autofree char *shown = wait_for (win, "row\torg.gnome.Evince\tinstalled\t");
  expect_row (shown, "org.gnome.Evince", "installed", "-");
  expect_row (shown, "xournal.desktop", "available", "insensitive");
  expect_row (shown, "chandler.test.Markup", "unavailable", "-");
  expect_row (shown, "chandler.test.Escape", "ambiguous", "-");
  g_assert_nonnull (strstr (shown, "info\t\n"));
  close_window (win);
}

static void
case_resolve_error (void)
{
  GtkApplication *app = test_app ();
  ChandlerWindow *win = show_type (app, "text/markdown");
  g_autofree char *shown = wait_for (win, "Another package manager is running");
  expect_row (shown, "me.mitya57.ReText", "unknown", "-");
  close_window (win);
}

static bool
search_nothing (void *win)
{
  ChandlerRequest request;
  const char *const argv[] = { "chandler", "application/x-chandler-nothing", nullptr };
  chandler_request_parse (2, argv, &request);
  chandler_window_show_request (win, &request);
  chandler_request_clear (&request);
  return false;
}

static void
case_requery_cancels (void)
{
  GtkApplication *app = test_app ();
  ChandlerWindow *win = show_type (app, "text/markdown");
  wait_shown (win, "row\tme.mitya57.ReText\tchecking\t");
  /* Search again while the daemon is still answering: the fake waits a second before each
   * step, and 300 ms is long enough for the Resolve call to have reached it. */
  g_autoptr (GObject) clock = g_object_new (G_TYPE_OBJECT, nullptr);
  gbk_timeout_add (clock, 300, search_nothing, win);
  g_autofree char *shown = wait_for (win, "status\tNo app in your software sources opens");
  g_assert_cmpint (count_rows (shown), ==, 0);
  /* Nothing of the cancelled question reaches the new search (and the fake's verdict
   * requires that the daemon saw the Cancel). */
  g_assert_nonnull (strstr (shown, "info\t\n"));
  g_clear_object (&clock);
  close_window (win);
}

/* The window closes while the daemon is answering: the question is cancelled there (the
 * fake's verdict), and nothing reaches the window that has gone. */
static void
case_close_mid_resolve (void)
{
  GtkApplication *app = test_app ();
  ChandlerWindow *win = show_type (app, "text/markdown");
  wait_shown (win, "row\tme.mitya57.ReText\tchecking\t");
  /* The fake waits a second before each step; 300 ms puts the Resolve call on it. */
  gint64 until = g_get_monotonic_time () + 300 * G_TIME_SPAN_MILLISECOND;
  while (g_get_monotonic_time () < until)
    g_main_context_iteration (nullptr, false);
  close_window (win);
}

static const ScenarioCase cases[] = {
  { "row-states", case_row_states },
  { "resolve-error", case_resolve_error },
  { "requery-cancels", case_requery_cancels },
  { "close-mid-resolve", case_close_mid_resolve },
};

int
main (int argc, char **argv)
{
  gtk_init (&argc, &argv);
  return scenario_main (argc, argv, cases, G_N_ELEMENTS (cases));
}

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
  expect_row (shown, "xournal.desktop", "available", "sensitive");
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

/* --- Installing --- */

#define HELLO "chandler.test.Hello"

static ChandlerWindow *
show_hello (void)
{
  ChandlerWindow *win = show_type (test_app (), "application/x-chandler-hello");
  g_autofree char *shown = wait_for (win, "row\t" HELLO "\tavailable\t");
  expect_row (shown, HELLO, "available", "sensitive");
  return win;
}

static char *
action_of (const char *shown, const char *id)
{
  g_autofree char *row = row_of (shown, id);
  return field (row, 8);
}

/* Runs the main loop until nothing of a gone window is left: it was finalized, and no call
 * or callback registration outlives it. `widget` is cleared by a weak pointer. */
static void
wait_gone (GtkWidget *const *widget)
{
  gint64 deadline = g_get_monotonic_time () + WAIT_LIMIT_US;
  while (*widget != nullptr || chandler_async_live () > 0)
    {
      if (g_get_monotonic_time () > deadline)
        gbk_fatal ("the window never went (%s)",
                   *widget != nullptr ? "still there" : "calls running");
      g_main_context_iteration (nullptr, false);
      g_usleep (2 * G_TIME_SPAN_MILLISECOND);
    }
  while (g_main_context_iteration (nullptr, false))
    ;
  if (gbk_lifetime_live () != 0)
    gbk_fatal ("%d callback registrations outlived the window", gbk_lifetime_live ());
}

/* Runs the main loop for `ms`. */
static void
spin (int ms)
{
  gint64 until = g_get_monotonic_time () + ms * G_TIME_SPAN_MILLISECOND;
  while (g_get_monotonic_time () < until)
    {
      g_main_context_iteration (nullptr, false);
      g_usleep (2 * G_TIME_SPAN_MILLISECOND);
    }
}

/* A key event as the display would deliver it to the window, with the keymap's keycode
 * (GTK's key bindings, which make Enter activate a button, look at the keycode). */
static void
send_key (ChandlerWindow *win, guint keyval, bool press)
{
  GdkDisplay *display = gtk_widget_get_display (GTK_WIDGET (win));
  g_autofree GdkKeymapKey *keys = nullptr;
  int n = 0;
  if (!gdk_keymap_get_entries_for_keyval (gdk_keymap_get_for_display (display), keyval, &keys, &n)
      || n == 0)
    gbk_fatal ("no keycode for keyval %u", keyval);
  GdkEvent *event = gdk_event_new (press ? GDK_KEY_PRESS : GDK_KEY_RELEASE);
  event->key.window = g_object_ref (gtk_widget_get_window (GTK_WIDGET (win)));
  event->key.send_event = true;
  event->key.time = GDK_CURRENT_TIME;
  event->key.keyval = keyval;
  event->key.hardware_keycode = (guint16) keys[0].keycode;
  event->key.group = (guint8) keys[0].group;
  gdk_event_set_device (event, gdk_seat_get_keyboard (gdk_display_get_default_seat (display)));
  gtk_main_do_event (event);
  gdk_event_free (event);
}

/* Enter held down on row `index`: the first press activates the row (focusing its button);
 * then presses at the keyboard's repeat rate with no release, Shift tapped in the middle,
 * and at last the release. None of it may press the button (plan §3.6). */
static void
hold_enter (ChandlerWindow *win, int index)
{
  chandler_window_focus_row_for_test (win, index);
  for (int i = 0; i < 12; i++)
    {
      send_key (win, GDK_KEY_Return, true);
      spin (35);
      if (i == 6)
        {
          send_key (win, GDK_KEY_Shift_L, true);
          send_key (win, GDK_KEY_Shift_L, false);
        }
    }
  spin (400); /* past GtkButton's activation delay */
  send_key (win, GDK_KEY_Return, false);
  spin (400);
}

static void
case_install (void)
{
  ChandlerWindow *win = show_hello ();
  chandler_window_click_for_test (win, 0);
  g_autofree char *shown = wait_for (win, "row\t" HELLO "\tinstalled\t");
  g_assert_nonnull (strstr (shown, "info\t\n"));
  close_window (win);
}

static void
case_dismiss (void)
{
  ChandlerWindow *win = show_hello ();
  chandler_window_click_for_test (win, 0);
  /* Shown once PackageKit reports WAITING_FOR_AUTH ("Starting…" before it). */
  wait_shown (win, "Waiting for authentication…");
  /* The prompt is up: the window stays, and says why. */
  gtk_window_close (GTK_WINDOW (win));
  spin (100);
  g_assert_true (gtk_widget_get_visible (GTK_WIDGET (win)));
  wait_shown (win, "Answer the password prompt first");
  g_autofree char *shown = wait_for (win, "authentication didn't complete");
  expect_row (shown, HELLO, "available", "sensitive");
  g_assert_null (strstr (shown, "Answer the password prompt first"));
  close_window (win);
}

static void
case_no_network (void)
{
  ChandlerWindow *win = show_hello ();
  chandler_window_click_for_test (win, 0);
  g_autofree char *shown = wait_for (win, "Couldn't download the package.");
  expect_row (shown, HELLO, "available", "sensitive");
  close_window (win);
}

static void
case_close_after_auth (void)
{
  ChandlerWindow *win = show_hello ();
  GtkWidget *widget = GTK_WIDGET (win);
  g_object_add_weak_pointer (G_OBJECT (win), (gpointer *) &widget);
  chandler_window_click_for_test (win, 0);
  wait_shown (win, "Installing…");
  gtk_window_close (GTK_WINDOW (win)); /* delivered from an idle */
  spin (100);
  /* Hidden, not gone: the install (and the revoke after it) finish first. */
  g_assert_nonnull (widget);
  g_assert_false (gtk_widget_get_visible (widget));
  wait_gone (&widget);
}

static void
case_other_error (void)
{
  ChandlerWindow *win = show_hello ();
  chandler_window_click_for_test (win, 0);
  g_autofree char *shown = wait_for (win, "Something went wrong in PackageKit.");
  g_assert_nonnull (strstr (shown, "details\tfake: the backend fell over\n"));
  close_window (win);
}

static void
expect_refused (const char *needle, const char *list)
{
  ChandlerWindow *win = show_hello ();
  chandler_window_click_for_test (win, 0);
  g_autofree char *shown = wait_for (win, needle);
  g_assert_nonnull (strstr (shown, list));
  expect_row (shown, HELLO, "available", "sensitive");
  close_window (win);
}

static void
case_would_change (void)
{
  expect_refused ("This would also change other software: use Synaptic or apt.",
                  "It would remove libfoo1, downgrade libbar2, replace libold3, reinstall "
                  "libsame4, install unsigned libunsigned5, change libodd6.");
}

static void
case_other_version (void)
{
  expect_refused ("No longer available", "info\tNo longer available");
}

static void
case_confirm_changed (void)
{
  ChandlerWindow *win = show_type (test_app (), "application/x-chandler-fonts");
  g_autofree char *listed = wait_for (win, "row\tchandler.test.Fonts\tavailable\t");
  expect_row (listed, "chandler.test.Fonts", "available", "sensitive");
  chandler_window_click_for_test (win, 0);
  g_autofree char *first = wait_for (win, "row\tchandler.test.Fonts\tconfirm\t");
  g_autofree char *before = action_of (first, "chandler.test.Fonts");
  g_assert_cmpstr (before, ==, "Install (also installs 2 packages)");
  chandler_window_click_for_test (win, 0);
  g_autofree char *shown = wait_for (win, "What the install brings has changed");
  g_autofree char *action = action_of (shown, "chandler.test.Fonts");
  g_assert_cmpstr (action, ==, "Install (also installs 3 packages)");
  close_window (win);
}

static void
case_reshow (void)
{
  ChandlerWindow *win = show_hello ();
  GtkWidget *widget = GTK_WIDGET (win);
  g_object_add_weak_pointer (G_OBJECT (win), (gpointer *) &widget);
  chandler_window_click_for_test (win, 0);
  wait_shown (win, "Installing…");
  gtk_window_close (GTK_WINDOW (win)); /* delivered from an idle */
  spin (100);
  g_assert_false (gtk_widget_get_visible (widget));
  /* A second launch presents the window (main.c): it no longer goes with the install. */
  gtk_window_present (GTK_WINDOW (win));
  g_autofree char *shown = wait_for (win, "row\t" HELLO "\tinstalled\t");
  g_assert_nonnull (widget);
  g_assert_true (gtk_widget_get_visible (widget));
  g_object_remove_weak_pointer (G_OBJECT (win), (gpointer *) &widget);
  close_window (win);
}

static void
case_revoke_fails (void)
{
  ChandlerWindow *win = show_hello ();
  chandler_window_click_for_test (win, 0);
  g_autofree char *shown = wait_for (win, "row\t" HELLO "\tinstalled\t");
  g_assert_nonnull (strstr (shown, "info\t\n"));
  close_window (win);
}

static void
case_untrusted (void)
{
  expect_refused ("Not signed by a trusted source.", "It would install unsigned hello.");
}

static void
case_leaves_out (void)
{
  expect_refused ("No longer available", "info\tNo longer available");
}

static void
case_updates (void)
{
  ChandlerWindow *win = show_hello ();
  GtkWidget *widget = GTK_WIDGET (win);
  g_object_add_weak_pointer (G_OBJECT (win), (gpointer *) &widget);
  chandler_window_click_for_test (win, 0);
  g_autofree char *shown = wait_for (win, "row\t" HELLO "\tconfirm\t");
  g_autofree char *action = action_of (shown, HELLO);
  g_assert_cmpstr (action, ==, "Install (also installs or updates 1 package)");
  /* Nothing has begun: closing closes, and installs nothing (the fake's verdict). */
  gtk_window_close (GTK_WINDOW (win));
  wait_gone (&widget);
}

static void
case_gone (void)
{
  ChandlerWindow *win = show_hello ();
  chandler_window_click_for_test (win, 0);
  g_autofree char *shown = wait_for (win, "row\t" HELLO "\tunavailable\t");
  g_assert_nonnull (strstr (shown, "No longer available"));
  close_window (win);
}

static void
case_already (void)
{
  ChandlerWindow *win = show_hello ();
  chandler_window_click_for_test (win, 0);
  g_autofree char *shown = wait_for (win, "row\t" HELLO "\tinstalled\t");
  g_assert_nonnull (strstr (shown, "info\t\n"));
  close_window (win);
}

static void
case_enter_focus (void)
{
  ChandlerWindow *win = show_hello ();
  hold_enter (win, 0);
  g_autofree char *shown = chandler_window_describe_for_test (win);
  expect_row (shown, HELLO, "available", "sensitive+focus");
  close_window (win);
}

static void
case_confirm (void)
{
  ChandlerWindow *win = show_type (test_app (), "application/x-chandler-fonts");
  g_autofree char *listed = wait_for (win, "row\tchandler.test.Fonts\tavailable\t");
  expect_row (listed, "chandler.test.Fonts", "available", "sensitive");
  chandler_window_click_for_test (win, 0);
  g_autofree char *shown = wait_for (win, "row\tchandler.test.Fonts\tconfirm\t");
  g_autofree char *action = action_of (shown, "chandler.test.Fonts");
  g_assert_cmpstr (action, ==, "Install (also installs 2 packages)");
  /* Enter on the row, held: the confirmation is focused, never pressed. */
  hold_enter (win, 0);
  g_autofree char *held = chandler_window_describe_for_test (win);
  expect_row (held, "chandler.test.Fonts", "confirm", "sensitive+focus");
  /* A fresh press confirms; the plan is checked again, unchanged, and installed. */
  send_key (win, GDK_KEY_Return, true);
  send_key (win, GDK_KEY_Return, false);
  g_autofree char *done = wait_for (win, "row\tchandler.test.Fonts\tinstalled\t");
  g_assert_nonnull (strstr (done, "info\t\n"));
  close_window (win);
}

/* The index of application `id`'s row. */
static int
index_of (const char *shown, const char *id)
{
  g_autofree char *prefix = g_strdup_printf ("row\t%s\t", id);
  g_auto (GStrv) lines = g_strsplit (shown, "\n", -1);
  int index = 0;
  for (char **l = lines; *l != nullptr; l++)
    {
      if (g_str_has_prefix (*l, prefix))
        return index;
      if (g_str_has_prefix (*l, "row\t"))
        index++;
    }
  gbk_fatal ("no row for %s", id);
}

static void
case_one_at_a_time (void)
{
  ChandlerWindow *win = show_type (test_app (), "application/x-chandler-both");
  g_autofree char *listed = wait_for (win, "row\t" HELLO "\tavailable\t");
  expect_row (listed, "chandler.test.Fonts", "available", "sensitive");
  chandler_window_click_for_test (win, index_of (listed, "chandler.test.Fonts"));
  g_autofree char *confirming = wait_for (win, "row\tchandler.test.Fonts\tconfirm\t");
  expect_row (confirming, HELLO, "available", "sensitive");
  /* The other app instead: the first goes back to Install. */
  chandler_window_click_for_test (win, index_of (listed, HELLO));
  g_autofree char *dropped = chandler_window_describe_for_test (win);
  expect_row (dropped, "chandler.test.Fonts", "available", "insensitive");
  wait_shown (win, "row\t" HELLO "\tinstalling\t");
  g_autofree char *prompting = chandler_window_describe_for_test (win);
  expect_row (prompting, "chandler.test.Fonts", "available", "insensitive");
  g_autofree char *shown = wait_for (win, "authentication didn't complete");
  expect_row (shown, HELLO, "available", "sensitive");
  expect_row (shown, "chandler.test.Fonts", "available", "sensitive");
  close_window (win);
}

static const ScenarioCase cases[] = {
  { "one-at-a-time", case_one_at_a_time },
  { "install", case_install },
  { "dismiss", case_dismiss },
  { "no-network", case_no_network },
  { "close-after-auth", case_close_after_auth },
  { "other-error", case_other_error },
  { "would-change", case_would_change },
  { "untrusted", case_untrusted },
  { "leaves-out", case_leaves_out },
  { "updates", case_updates },
  { "gone", case_gone },
  { "already", case_already },
  { "enter-focus", case_enter_focus },
  { "confirm", case_confirm },
  { "other-version", case_other_version },
  { "confirm-changed", case_confirm_changed },
  { "reshow", case_reshow },
  { "revoke-fails", case_revoke_fails },
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

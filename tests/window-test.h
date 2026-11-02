/* Helpers for driving a ChandlerWindow in tests: open one on the fixture catalog, wait for
 * what it shows, and read one row. Waiting is bounded: a window that never gets there fails
 * the test with what it did show. */
#pragma once

#include "async.h"
#include "lifetime.h"
#include "window.h"

#define WAIT_LIMIT_US (20 * G_TIME_SPAN_SECOND)

/* One application for the whole test process: a registered application exports objects on
 * the session bus connection, and a second one with the same id can't. Borrowed. */
static GtkApplication *
test_app (void)
{
  static GtkApplication *app;
  if (app != nullptr)
    return app;
  /* AppStream writes a cache, and the test environment's XDG_CACHE_HOME doesn't exist (on
   * purpose: nothing of the user's may be read): give it a private one. */
  g_autofree char *cache = g_dir_make_tmp ("chandler-test-XXXXXX", nullptr);
  if (cache == nullptr)
    gbk_fatal ("no temporary directory for the AppStream cache");
  g_setenv ("XDG_CACHE_HOME", cache, true);
  app = gtk_application_new ("io.github.odclcslkrpb5k.ChandlerTest", G_APPLICATION_NON_UNIQUE);
  g_autoptr (GError) error = nullptr;
  if (!g_application_register (G_APPLICATION (app), nullptr, &error))
    gbk_fatal ("register: %s", error->message);
  return app;
}

static char *
fixture_dir (const char *name)
{
  return g_build_filename (g_getenv ("CHANDLER_SOURCE_ROOT"), "tests", "data", name, nullptr);
}

static ChandlerWindow *
open_fixture (GtkApplication *app)
{
  g_autofree char *dir = fixture_dir ("catalog");
  ChandlerWindow *win = chandler_window_new_at (app, dir);
  gtk_widget_show (GTK_WIDGET (win));
  return win;
}

/* Runs the main loop until the window's description contains `needle`. */
static void
wait_shown (ChandlerWindow *win, const char *needle)
{
  gint64 deadline = g_get_monotonic_time () + WAIT_LIMIT_US;
  for (;;)
    {
      g_autofree char *shown = chandler_window_describe_for_test (win);
      if (strstr (shown, needle) != nullptr)
        break;
      if (g_get_monotonic_time () > deadline)
        gbk_fatal ("waited for \"%s\"; the window shows:\n%s", needle, shown);
      g_main_context_iteration (nullptr, false);
      g_usleep (2 * G_TIME_SPAN_MILLISECOND);
    }
}

/* wait_shown, then until no asynchronous call is outstanding, so what follows sees a
 * settled window. */
static char *
wait_for (ChandlerWindow *win, const char *needle)
{
  wait_shown (win, needle);
  gint64 deadline = g_get_monotonic_time () + WAIT_LIMIT_US;
  while (chandler_async_live () > 0)
    {
      if (g_get_monotonic_time () > deadline)
        gbk_fatal ("asynchronous calls still running after \"%s\"", needle);
      g_main_context_iteration (nullptr, true);
    }
  return chandler_window_describe_for_test (win);
}

/* The row line for application `id`, or nullptr. */
static char *
row_of (const char *shown, const char *id)
{
  g_autofree char *prefix = g_strdup_printf ("row\t%s\t", id);
  g_auto (GStrv) lines = g_strsplit (shown, "\n", -1);
  for (char **l = lines; *l != nullptr; l++)
    if (g_str_has_prefix (*l, prefix))
      return g_strdup (*l);
  return nullptr;
}

/* Field `n` of a row line: 0 "row", 1 id, 2 state, 3 icon, 4 name, 5 summary, 6 packages,
 * 7 button. */
static char *
field (const char *row, int n)
{
  if (row == nullptr)
    gbk_fatal ("no such row");
  g_auto (GStrv) fields = g_strsplit (row, "\t", -1);
  if (g_strv_length (fields) <= (guint) n)
    gbk_fatal ("row has no field %d: %s", n, row);
  return g_strdup (fields[n]);
}

static int
count_rows (const char *shown)
{
  int n = 0;
  for (const char *p = shown; (p = strstr (p, "\nrow\t")) != nullptr; p++)
    n++;
  return n;
}

/* Destroys the window and waits until nothing of it is left running. */
static void
close_window (ChandlerWindow *win)
{
  GtkWidget *widget = GTK_WIDGET (win);
  g_object_add_weak_pointer (G_OBJECT (win), (gpointer *) &widget);
  gtk_widget_destroy (widget);
  while (chandler_async_live () > 0)
    g_main_context_iteration (nullptr, true);
  while (g_main_context_iteration (nullptr, false))
    ;
  if (widget != nullptr)
    gbk_fatal ("the window was not finalized");
  if (gbk_lifetime_live () != 0)
    gbk_fatal ("%d callback registrations outlived the window", gbk_lifetime_live ());
}

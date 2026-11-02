/* Owner-scoped async completion (src/async.h), driven by a GTask that completes on idle. */
#include "async.h"

typedef struct
{
  int calls;
  GObject *owner_seen;
  bool cancelled_seen;
  int destroyed;
} Seen;

static void
on_done (GObject *source G_GNUC_UNUSED, GAsyncResult *result, GObject *owner, void *data)
{
  Seen *s = data;
  s->calls++;
  s->owner_seen = owner;
  g_autoptr (GError) error = nullptr;
  bool ok = g_task_propagate_boolean (G_TASK (result), &error);
  s->cancelled_seen = !ok && g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
}

static void
count_destroy (void *data)
{
  ((Seen *) data)->destroyed++;
}

static void
in_thread (GTask *task, void *source G_GNUC_UNUSED, void *data G_GNUC_UNUSED,
           GCancellable *cancellable)
{
  /* Long enough for the test to dispose the owner first, short enough to stay quick. */
  for (int i = 0; i < 50 && !g_cancellable_is_cancelled (cancellable); i++)
    g_usleep (2 * G_TIME_SPAN_MILLISECOND);
  if (!g_task_return_error_if_cancelled (task))
    g_task_return_boolean (task, true);
}

static void
start (GObject *owner, Seen *seen)
{
  ChandlerAsync *a = chandler_async_new (owner, on_done, seen, count_destroy);
  g_autoptr (GTask) task
      = g_task_new (nullptr, chandler_async_cancellable (a), chandler_async_ready, a);
  g_task_run_in_thread (task, in_thread);
}

static void
wait_idle (void)
{
  while (chandler_async_live () > 0)
    g_main_context_iteration (nullptr, true);
}

static void
test_owner_alive (void)
{
  g_autoptr (GObject) owner = g_object_new (G_TYPE_OBJECT, nullptr);
  Seen seen = { 0 };
  start (owner, &seen);
  g_assert_cmpint (chandler_async_live (), ==, 1);
  wait_idle ();
  g_assert_cmpint (seen.calls, ==, 1);
  g_assert_true (seen.owner_seen == owner);
  g_assert_false (seen.cancelled_seen);
  g_assert_cmpint (seen.destroyed, ==, 1);
}

static void
test_owner_gone (void)
{
  GObject *owner = g_object_new (G_TYPE_OBJECT, nullptr);
  Seen seen = { 0 };
  start (owner, &seen);
  g_object_unref (owner);
  wait_idle ();
  g_assert_cmpint (seen.calls, ==, 1);
  g_assert_null (seen.owner_seen);
  g_assert_true (seen.cancelled_seen);
  g_assert_cmpint (seen.destroyed, ==, 1);
}

static void
block (GObject *source G_GNUC_UNUSED, GAsyncResult *result G_GNUC_UNUSED,
       GObject *owner G_GNUC_UNUSED, void *data G_GNUC_UNUSED)
{
  g_usleep (300 * G_TIME_SPAN_MILLISECOND);
}

static void
test_stall_aborts (void)
{
  if (g_test_subprocess ())
    {
      g_autoptr (GObject) owner = g_object_new (G_TYPE_OBJECT, nullptr);
      ChandlerAsync *a = chandler_async_new (owner, block, nullptr, nullptr);
      g_autoptr (GTask) task
          = g_task_new (nullptr, chandler_async_cancellable (a), chandler_async_ready, a);
      g_task_return_boolean (task, true);
      wait_idle ();
      return;
    }
  g_test_trap_subprocess (nullptr, 0, G_TEST_SUBPROCESS_DEFAULT);
  g_test_trap_assert_failed ();
  g_test_trap_assert_stderr ("*main loop blocked*async completion block*");
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, nullptr);
  g_test_add_func ("/async/owner-alive", test_owner_alive);
  g_test_add_func ("/async/owner-gone", test_owner_gone);
  g_test_add_func ("/async/stall-aborts", test_stall_aborts);
  return g_test_run ();
}

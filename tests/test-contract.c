/* The vendored lifetime module's contract with this project (plan §4.1). The module is
 * copied unchanged from Quoin, so what it relies on from its host is checked here:
 * - the stall watch is on in tests (GBK_STALL_MS), and a callback that blocks the main loop
 *   longer than that aborts, naming itself;
 * - GBK_LIFETIME_IMPL, which lifts the bans on raw signal and source APIs, is defined only
 *   for vendor/quoin/src/lifetime.c, never for a file like this one. */
#include "lifetime.h"

#ifdef GBK_LIFETIME_IMPL
#error "GBK_LIFETIME_IMPL must be defined only for vendor/quoin/src/lifetime.c"
#endif

static void
test_stall_limit_set (void)
{
  const char *limit = g_getenv ("GBK_STALL_MS");
  if (limit == nullptr)
    gbk_fatal ("GBK_STALL_MS is not set: the stall watch is off in tests");
  g_assert_cmpstr (limit, ==, "250");
}

static bool
block_main_loop (void *unused G_GNUC_UNUSED)
{
  g_usleep (300 * G_TIME_SPAN_MILLISECOND);
  return false;
}

static void
test_stall_aborts (void)
{
  if (g_test_subprocess ())
    {
      g_autoptr (GObject) owner = g_object_new (G_TYPE_OBJECT, nullptr);
      gbk_idle_add (owner, block_main_loop, nullptr);
      for (int i = 0; i < 10; i++)
        g_main_context_iteration (nullptr, false);
      return;
    }
  g_test_trap_subprocess (nullptr, 0, G_TEST_SUBPROCESS_DEFAULT);
  g_test_trap_assert_failed ();
  g_test_trap_assert_stderr ("*main loop blocked*block_main_loop*");
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, nullptr);
  g_test_add_func ("/contract/stall-limit-set", test_stall_limit_set);
  g_test_add_func ("/contract/stall-aborts", test_stall_aborts);
  return g_test_run ();
}

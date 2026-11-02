/* packages.c against the fake PackageKit (plan §4.3). Run only by tests/with-fake-pk.py:
 * one case per scenario in tests/data/pk/scenarios/, each against its own fake daemon on a
 * private bus. `test-resolve --scenarios` lists the cases; `test-resolve <case>` runs one. */
#include "async.h"
#include "lifetime.h"
#include "packages.h"

typedef struct
{
  bool done;
  ChandlerOutcome outcome;
  ChandlerPackages *packages;
} Opened;

static void
on_open (ChandlerPackages *packages, ChandlerOutcome outcome, GObject *owner G_GNUC_UNUSED,
         void *data)
{
  Opened *o = data;
  o->done = true;
  o->outcome = outcome;
  if (outcome == CHANDLER_OUTCOME_OK)
    o->packages = g_object_ref (packages);
}

typedef struct
{
  int calls;
  ChandlerOutcome outcome;
  const char *const *names; /* the states are read for these, while `resolved` is lent */
  ChandlerPkgState states[4];
  char ids[4][128]; /* copies: "" when there is no candidate */
  ChandlerPkgState app;
} Answer;

static void
on_resolved (const ChandlerResolved *resolved, ChandlerOutcome outcome,
             GObject *owner G_GNUC_UNUSED, void *data)
{
  Answer *a = data;
  a->calls++;
  a->outcome = outcome;
  if (resolved == nullptr)
    return;
  for (int i = 0; a->names[i] != nullptr; i++)
    {
      a->states[i] = chandler_resolved_state (resolved, a->names[i]);
      const char *id = chandler_resolved_id_for_test (resolved, a->names[i]);
      g_strlcpy (a->ids[i], id != nullptr ? id : "", sizeof a->ids[i]);
    }
  a->app = chandler_resolved_app_state (resolved, a->names);
}

static void
wait_idle (void)
{
  while (chandler_async_live () > 0)
    g_main_context_iteration (nullptr, true);
}

static ChandlerPackages *
open_packages (GObject *owner)
{
  Opened o = { 0 };
  chandler_packages_open (owner, on_open, &o);
  wait_idle ();
  if (!o.done || o.outcome != CHANDLER_OUTCOME_OK)
    gbk_fatal ("the fake daemon didn't open (%s)", chandler_outcome_name (o.outcome));
  return o.packages;
}

/* Resolves `names` and waits for the answer. */
static Answer
resolve (const char *const *names)
{
  g_autoptr (GObject) owner = g_object_new (G_TYPE_OBJECT, nullptr);
  g_autoptr (ChandlerPackages) packages = open_packages (owner);
  Answer a = { .names = names };
  chandler_packages_resolve (packages, owner, names, on_resolved, &a);
  wait_idle ();
  g_assert_cmpint (a.calls, ==, 1);
  return a;
}

static void
expect_ok (const Answer *a)
{
  g_assert_cmpstr (chandler_outcome_name (a->outcome), ==,
                   chandler_outcome_name (CHANDLER_OUTCOME_OK));
}

static void
expect_state (ChandlerPkgState got, ChandlerPkgState want)
{
  g_assert_cmpstr (chandler_pkg_state_name (got), ==, chandler_pkg_state_name (want));
}

static void
case_available (void)
{
  const char *const names[] = { "hello", "hello", nullptr };
  Answer a = resolve (names);
  expect_ok (&a);
  expect_state (a.states[0], CHANDLER_PKG_AVAILABLE);
  expect_state (a.app, CHANDLER_PKG_AVAILABLE);
}

static void
case_installed (void)
{
  const char *const names[] = { "hello", nullptr };
  Answer a = resolve (names);
  expect_ok (&a);
  expect_state (a.states[0], CHANDLER_PKG_INSTALLED);
  expect_state (a.app, CHANDLER_PKG_INSTALLED);
}

static void
case_arch_all (void)
{
  const char *const names[] = { "ttf-mscorefonts-installer", nullptr };
  Answer a = resolve (names);
  expect_ok (&a);
  expect_state (a.states[0], CHANDLER_PKG_AVAILABLE);
  g_assert_cmpstr (a.ids[0], ==,
                   "ttf-mscorefonts-installer;3.8.1ubuntu2;all;ubuntu-resolute-multiverse");
}

static void
case_unknown_name (void)
{
  const char *const names[] = { "hello", "no-such-package", nullptr };
  Answer a = resolve (names);
  expect_ok (&a);
  expect_state (a.states[0], CHANDLER_PKG_AVAILABLE);
  expect_state (a.states[1], CHANDLER_PKG_UNAVAILABLE);
  expect_state (a.app, CHANDLER_PKG_UNAVAILABLE);
}

static void
case_two_versions (void)
{
  const char *const names[] = { "hello", nullptr };
  Answer a = resolve (names);
  expect_ok (&a);
  expect_state (a.states[0], CHANDLER_PKG_AMBIGUOUS);
}

static void
case_foreign_only (void)
{
  const char *const names[] = { "hello", nullptr };
  Answer a = resolve (names);
  expect_ok (&a);
  expect_state (a.states[0], CHANDLER_PKG_UNAVAILABLE);
}

static void
case_foreign_and_native (void)
{
  const char *const names[] = { "hello", nullptr };
  Answer a = resolve (names);
  expect_ok (&a);
  expect_state (a.states[0], CHANDLER_PKG_AVAILABLE);
  g_assert_cmpstr (a.ids[0], ==, "hello;2.10-5build1;amd64;ubuntu-resolute-main");
}

static void
case_foreign_installed (void)
{
  const char *const names[] = { "hello", nullptr };
  Answer a = resolve (names);
  expect_ok (&a);
  expect_state (a.states[0], CHANDLER_PKG_AVAILABLE);
  g_assert_cmpstr (a.ids[0], ==, "hello;2.10-5build1;amd64;ubuntu-resolute-main");
}

static void
case_locked (void)
{
  const char *const names[] = { "hello", nullptr };
  Answer a = resolve (names);
  g_assert_cmpstr (chandler_outcome_name (a.outcome), ==,
                   chandler_outcome_name (CHANDLER_OUTCOME_LOCKED));
}

static void
case_failed_with_packages (void)
{
  const char *const names[] = { "hello", nullptr };
  Answer a = resolve (names);
  g_assert_cmpstr (chandler_outcome_name (a.outcome), ==,
                   chandler_outcome_name (CHANDLER_OUTCOME_OTHER));
}

static void
case_installed_filter_ignored (void)
{
  const char *const names[] = { "hello", nullptr };
  Answer a = resolve (names);
  expect_ok (&a);
  expect_state (a.states[0], CHANDLER_PKG_AVAILABLE);
}

static void
case_no_daemon (void)
{
  g_autoptr (GObject) owner = g_object_new (G_TYPE_OBJECT, nullptr);
  Opened o = { 0 };
  chandler_packages_open (owner, on_open, &o);
  wait_idle ();
  g_assert_true (o.done);
  g_assert_cmpstr (chandler_outcome_name (o.outcome), ==,
                   chandler_outcome_name (CHANDLER_OUTCOME_NO_DAEMON));
  g_assert_null (o.packages);
}

static bool
drop_owner (void *owner)
{
  g_object_unref (owner);
  return G_SOURCE_REMOVE;
}

static void
case_owner_gone (void)
{
  GObject *owner = g_object_new (G_TYPE_OBJECT, nullptr);
  g_autoptr (ChandlerPackages) packages = open_packages (owner);
  const char *const names[] = { "hello", nullptr };
  Answer a = { .names = names };
  chandler_packages_resolve (packages, owner, names, on_resolved, &a);
  /* The fake waits a second before each step, so the Resolve call has long reached it. */
  g_autoptr (GObject) clock = g_object_new (G_TYPE_OBJECT, nullptr);
  gbk_timeout_add (clock, 300, drop_owner, owner);
  wait_idle ();
  g_assert_cmpint (a.calls, ==, 0);
  g_assert_cmpint (gbk_lifetime_live (), ==, 0);
}

static const struct
{
  const char *name;
  void (*run) (void);
} cases[] = {
  { "available", case_available },
  { "installed", case_installed },
  { "arch-all", case_arch_all },
  { "unknown-name", case_unknown_name },
  { "two-versions", case_two_versions },
  { "foreign-only", case_foreign_only },
  { "foreign-and-native", case_foreign_and_native },
  { "foreign-installed", case_foreign_installed },
  { "locked", case_locked },
  { "failed-with-packages", case_failed_with_packages },
  { "installed-filter-ignored", case_installed_filter_ignored },
  { "no-daemon", case_no_daemon },
  { "owner-gone", case_owner_gone },
};

int
main (int argc, char **argv)
{
  if (argc == 2 && g_str_equal (argv[1], "--scenarios"))
    {
      for (size_t i = 0; i < G_N_ELEMENTS (cases); i++)
        printf ("%s\n", cases[i].name);
      return 0;
    }
  if (argc != 2)
    gbk_fatal ("usage: test-resolve --scenarios | test-resolve <case> (run by with-fake-pk.py)");
  /* TS-6: assert what was selected. Only the runner's private bus, with the fake for this
   * very case, may answer. */
  const char *fake = g_getenv ("CHANDLER_FAKE_PK");
  const char *bus = g_getenv ("DBUS_SYSTEM_BUS_ADDRESS");
  if (fake == nullptr || !g_str_equal (fake, argv[1]) || bus == nullptr
      || strstr (bus, "/run/dbus") != nullptr)
    gbk_fatal ("not started by with-fake-pk.py for this case");
  /* The scenarios are recordings from an amd64 machine: elsewhere every native candidate
   * in them is foreign, and the cases would fail for that reason, not a real one. */
  if (!g_str_equal (CHANDLER_NATIVE_ARCH, "amd64"))
    gbk_fatal ("the PackageKit scenarios are amd64 recordings; this build is %s",
               CHANDLER_NATIVE_ARCH);
  for (size_t i = 0; i < G_N_ELEMENTS (cases); i++)
    if (g_str_equal (cases[i].name, argv[1]))
      {
        cases[i].run ();
        return 0;
      }
  gbk_fatal ("no case %s", argv[1]);
}

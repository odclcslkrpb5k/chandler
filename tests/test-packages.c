/* The PackageKit adapter. Milestone 2 half: how errors become outcomes. The resolve and
 * install paths are tested against a fake daemon written from recorded traffic (plan §4.3),
 * once that traffic is recorded. */
#include "packages.h"

static void
test_every_daemon_code (void)
{
  /* Every code PackageKit defines maps to something, and none to OK. */
  for (int code = PK_ERROR_ENUM_UNKNOWN; code <= PK_ERROR_ENUM_LAST; code++)
    g_assert_cmpint (chandler_packages_daemon_outcome_for_test (code), !=, CHANDLER_OUTCOME_OK);
  g_assert_cmpint (chandler_packages_daemon_outcome_for_test (PK_ERROR_ENUM_LAST + 7), ==,
                   CHANDLER_OUTCOME_OTHER);
}

static ChandlerOutcome
of (GQuark domain, int code)
{
  g_autoptr (GError) error = g_error_new_literal (domain, code, "message text is never read");
  return chandler_packages_outcome_for_test (error);
}

static void
test_errors (void)
{
  const int daemon = 0xff;
  g_assert_cmpint (of (PK_CLIENT_ERROR, daemon + PK_ERROR_ENUM_NOT_AUTHORIZED), ==,
                   CHANDLER_OUTCOME_AUTH_INCOMPLETE);
  g_assert_cmpint (of (PK_CLIENT_ERROR, daemon + PK_ERROR_ENUM_NO_NETWORK), ==,
                   CHANDLER_OUTCOME_NO_NETWORK);
  g_assert_cmpint (of (PK_CLIENT_ERROR, daemon + PK_ERROR_ENUM_CANNOT_GET_LOCK), ==,
                   CHANDLER_OUTCOME_LOCKED);
  g_assert_cmpint (of (PK_CLIENT_ERROR, daemon + PK_ERROR_ENUM_PACKAGE_NOT_FOUND), ==,
                   CHANDLER_OUTCOME_GONE);
  g_assert_cmpint (of (PK_CLIENT_ERROR, daemon + PK_ERROR_ENUM_PACKAGE_ALREADY_INSTALLED), ==,
                   CHANDLER_OUTCOME_ALREADY);
  g_assert_cmpint (of (PK_CLIENT_ERROR, daemon + PK_ERROR_ENUM_MISSING_GPG_SIGNATURE), ==,
                   CHANDLER_OUTCOME_UNTRUSTED);
  g_assert_cmpint (of (PK_CLIENT_ERROR, daemon + PK_ERROR_ENUM_TRANSACTION_CANCELLED), ==,
                   CHANDLER_OUTCOME_CANCELLED);
  g_assert_cmpint (of (PK_CLIENT_ERROR, PK_CLIENT_ERROR_CANNOT_START_DAEMON), ==,
                   CHANDLER_OUTCOME_NO_DAEMON);
  g_assert_cmpint (of (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED_AUTH), ==,
                   CHANDLER_OUTCOME_AUTH_INCOMPLETE);
  g_assert_cmpint (of (G_IO_ERROR, G_IO_ERROR_CANCELLED), ==, CHANDLER_OUTCOME_CANCELLED);
  g_assert_cmpint (of (G_DBUS_ERROR, G_DBUS_ERROR_SERVICE_UNKNOWN), ==, CHANDLER_OUTCOME_NO_DAEMON);
  g_assert_cmpint (of (G_FILE_ERROR, G_FILE_ERROR_NOENT), ==, CHANDLER_OUTCOME_OTHER);
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, nullptr);
  g_test_add_func ("/packages/every-daemon-code", test_every_daemon_code);
  g_test_add_func ("/packages/errors", test_errors);
  return g_test_run ();
}

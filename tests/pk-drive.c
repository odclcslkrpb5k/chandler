/* pk-drive: drives the real PackageKit daemon and prints everything it says, for recording
 * its D-Bus traffic (plan §1.2 item 3). A development tool: built with the tests, never
 * installed, run by the owner on the host while `sudo dbus-monitor --system` records.
 *
 *   pk-drive resolve FILTERS NAME...   FILTERS as PackageKit spells them ("none",
 *                                      "installed;arch", "arch;newest;~installed;~source")
 *   pk-drive simulate NAME...          resolve native newest candidates, then simulate
 *   pk-drive install NAME...           resolve, then install for real (asks polkit)
 *
 * Simulate and install use the flags Chandler will use: ONLY_TRUSTED always. */
#include "async.h"

typedef struct
{
  bool done;
  int status;
  PkClient *client;
  bool simulate;
} Drive;

static void
print_results (PkResults *results)
{
  g_print ("exit: %s\n", pk_exit_enum_to_string (pk_results_get_exit_code (results)));
  g_autoptr (GPtrArray) packages = pk_results_get_package_array (results);
  for (guint i = 0; i < packages->len; i++)
    {
      PkPackage *p = g_ptr_array_index (packages, i);
      g_print ("package: %s %s\n", pk_info_enum_to_string (pk_package_get_info (p)),
               pk_package_get_id (p));
    }
  g_autoptr (PkError) error = pk_results_get_error_code (results);
  if (error != nullptr)
    g_print ("error-code: %s (%d): %s\n", pk_error_enum_to_string (pk_error_get_code (error)),
             (int) pk_error_get_code (error), pk_error_get_details (error));
}

static void
print_error (const char *what, const GError *error)
{
  g_print ("%s failed: domain %s code %d", what, g_quark_to_string (error->domain), error->code);
  if (error->domain == PK_CLIENT_ERROR && error->code >= 0xff)
    g_print (" (daemon %s)", pk_error_enum_to_string ((PkErrorEnum) (error->code - 0xff)));
  g_print (": %s\n", error->message);
}

static void
on_progress (PkProgress *progress, PkProgressType type, void *data G_GNUC_UNUSED)
{
  switch (type)
    {
    case PK_PROGRESS_TYPE_STATUS:
      g_print ("progress: status %s\n",
               pk_status_enum_to_string (pk_progress_get_status (progress)));
      break;
    case PK_PROGRESS_TYPE_PERCENTAGE:
      g_print ("progress: percentage %d\n", pk_progress_get_percentage (progress));
      break;
    case PK_PROGRESS_TYPE_PACKAGE_ID:
      g_print ("progress: package %s\n", pk_progress_get_package_id (progress));
      break;
    case PK_PROGRESS_TYPE_ALLOW_CANCEL:
      g_print ("progress: allow-cancel %d\n", pk_progress_get_allow_cancel (progress));
      break;
    case PK_PROGRESS_TYPE_PACKAGE:
    case PK_PROGRESS_TYPE_ROLE:
    case PK_PROGRESS_TYPE_CALLER_ACTIVE:
    case PK_PROGRESS_TYPE_ELAPSED_TIME:
    case PK_PROGRESS_TYPE_REMAINING_TIME:
    case PK_PROGRESS_TYPE_SPEED:
    case PK_PROGRESS_TYPE_DOWNLOAD_SIZE_REMAINING:
    case PK_PROGRESS_TYPE_UID:
    case PK_PROGRESS_TYPE_TRANSACTION_ID:
    case PK_PROGRESS_TYPE_TRANSACTION_FLAGS:
    case PK_PROGRESS_TYPE_ITEM_PROGRESS:
    case PK_PROGRESS_TYPE_SENDER:
    case PK_PROGRESS_TYPE_INVALID: break;
    }
}

static void
on_finished (GObject *source, GAsyncResult *result, GObject *owner G_GNUC_UNUSED, void *data)
{
  Drive *d = data;
  g_autoptr (GError) error = nullptr;
  g_autoptr (PkResults) results = pk_client_generic_finish (PK_CLIENT (source), result, &error);
  if (results == nullptr)
    {
      print_error ("transaction", error);
      d->status = 1;
    }
  else
    print_results (results);
  d->done = true;
}

static void
on_candidates (GObject *source, GAsyncResult *result, GObject *owner, void *data)
{
  Drive *d = data;
  g_autoptr (GError) error = nullptr;
  g_autoptr (PkResults) results = pk_client_generic_finish (PK_CLIENT (source), result, &error);
  if (results == nullptr)
    {
      print_error ("resolve", error);
      d->status = 1;
      d->done = true;
      return;
    }
  print_results (results);
  g_autoptr (GPtrArray) packages = pk_results_get_package_array (results);
  g_autoptr (GPtrArray) ids = g_ptr_array_new_with_free_func (g_free);
  for (guint i = 0; i < packages->len; i++)
    g_ptr_array_add (ids, g_strdup (pk_package_get_id (g_ptr_array_index (packages, i))));
  if (ids->len == 0)
    {
      g_print ("nothing to %s\n", d->simulate ? "simulate" : "install");
      d->done = true;
      return;
    }
  g_ptr_array_add (ids, nullptr);
  PkBitfield flags = d->simulate ? pk_bitfield_from_enums (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED,
                                                           PK_TRANSACTION_FLAG_ENUM_SIMULATE, -1)
                                 : pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
  pk_client_set_interactive (d->client, true);
  g_print ("== %s\n", d->simulate ? "simulate" : "install");
  ChandlerAsync *a = chandler_async_new (owner, on_finished, d, nullptr);
  pk_client_install_packages_async (d->client, flags, (char **) ids->pdata,
                                    chandler_async_cancellable (a), on_progress, nullptr,
                                    chandler_async_ready, a);
}

int
main (int argc, char **argv)
{
  if (argc < 3)
    {
      fputs ("usage: pk-drive resolve FILTERS NAME... | simulate NAME... | install NAME...\n",
             stderr);
      return 2;
    }
  g_autoptr (GObject) owner = g_object_new (G_TYPE_OBJECT, nullptr);
  Drive d = { .client = pk_client_new () };
  pk_client_set_interactive (d.client, false);
  ChandlerAsync *a;
  if (g_str_equal (argv[1], "resolve") && argc >= 4)
    {
      PkBitfield filters = pk_filter_bitfield_from_string (argv[2]);
      g_print ("== resolve %s\n", argv[2]);
      a = chandler_async_new (owner, on_finished, &d, nullptr);
      pk_client_resolve_async (d.client, filters, argv + 3, chandler_async_cancellable (a),
                               on_progress, nullptr, chandler_async_ready, a);
    }
  else if (g_str_equal (argv[1], "simulate") || g_str_equal (argv[1], "install"))
    {
      d.simulate = g_str_equal (argv[1], "simulate");
      PkBitfield filters
          = pk_bitfield_from_enums (PK_FILTER_ENUM_NOT_INSTALLED, PK_FILTER_ENUM_ARCH,
                                    PK_FILTER_ENUM_NEWEST, PK_FILTER_ENUM_NOT_SOURCE, -1);
      g_print ("== resolve candidates\n");
      a = chandler_async_new (owner, on_candidates, &d, nullptr);
      pk_client_resolve_async (d.client, filters, argv + 2, chandler_async_cancellable (a),
                               on_progress, nullptr, chandler_async_ready, a);
    }
  else
    {
      fputs ("pk-drive: unknown command\n", stderr);
      g_object_unref (d.client);
      return 2;
    }
  while (!d.done)
    g_main_context_iteration (nullptr, true);
  g_object_unref (d.client);
  return d.status;
}

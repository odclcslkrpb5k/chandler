#include "packages.h"
#include "async.h"

/* packagekit-glib2 hands daemon errors over in PK_CLIENT_ERROR at this offset (pk-client.h:
 * "we define this so we can punt the PkErrorEnums here at offset 0xff"). */
#define DAEMON_ERROR_OFFSET 0xff

struct _ChandlerPackageId
{
  char *value; /* "name;version;arch;data", exactly as PackageKit sent it */
};

typedef struct
{
  ChandlerPkgState state;
  ChandlerPackageId *id; /* AVAILABLE only */
} Entry;

struct _ChandlerResolved
{
  GHashTable *entries; /* name -> Entry */
};

struct _ChandlerPackages
{
  GObject parent;
  PkClient *client;
  PkControl *control;
};

G_DEFINE_FINAL_TYPE (ChandlerPackages, chandler_packages, G_TYPE_OBJECT)

static void
chandler_packages_finalize (GObject *object)
{
  ChandlerPackages *self = CHANDLER_PACKAGES (object);
  g_clear_object (&self->client);
  g_clear_object (&self->control);
  G_OBJECT_CLASS (chandler_packages_parent_class)->finalize (object);
}

static void
chandler_packages_class_init (ChandlerPackagesClass *klass)
{
  G_OBJECT_CLASS (klass)->finalize = chandler_packages_finalize;
}

static void
chandler_packages_init (ChandlerPackages *self)
{
  self->client = pk_client_new ();
  self->control = pk_control_new ();
  /* Resolving never needs a password; installing (milestone 4) sets this per call. */
  pk_client_set_interactive (self->client, false);
  pk_client_set_background (self->client, false);
}

const char *
chandler_pkg_state_name (ChandlerPkgState state)
{
  switch (state)
    {
    case CHANDLER_PKG_INSTALLED: return "installed";
    case CHANDLER_PKG_AVAILABLE: return "available";
    case CHANDLER_PKG_UNAVAILABLE: return "unavailable";
    case CHANDLER_PKG_AMBIGUOUS: return "ambiguous";
    }
  gbk_fatal ("unknown package state %d", (int) state);
}

static void
entry_free (Entry *e)
{
  if (e->id != nullptr)
    {
      g_free (e->id->value);
      g_free (e->id);
    }
  g_free (e);
}

void
chandler_resolved_free (ChandlerResolved *resolved)
{
  if (resolved == nullptr)
    return;
  g_hash_table_unref (resolved->entries);
  g_free (resolved);
}

ChandlerPkgState
chandler_resolved_state (const ChandlerResolved *resolved, const char *name)
{
  const Entry *e = g_hash_table_lookup (resolved->entries, name);
  return e != nullptr ? e->state : CHANDLER_PKG_UNAVAILABLE;
}

ChandlerPkgState
chandler_resolved_app_state (const ChandlerResolved *resolved, const char *const *pkgnames)
{
  bool all_installed = true;
  for (const char *const *p = pkgnames; *p != nullptr; p++)
    {
      ChandlerPkgState s = chandler_resolved_state (resolved, *p);
      switch (s)
        {
        case CHANDLER_PKG_INSTALLED: break;
        case CHANDLER_PKG_AVAILABLE: all_installed = false; break;
        case CHANDLER_PKG_UNAVAILABLE:
        case CHANDLER_PKG_AMBIGUOUS: return s;
        }
    }
  return all_installed ? CHANDLER_PKG_INSTALLED : CHANDLER_PKG_AVAILABLE;
}

/* --- Errors -> outcomes: codes only (ER-8). Every daemon code is listed, so a code added
 * to PackageKit fails the build here (-Wswitch-enum) instead of falling into OTHER. --- */

static ChandlerOutcome
daemon_outcome (PkErrorEnum code)
{
  switch (code)
    {
    case PK_ERROR_ENUM_TRANSACTION_CANCELLED:
    case PK_ERROR_ENUM_CANCELLED_PRIORITY: return CHANDLER_OUTCOME_CANCELLED;
    case PK_ERROR_ENUM_NOT_AUTHORIZED: return CHANDLER_OUTCOME_AUTH_INCOMPLETE;
    case PK_ERROR_ENUM_NO_NETWORK:
    case PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED:
    case PK_ERROR_ENUM_NO_MORE_MIRRORS_TO_TRY:
    case PK_ERROR_ENUM_REPO_NOT_AVAILABLE:
    case PK_ERROR_ENUM_CANNOT_FETCH_SOURCES:
    case PK_ERROR_ENUM_RESTRICTED_DOWNLOAD: return CHANDLER_OUTCOME_NO_NETWORK;
    case PK_ERROR_ENUM_CANNOT_GET_LOCK:
    case PK_ERROR_ENUM_LOCK_REQUIRED: return CHANDLER_OUTCOME_LOCKED;
    case PK_ERROR_ENUM_PACKAGE_NOT_FOUND:
    case PK_ERROR_ENUM_PACKAGE_ID_INVALID:
    case PK_ERROR_ENUM_PACKAGE_DATABASE_CHANGED:
    case PK_ERROR_ENUM_NO_CACHE: return CHANDLER_OUTCOME_GONE;
    case PK_ERROR_ENUM_PACKAGE_ALREADY_INSTALLED:
    case PK_ERROR_ENUM_ALL_PACKAGES_ALREADY_INSTALLED: return CHANDLER_OUTCOME_ALREADY;
    case PK_ERROR_ENUM_DEP_RESOLUTION_FAILED:
    case PK_ERROR_ENUM_TRANSACTION_ERROR:
    case PK_ERROR_ENUM_FILE_CONFLICTS:
    case PK_ERROR_ENUM_PACKAGE_CONFLICTS:
    case PK_ERROR_ENUM_PACKAGE_CORRUPT:
    case PK_ERROR_ENUM_PACKAGE_INSTALL_BLOCKED:
    case PK_ERROR_ENUM_PACKAGE_FAILED_TO_CONFIGURE:
    case PK_ERROR_ENUM_PACKAGE_FAILED_TO_INSTALL:
    case PK_ERROR_ENUM_UNFINISHED_TRANSACTION:
    case PK_ERROR_ENUM_INCOMPATIBLE_ARCHITECTURE:
    case PK_ERROR_ENUM_REPO_CONFIGURATION_ERROR: return CHANDLER_OUTCOME_BROKEN;
    case PK_ERROR_ENUM_NO_SPACE_ON_DEVICE: return CHANDLER_OUTCOME_NO_SPACE;
    case PK_ERROR_ENUM_GPG_FAILURE:
    case PK_ERROR_ENUM_BAD_GPG_SIGNATURE:
    case PK_ERROR_ENUM_MISSING_GPG_SIGNATURE:
    case PK_ERROR_ENUM_CANNOT_INSTALL_REPO_UNSIGNED: return CHANDLER_OUTCOME_UNTRUSTED;
    case PK_ERROR_ENUM_CANNOT_CANCEL: return CHANDLER_OUTCOME_CANNOT_CANCEL;
    case PK_ERROR_ENUM_UNKNOWN:
    case PK_ERROR_ENUM_OOM:
    case PK_ERROR_ENUM_NOT_SUPPORTED:
    case PK_ERROR_ENUM_INTERNAL_ERROR:
    case PK_ERROR_ENUM_PACKAGE_NOT_INSTALLED:
    case PK_ERROR_ENUM_GROUP_NOT_FOUND:
    case PK_ERROR_ENUM_GROUP_LIST_INVALID:
    case PK_ERROR_ENUM_FILTER_INVALID:
    case PK_ERROR_ENUM_CREATE_THREAD_FAILED:
    case PK_ERROR_ENUM_REPO_NOT_FOUND:
    case PK_ERROR_ENUM_CANNOT_REMOVE_SYSTEM_PACKAGE:
    case PK_ERROR_ENUM_PROCESS_KILL:
    case PK_ERROR_ENUM_FAILED_INITIALIZATION:
    case PK_ERROR_ENUM_FAILED_FINALISE:
    case PK_ERROR_ENUM_FAILED_CONFIG_PARSING:
    case PK_ERROR_ENUM_NO_PACKAGES_TO_UPDATE:
    case PK_ERROR_ENUM_CANNOT_WRITE_REPO_CONFIG:
    case PK_ERROR_ENUM_LOCAL_INSTALL_FAILED:
    case PK_ERROR_ENUM_CANNOT_INSTALL_SOURCE_PACKAGE:
    case PK_ERROR_ENUM_NO_LICENSE_AGREEMENT:
    case PK_ERROR_ENUM_INVALID_PACKAGE_FILE:
    case PK_ERROR_ENUM_FILE_NOT_FOUND:
    case PK_ERROR_ENUM_NO_DISTRO_UPGRADE_DATA:
    case PK_ERROR_ENUM_MEDIA_CHANGE_REQUIRED:
    case PK_ERROR_ENUM_UPDATE_NOT_FOUND:
    case PK_ERROR_ENUM_CANNOT_UPDATE_REPO_UNSIGNED:
    case PK_ERROR_ENUM_CANNOT_GET_FILELIST:
    case PK_ERROR_ENUM_CANNOT_GET_REQUIRES:
    case PK_ERROR_ENUM_CANNOT_DISABLE_REPOSITORY:
    case PK_ERROR_ENUM_PACKAGE_FAILED_TO_BUILD:
    case PK_ERROR_ENUM_PACKAGE_FAILED_TO_REMOVE:
    case PK_ERROR_ENUM_UPDATE_FAILED_DUE_TO_RUNNING_PROCESS:
    case PK_ERROR_ENUM_PROVIDE_TYPE_NOT_SUPPORTED:
    case PK_ERROR_ENUM_INSTALL_ROOT_INVALID:
    case PK_ERROR_ENUM_REPO_ALREADY_SET:
    case PK_ERROR_ENUM_LAST: return CHANDLER_OUTCOME_OTHER;
    }
  return CHANDLER_OUTCOME_OTHER; /* a number outside the enum, from the daemon */
}

static ChandlerOutcome
client_outcome (PkClientError code)
{
  switch (code)
    {
    case PK_CLIENT_ERROR_FAILED_AUTH:
    case PK_CLIENT_ERROR_DECLINED_INTERACTION: return CHANDLER_OUTCOME_AUTH_INCOMPLETE;
    case PK_CLIENT_ERROR_CANNOT_START_DAEMON: return CHANDLER_OUTCOME_NO_DAEMON;
    case PK_CLIENT_ERROR_DECLINED_SIMULATION: return CHANDLER_OUTCOME_CANCELLED;
    case PK_CLIENT_ERROR_FAILED:
    case PK_CLIENT_ERROR_NO_TID:
    case PK_CLIENT_ERROR_ALREADY_TID:
    case PK_CLIENT_ERROR_ROLE_UNKNOWN:
    case PK_CLIENT_ERROR_INVALID_INPUT:
    case PK_CLIENT_ERROR_INVALID_FILE:
    case PK_CLIENT_ERROR_NOT_SUPPORTED:
    case PK_CLIENT_ERROR_LAST: return CHANDLER_OUTCOME_OTHER;
    }
  return CHANDLER_OUTCOME_OTHER;
}

static ChandlerOutcome
outcome_of (const GError *error)
{
  if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    return CHANDLER_OUTCOME_CANCELLED;
  if (error->domain == PK_CLIENT_ERROR)
    return error->code >= DAEMON_ERROR_OFFSET
               ? daemon_outcome ((PkErrorEnum) (error->code - DAEMON_ERROR_OFFSET))
               : client_outcome ((PkClientError) error->code);
  if (error->domain == G_DBUS_ERROR || error->domain == G_IO_ERROR)
    return CHANDLER_OUTCOME_NO_DAEMON;
  return CHANDLER_OUTCOME_OTHER;
}

ChandlerOutcome
chandler_packages_outcome_for_test (const GError *error)
{
  return outcome_of (error);
}

ChandlerOutcome
chandler_packages_daemon_outcome_for_test (int code)
{
  return daemon_outcome ((PkErrorEnum) code);
}

/* --- The daemon's presence --- */

typedef struct
{
  ChandlerPackages *packages; /* owned */
  ChandlerPackagesFunc done;
  void *data;
} Open;

static void
open_free (Open *open)
{
  g_clear_object (&open->packages);
  g_free (open);
}

static void
on_properties (GObject *source, GAsyncResult *result, GObject *owner, void *data)
{
  const Open *open = data;
  g_autoptr (GError) error = nullptr;
  bool ok = pk_control_get_properties_finish (PK_CONTROL (source), result, &error);
  if (owner == nullptr)
    return;
  if (!ok)
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        return;
      g_debug ("PackageKit unavailable: %s %d", g_quark_to_string (error->domain), error->code);
    }
  open->done (open->packages, ok ? CHANDLER_OUTCOME_OK : CHANDLER_OUTCOME_NO_DAEMON, owner,
              open->data);
}

void
chandler_packages_open (GObject *owner, ChandlerPackagesFunc done, void *data)
{
  Open *open = g_new0 (Open, 1);
  open->packages = g_object_new (CHANDLER_TYPE_PACKAGES, nullptr);
  open->done = done;
  open->data = data;
  ChandlerAsync *a = chandler_async_new (owner, on_properties, open, (GDestroyNotify) open_free);
  pk_control_get_properties_async (open->packages->control, chandler_async_cancellable (a),
                                   chandler_async_ready, a);
}

/* --- Resolve: what is installed, then the candidates for the rest --- */

/* Shared by both transactions, each holding a reference (g_rc_box), so ownership needs no
 * bookkeeping of which step has it. */
typedef struct
{
  ChandlerPackages *packages; /* owned */
  char **names;               /* each once */
  ChandlerResolved *resolved;
  ChandlerResolveFunc done;
  void *data;
} Resolve;

static void
resolve_clear (Resolve *r)
{
  g_clear_object (&r->packages);
  g_strfreev (r->names);
  chandler_resolved_free (r->resolved);
}

static void
resolve_release (Resolve *r)
{
  g_rc_box_release_full (r, (GDestroyNotify) resolve_clear);
}

static void
finish (const Resolve *r, ChandlerOutcome outcome, GObject *owner)
{
  r->done (outcome == CHANDLER_OUTCOME_OK ? r->resolved : nullptr, outcome, owner, r->data);
}

static bool
asked (const Resolve *r, const char *name)
{
  return name != nullptr && g_strv_contains ((const char *const *) r->names, name);
}

/* The transaction's own verdict: a reply is not success unless PackageKit says it finished
 * successfully. A killed or pre-empted transaction can return a partial package list with
 * no GError (plan §3.4), which must not read as "these packages don't exist". */
static ChandlerOutcome
results_outcome (PkResults *results, const GError *error)
{
  if (results == nullptr)
    return outcome_of (error);
  g_autoptr (PkError) failed = pk_results_get_error_code (results);
  if (failed != nullptr)
    return daemon_outcome (pk_error_get_code (failed));
  PkExitEnum exit = pk_results_get_exit_code (results);
  switch (exit)
    {
    case PK_EXIT_ENUM_SUCCESS: return CHANDLER_OUTCOME_OK;
    case PK_EXIT_ENUM_CANCELLED:
    case PK_EXIT_ENUM_CANCELLED_PRIORITY: return CHANDLER_OUTCOME_CANCELLED;
    case PK_EXIT_ENUM_UNKNOWN:
    case PK_EXIT_ENUM_FAILED:
    case PK_EXIT_ENUM_KEY_REQUIRED:
    case PK_EXIT_ENUM_EULA_REQUIRED:
    case PK_EXIT_ENUM_KILLED:
    case PK_EXIT_ENUM_MEDIA_CHANGE_REQUIRED:
    case PK_EXIT_ENUM_NEED_UNTRUSTED:
    case PK_EXIT_ENUM_SKIP_TRANSACTION:
    case PK_EXIT_ENUM_REPAIR_REQUIRED:
    case PK_EXIT_ENUM_LAST: return CHANDLER_OUTCOME_OTHER;
    }
  return CHANDLER_OUTCOME_OTHER;
}

static void
on_candidates (GObject *source, GAsyncResult *result, GObject *owner, void *data)
{
  Resolve *r = data;
  g_autoptr (GError) error = nullptr;
  g_autoptr (PkResults) results = pk_client_generic_finish (PK_CLIENT (source), result, &error);
  if (owner == nullptr)
    return;
  ChandlerOutcome outcome = results_outcome (results, error);
  if (outcome != CHANDLER_OUTCOME_OK)
    {
      finish (r, outcome, owner);
      return;
    }
  g_autoptr (GPtrArray) found = pk_results_get_package_array (results);
  for (guint i = 0; i < found->len; i++)
    {
      PkPackage *pkg = g_ptr_array_index (found, i);
      const char *name = pk_package_get_name (pkg);
      const char *id = pk_package_get_id (pkg);
      if (!asked (r, name) || id == nullptr || pk_package_get_info (pkg) != PK_INFO_ENUM_AVAILABLE)
        continue;
      Entry *e = g_hash_table_lookup (r->resolved->entries, name);
      if (e == nullptr)
        {
          e = g_new0 (Entry, 1);
          e->state = CHANDLER_PKG_AVAILABLE;
          e->id = g_new0 (ChandlerPackageId, 1);
          e->id->value = g_strdup (id);
          g_hash_table_insert (r->resolved->entries, g_strdup (name), e);
        }
      else if (e->state == CHANDLER_PKG_AVAILABLE && !g_str_equal (e->id->value, id))
        {
          /* A second, different candidate (another architecture, another version): refuse
           * to guess. The same id twice is one candidate. */
          e->state = CHANDLER_PKG_AMBIGUOUS;
          g_free (e->id->value);
          g_clear_pointer (&e->id, g_free);
        }
    }
  finish (r, CHANDLER_OUTCOME_OK, owner);
}

static void
on_installed (GObject *source, GAsyncResult *result, GObject *owner, void *data)
{
  Resolve *r = data;
  g_autoptr (GError) error = nullptr;
  g_autoptr (PkResults) results = pk_client_generic_finish (PK_CLIENT (source), result, &error);
  if (owner == nullptr)
    return;
  ChandlerOutcome outcome = results_outcome (results, error);
  if (outcome != CHANDLER_OUTCOME_OK)
    {
      finish (r, outcome, owner);
      return;
    }
  g_autoptr (GPtrArray) found = pk_results_get_package_array (results);
  g_autoptr (GPtrArray) rest = g_ptr_array_new_with_free_func (g_free);
  for (guint i = 0; i < found->len; i++)
    {
      PkPackage *pkg = g_ptr_array_index (found, i);
      const char *name = pk_package_get_name (pkg);
      if (!asked (r, name) || g_hash_table_contains (r->resolved->entries, name)
          || pk_package_get_info (pkg) != PK_INFO_ENUM_INSTALLED)
        continue;
      Entry *e = g_new0 (Entry, 1);
      e->state = CHANDLER_PKG_INSTALLED;
      g_hash_table_insert (r->resolved->entries, g_strdup (name), e);
    }
  for (char **n = r->names; *n != nullptr; n++)
    if (!g_hash_table_contains (r->resolved->entries, *n))
      g_ptr_array_add (rest, g_strdup (*n));
  if (rest->len == 0)
    {
      finish (r, CHANDLER_OUTCOME_OK, owner);
      return;
    }
  g_ptr_array_add (rest, nullptr);
  /* Native architecture, newest version, binary packages: one candidate per name. */
  PkBitfield filters
      = pk_bitfield_from_enums (PK_FILTER_ENUM_NOT_INSTALLED, PK_FILTER_ENUM_ARCH,
                                PK_FILTER_ENUM_NEWEST, PK_FILTER_ENUM_NOT_SOURCE, -1);
  ChandlerAsync *a = chandler_async_new (owner, on_candidates, g_rc_box_acquire (r),
                                         (GDestroyNotify) resolve_release);
  pk_client_resolve_async (r->packages->client, filters, (char **) rest->pdata,
                           chandler_async_cancellable (a), nullptr, nullptr, chandler_async_ready,
                           a);
}

void
chandler_packages_resolve (ChandlerPackages *packages, GObject *owner, const char *const *names,
                           ChandlerResolveFunc done, void *data)
{
  Resolve *r = g_rc_box_new0 (Resolve);
  r->packages = g_object_ref (packages);
  /* Each name once: apps share packages, and a name asked twice may be answered twice. */
  GPtrArray *copy = g_ptr_array_new ();
  for (const char *const *n = names; *n != nullptr; n++)
    {
      bool seen = false;
      for (guint i = 0; i < copy->len && !seen; i++)
        seen = g_str_equal (g_ptr_array_index (copy, i), *n);
      if (!seen)
        g_ptr_array_add (copy, g_strdup (*n));
    }
  g_ptr_array_add (copy, nullptr);
  r->names = (char **) g_ptr_array_free (copy, false);
  r->resolved = g_new0 (ChandlerResolved, 1);
  r->resolved->entries
      = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) entry_free);
  r->done = done;
  r->data = data;
  PkBitfield filters = pk_bitfield_from_enums (PK_FILTER_ENUM_INSTALLED, PK_FILTER_ENUM_ARCH,
                                               PK_FILTER_ENUM_NOT_SOURCE, -1);
  ChandlerAsync *a = chandler_async_new (owner, on_installed, r, (GDestroyNotify) resolve_release);
  pk_client_resolve_async (packages->client, filters, r->names, chandler_async_cancellable (a),
                           nullptr, nullptr, chandler_async_ready, a);
}

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
  PkClient *client;    /* asks: never a password prompt */
  PkClient *installer; /* installs: may prompt */
  PkControl *control;
};

G_DEFINE_FINAL_TYPE (ChandlerPackages, chandler_packages, G_TYPE_OBJECT)

static void
chandler_packages_finalize (GObject *object)
{
  ChandlerPackages *self = CHANDLER_PACKAGES (object);
  g_clear_object (&self->client);
  g_clear_object (&self->installer);
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
  self->control = pk_control_new ();
  /* Resolving and simulating never need a password: a prompt for them would be a bug. */
  self->client = pk_client_new ();
  pk_client_set_interactive (self->client, false);
  pk_client_set_background (self->client, false);
  self->installer = pk_client_new ();
  pk_client_set_interactive (self->installer, true);
  pk_client_set_background (self->installer, false);
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

/* The candidate's package ID, or nullptr unless the name is AVAILABLE. */
static const char *
candidate_id (const ChandlerResolved *resolved, const char *name)
{
  const Entry *e = g_hash_table_lookup (resolved->entries, name);
  return e != nullptr && e->id != nullptr ? e->id->value : nullptr;
}

const char *
chandler_resolved_id_for_test (const ChandlerResolved *resolved, const char *name)
{
  return candidate_id (resolved, name);
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
  /* That succeeds with no daemon on the bus at all (found by the fake's no-daemon
   * scenario): PkControl answers from defaults. A daemon that answered has a version. */
  gboolean connected = false;
  guint major = 0;
  if (ok)
    g_object_get (source, "connected", &connected, "version-major", &major, nullptr);
  ok = ok && connected && major > 0;
  if (owner == nullptr)
    return;
  if (error != nullptr)
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        return;
      g_debug ("PackageKit unavailable: %s %d", g_quark_to_string (error->domain), error->code);
    }
  else if (!ok)
    g_debug ("PackageKit unavailable: no daemon answered");
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
  GDestroyNotify destroy; /* of data; may be nullptr */
} Resolve;

static void
resolve_clear (Resolve *r)
{
  g_clear_object (&r->packages);
  g_strfreev (r->names);
  chandler_resolved_free (r->resolved);
  if (r->destroy != nullptr)
    r->destroy (r->data);
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

/* The ARCH filter is the daemon's to apply; this is ours. A foreign candidate is dropped, not
 * refused: next to a native one it is noise, and alone it is not installable here. */
static bool
native (PkPackage *pkg)
{
  const char *arch = pk_package_get_arch (pkg);
  return arch != nullptr && (g_str_equal (arch, CHANDLER_NATIVE_ARCH) || g_str_equal (arch, "all"));
}

/* The transaction's own verdict: a reply is not success unless PackageKit says it finished
 * successfully. A killed or pre-empted transaction can return a partial package list with
 * no GError (plan §3.4), which must not read as "these packages don't exist". */
static ChandlerOutcome
results_outcome (PkResults *results, const GError *error)
{
  /* A daemon ErrorCode always arrives here as the GError (0xff + code), never inside the
   * results: packagekit-glib2 1.3.4 returns no results then (recording 02), so the
   * results' own error code is never set. */
  if (results == nullptr)
    return outcome_of (error);
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
      if (!asked (r, name) || id == nullptr || pk_package_get_info (pkg) != PK_INFO_ENUM_AVAILABLE
          || !native (pkg))
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
          || pk_package_get_info (pkg) != PK_INFO_ENUM_INSTALLED || !native (pkg))
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

static void
resolve_start (ChandlerPackages *packages, GObject *owner, const char *const *names,
               ChandlerResolveFunc done, void *data, GDestroyNotify destroy)
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
  r->destroy = destroy;
  PkBitfield filters = pk_bitfield_from_enums (PK_FILTER_ENUM_INSTALLED, PK_FILTER_ENUM_ARCH,
                                               PK_FILTER_ENUM_NOT_SOURCE, -1);
  ChandlerAsync *a = chandler_async_new (owner, on_installed, r, (GDestroyNotify) resolve_release);
  pk_client_resolve_async (packages->client, filters, r->names, chandler_async_cancellable (a),
                           nullptr, nullptr, chandler_async_ready, a);
}

void
chandler_packages_resolve (ChandlerPackages *packages, GObject *owner, const char *const *names,
                           ChandlerResolveFunc done, void *data)
{
  resolve_start (packages, owner, names, done, data, nullptr);
}

/* --- Checking an install: resolve again, then simulate (plan §3.3) --- */

struct _ChandlerPlan
{
  char **ids; /* the chosen package IDs, each minted by a resolve */
  guint extra;
  bool updates;
  char *changes; /* everything the simulation does, one "<info> <build>" per line, sorted */
};

void
chandler_plan_free (ChandlerPlan *plan)
{
  if (plan == nullptr)
    return;
  g_strfreev (plan->ids);
  g_free (plan->changes);
  g_free (plan);
}

guint
chandler_plan_extra (const ChandlerPlan *plan)
{
  return plan->extra;
}

bool
chandler_plan_updates (const ChandlerPlan *plan)
{
  return plan->updates;
}

bool
chandler_plan_same (const ChandlerPlan *a, const ChandlerPlan *b)
{
  return g_str_equal (a->changes, b->changes);
}

/* PackageKit's own text, for OTHER only: the one outcome whose words can't say more. */
static const char *
detail_of (ChandlerOutcome outcome, const GError *error)
{
  return outcome == CHANDLER_OUTCOME_OTHER && error != nullptr ? error->message : nullptr;
}

/* "name;version;arch": the part of a package ID that names one build. The data field differs
 * between resolve ("ubuntu-resolute-main") and simulate ("+manual:ubuntu-resolute-main"),
 * so IDs are compared by this (recording 01). nullptr for a malformed ID. */
static char *
build_of (const char *id)
{
  if (id == nullptr)
    return nullptr;
  g_auto (GStrv) parts = pk_package_id_split (id);
  if (parts == nullptr)
    return nullptr;
  return g_strdup_printf ("%s;%s;%s", parts[PK_PACKAGE_ID_NAME], parts[PK_PACKAGE_ID_VERSION],
                          parts[PK_PACKAGE_ID_ARCH]);
}

typedef enum
{
  CHANGE_INSTALL,
  CHANGE_UPDATE,
  CHANGE_UNTRUSTED,
  CHANGE_OTHER,
} Change;

/* What one package of the simulation would undergo. This is the allowlist (plan §3.3):
 * installing and updating pass; removing, obsoleting, downgrading, reinstalling, anything
 * untrusted or unknown refuses the install. Every value is listed, so one added to
 * PackageKit fails the build here (-Wswitch-enum) instead of passing. */
static Change
change_of (PkInfoEnum info)
{
  switch (info)
    {
    case PK_INFO_ENUM_INSTALLING:
    case PK_INFO_ENUM_INSTALL: return CHANGE_INSTALL;
    case PK_INFO_ENUM_UPDATING: return CHANGE_UPDATE;
    case PK_INFO_ENUM_UNTRUSTED: return CHANGE_UNTRUSTED;
    case PK_INFO_ENUM_UNKNOWN:
    case PK_INFO_ENUM_INSTALLED:
    case PK_INFO_ENUM_AVAILABLE:
    case PK_INFO_ENUM_LOW:
    case PK_INFO_ENUM_ENHANCEMENT:
    case PK_INFO_ENUM_NORMAL:
    case PK_INFO_ENUM_BUGFIX:
    case PK_INFO_ENUM_IMPORTANT:
    case PK_INFO_ENUM_SECURITY:
    case PK_INFO_ENUM_BLOCKED:
    case PK_INFO_ENUM_DOWNLOADING:
    case PK_INFO_ENUM_REMOVING:
    case PK_INFO_ENUM_CLEANUP:
    case PK_INFO_ENUM_OBSOLETING:
    case PK_INFO_ENUM_COLLECTION_INSTALLED:
    case PK_INFO_ENUM_COLLECTION_AVAILABLE:
    case PK_INFO_ENUM_FINISHED:
    case PK_INFO_ENUM_REINSTALLING:
    case PK_INFO_ENUM_DOWNGRADING:
    case PK_INFO_ENUM_PREPARING:
    case PK_INFO_ENUM_DECOMPRESSING:
    case PK_INFO_ENUM_TRUSTED:
    case PK_INFO_ENUM_UNAVAILABLE:
    case PK_INFO_ENUM_CRITICAL:
    case PK_INFO_ENUM_REMOVE:
    case PK_INFO_ENUM_OBSOLETE:
    case PK_INFO_ENUM_DOWNGRADE:
    case PK_INFO_ENUM_LAST: return CHANGE_OTHER;
    }
  return CHANGE_OTHER; /* a number outside the enum, from the daemon */
}

/* The words for a refused change, in the list the user is shown. */
static const char *
refused_verb (PkInfoEnum info)
{
  if (info == PK_INFO_ENUM_REMOVING || info == PK_INFO_ENUM_REMOVE)
    return "remove";
  if (info == PK_INFO_ENUM_OBSOLETING || info == PK_INFO_ENUM_OBSOLETE)
    return "replace";
  if (info == PK_INFO_ENUM_DOWNGRADING || info == PK_INFO_ENUM_DOWNGRADE)
    return "downgrade";
  if (info == PK_INFO_ENUM_REINSTALLING)
    return "reinstall";
  if (info == PK_INFO_ENUM_UNTRUSTED)
    return "install unsigned";
  return "change";
}

typedef struct
{
  ChandlerPackages *packages; /* owned */
  char **names;
  char **ids; /* the chosen ones, once resolved */
  ChandlerCheckFunc done;
  void *data;
} Check;

static void
check_clear (Check *c)
{
  g_clear_object (&c->packages);
  g_strfreev (c->names);
  g_strfreev (c->ids);
}

static void
check_release (Check *c)
{
  g_rc_box_release_full (c, (GDestroyNotify) check_clear);
}

static void
on_simulated (GObject *source, GAsyncResult *result, GObject *owner, void *data)
{
  const Check *c = data;
  g_autoptr (GError) error = nullptr;
  g_autoptr (PkResults) results = pk_client_generic_finish (PK_CLIENT (source), result, &error);
  if (owner == nullptr)
    return;
  ChandlerOutcome outcome = results_outcome (results, error);
  if (outcome != CHANDLER_OUTCOME_OK)
    {
      c->done (nullptr, outcome, detail_of (outcome, error), owner, c->data);
      return;
    }
  /* build -> whether the simulation installs it */
  g_autoptr (GHashTable) chosen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, nullptr);
  for (char **id = c->ids; *id != nullptr; id++)
    {
      char *build = build_of (*id);
      if (build == nullptr)
        {
          g_debug ("a resolved package ID that doesn't split");
          c->done (nullptr, CHANDLER_OUTCOME_GONE, nullptr, owner, c->data);
          return;
        }
      g_hash_table_insert (chosen, build, GINT_TO_POINTER (false));
    }
  g_autoptr (GPtrArray) changes = g_ptr_array_new_with_free_func (g_free);
  g_autoptr (GHashTable) extra = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, nullptr);
  g_autoptr (GString) refused = g_string_new (nullptr);
  bool updates = false;
  bool untrusted_only = true;
  g_autoptr (GPtrArray) found = pk_results_get_package_array (results);
  for (guint i = 0; i < found->len; i++)
    {
      PkPackage *pkg = g_ptr_array_index (found, i);
      PkInfoEnum info = pk_package_get_info (pkg);
      g_autofree char *build = build_of (pk_package_get_id (pkg));
      Change change = build != nullptr ? change_of (info) : CHANGE_OTHER;
      if (build != nullptr)
        g_ptr_array_add (changes, g_strdup_printf ("%d %s", (int) info, build));
      switch (change)
        {
        case CHANGE_INSTALL:
          if (g_hash_table_contains (chosen, build))
            g_hash_table_insert (chosen, g_steal_pointer (&build), GINT_TO_POINTER (true));
          else
            g_hash_table_add (extra, g_steal_pointer (&build));
          break;
        case CHANGE_UPDATE:
          updates = true;
          g_hash_table_add (extra, g_steal_pointer (&build));
          break;
        case CHANGE_UNTRUSTED:
        case CHANGE_OTHER:
          untrusted_only = untrusted_only && change == CHANGE_UNTRUSTED;
          {
            const char *name = pk_package_get_name (pkg);
            g_string_append_printf (refused, "%s%s %s", refused->len > 0 ? ", " : "",
                                    refused_verb (info), name != nullptr ? name : "?");
          }
          break;
        }
    }
  if (refused->len > 0)
    {
      c->done (nullptr, untrusted_only ? CHANDLER_OUTCOME_UNTRUSTED : CHANDLER_OUTCOME_WOULD_CHANGE,
               refused->str, owner, c->data);
      return;
    }
  GHashTableIter iter;
  void *installs = nullptr;
  g_hash_table_iter_init (&iter, chosen);
  while (g_hash_table_iter_next (&iter, nullptr, &installs))
    if (!GPOINTER_TO_INT (installs))
      {
        /* The simulation doesn't install what was chosen: the package lists have moved
         * since the resolve. */
        g_debug ("the simulation leaves out a chosen package");
        c->done (nullptr, CHANDLER_OUTCOME_GONE, nullptr, owner, c->data);
        return;
      }
  ChandlerPlan *plan = g_new0 (ChandlerPlan, 1);
  plan->ids = g_strdupv (c->ids);
  plan->extra = g_hash_table_size (extra);
  plan->updates = updates;
  g_ptr_array_sort_values (changes, (GCompareFunc) g_strcmp0);
  g_ptr_array_add (changes, nullptr);
  plan->changes = g_strjoinv ("\n", (char **) changes->pdata);
  c->done (plan, CHANDLER_OUTCOME_OK, nullptr, owner, c->data);
}

static void
on_check_resolved (const ChandlerResolved *resolved, ChandlerOutcome outcome, GObject *owner,
                   void *data)
{
  Check *c = data;
  if (resolved == nullptr)
    {
      c->done (nullptr, outcome, nullptr, owner, c->data);
      return;
    }
  g_autoptr (GStrvBuilder) ids = g_strv_builder_new ();
  bool any = false;
  for (char **n = c->names; *n != nullptr; n++)
    {
      ChandlerPkgState state = chandler_resolved_state (resolved, *n);
      switch (state)
        {
        case CHANDLER_PKG_INSTALLED: break;
        case CHANDLER_PKG_AVAILABLE:
          g_strv_builder_add (ids, candidate_id (resolved, *n));
          any = true;
          break;
        case CHANDLER_PKG_UNAVAILABLE:
        case CHANDLER_PKG_AMBIGUOUS:
          c->done (nullptr, CHANDLER_OUTCOME_GONE, nullptr, owner, c->data);
          return;
        }
    }
  if (!any)
    {
      c->done (nullptr, CHANDLER_OUTCOME_ALREADY, nullptr, owner, c->data);
      return;
    }
  c->ids = g_strv_builder_end (ids);
  PkBitfield flags = pk_bitfield_from_enums (PK_TRANSACTION_FLAG_ENUM_SIMULATE,
                                             PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED, -1);
  ChandlerAsync *a = chandler_async_new (owner, on_simulated, g_rc_box_acquire (c),
                                         (GDestroyNotify) check_release);
  pk_client_install_packages_async (c->packages->client, flags, c->ids,
                                    chandler_async_cancellable (a), nullptr, nullptr,
                                    chandler_async_ready, a);
}

void
chandler_packages_check (ChandlerPackages *packages, GObject *owner, const char *const *names,
                         ChandlerCheckFunc done, void *data)
{
  Check *c = g_rc_box_new0 (Check);
  c->packages = g_object_ref (packages);
  g_autoptr (GStrvBuilder) copy = g_strv_builder_new ();
  for (const char *const *n = names; *n != nullptr; n++)
    g_strv_builder_add (copy, *n);
  c->names = g_strv_builder_end (copy);
  c->done = done;
  c->data = data;
  resolve_start (packages, owner, names, on_check_resolved, c, (GDestroyNotify) check_release);
}

/* --- Installing --- */

typedef struct
{
  ChandlerPackages *packages; /* owned */
  GWeakRef owner;
  ChandlerAsync *async; /* the install call, while it runs */
  char **ids;
  bool asked; /* WAITING_FOR_AUTH was seen */
  bool authorized;
  ChandlerStageFunc on_stage;
  ChandlerInstallFunc done;
  void *data;
  ChandlerOutcome outcome;
  char *detail;
} Install;

static void
install_clear (Install *i)
{
  g_clear_object (&i->packages);
  g_weak_ref_clear (&i->owner);
  g_strfreev (i->ids);
  g_free (i->detail);
}

static void
install_release (Install *i)
{
  g_rc_box_release_full (i, (GDestroyNotify) install_clear);
}

/* Whether a transaction in this state has been authorized: it is doing the work, so a
 * password prompt is no longer possible. Before the prompt (UNKNOWN, WAIT: queued),
 * during it, and at the end, it isn't known to be. Every value is listed (-Wswitch-enum). */
static bool
past_auth (PkStatusEnum status)
{
  switch (status)
    {
    case PK_STATUS_ENUM_UNKNOWN:
    case PK_STATUS_ENUM_WAIT:
    case PK_STATUS_ENUM_WAITING_FOR_AUTH:
    case PK_STATUS_ENUM_FINISHED:
    case PK_STATUS_ENUM_CANCEL:
    case PK_STATUS_ENUM_LAST: return false;
    case PK_STATUS_ENUM_SETUP:
    case PK_STATUS_ENUM_RUNNING:
    case PK_STATUS_ENUM_QUERY:
    case PK_STATUS_ENUM_INFO:
    case PK_STATUS_ENUM_REMOVE:
    case PK_STATUS_ENUM_REFRESH_CACHE:
    case PK_STATUS_ENUM_DOWNLOAD:
    case PK_STATUS_ENUM_INSTALL:
    case PK_STATUS_ENUM_UPDATE:
    case PK_STATUS_ENUM_CLEANUP:
    case PK_STATUS_ENUM_OBSOLETE:
    case PK_STATUS_ENUM_DEP_RESOLVE:
    case PK_STATUS_ENUM_SIG_CHECK:
    case PK_STATUS_ENUM_TEST_COMMIT:
    case PK_STATUS_ENUM_COMMIT:
    case PK_STATUS_ENUM_REQUEST:
    case PK_STATUS_ENUM_DOWNLOAD_REPOSITORY:
    case PK_STATUS_ENUM_DOWNLOAD_PACKAGELIST:
    case PK_STATUS_ENUM_DOWNLOAD_FILELIST:
    case PK_STATUS_ENUM_DOWNLOAD_CHANGELOG:
    case PK_STATUS_ENUM_DOWNLOAD_GROUP:
    case PK_STATUS_ENUM_DOWNLOAD_UPDATEINFO:
    case PK_STATUS_ENUM_REPACKAGING:
    case PK_STATUS_ENUM_LOADING_CACHE:
    case PK_STATUS_ENUM_SCAN_APPLICATIONS:
    case PK_STATUS_ENUM_GENERATE_PACKAGE_LIST:
    case PK_STATUS_ENUM_WAITING_FOR_LOCK:
    case PK_STATUS_ENUM_SCAN_PROCESS_LIST:
    case PK_STATUS_ENUM_CHECK_EXECUTABLE_FILES:
    case PK_STATUS_ENUM_CHECK_LIBRARIES:
    case PK_STATUS_ENUM_COPY_FILES:
    case PK_STATUS_ENUM_RUN_HOOK: return true;
    }
  return false;
}

static void
on_install_progress (PkProgress *progress, PkProgressType type, void *data)
{
  Install *i = data;
  /* Progress after the end (never seen: belt and braces) has no call to report to. */
  if (type != PK_PROGRESS_TYPE_STATUS || i->authorized || i->async == nullptr)
    return;
  PkStatusEnum status = pk_progress_get_status (progress);
  GObject *owner = chandler_async_owner (i->async);
  if (status == PK_STATUS_ENUM_WAITING_FOR_AUTH && !i->asked)
    {
      i->asked = true;
      if (owner != nullptr)
        i->on_stage (CHANDLER_INSTALL_ASKING, owner, i->data);
    }
  /* Queued (WAIT) after the password was asked for: authorized, waiting its turn (recording
   * 06: WAITING_FOR_AUTH, WAIT, SETUP). Dismissed goes straight to FINISHED (recording 02). */
  if (!past_auth (status) && !(i->asked && status == PK_STATUS_ENUM_WAIT))
    return;
  i->authorized = true;
  if (owner != nullptr)
    i->on_stage (CHANDLER_INSTALL_WORKING, owner, i->data);
}

/* The end: `done`, while the owner lives. */
static void
installed (Install *i)
{
  g_autoptr (GObject) owner = g_weak_ref_get (&i->owner);
  if (owner != nullptr)
    i->done (i->outcome, i->detail, owner, i->data);
}

/* polkit keeps an install's authorization for a few minutes (auth_admin_keep), for the
 * process that asked: it would let Chandler install again without a password. Revoking
 * the session's temporary authorizations ends that (plan §3.3). Best effort: a failure is
 * logged, and the install's outcome stands. */
#define LOGIND "org.freedesktop.login1"
#define POLKIT "org.freedesktop.PolicyKit1"
/* Each step's limit: a hung logind or polkit must not hold the window for D-Bus's 25 s. */
#define REVOKE_TIMEOUT_MS 5000

static void
on_revoked (GObject *source, GAsyncResult *result, GObject *owner G_GNUC_UNUSED, void *data)
{
  Install *i = data;
  g_autoptr (GError) error = nullptr;
  g_autoptr (GVariant) reply
      = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), result, &error);
  if (reply == nullptr)
    g_debug ("polkit revoke failed: %s %d", g_quark_to_string (error->domain), error->code);
  installed (i);
}

static void
on_session (GObject *source, GAsyncResult *result, GObject *owner, void *data)
{
  Install *i = data;
  g_autoptr (GError) error = nullptr;
  g_autoptr (GVariant) reply
      = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), result, &error);
  if (reply == nullptr || owner == nullptr)
    {
      if (error != nullptr)
        g_debug ("no login session: %s %d", g_quark_to_string (error->domain), error->code);
      installed (i);
      return;
    }
  g_autoptr (GVariant) value = nullptr;
  g_variant_get (reply, "(v)", &value);
  if (!g_variant_is_of_type (value, G_VARIANT_TYPE_STRING))
    {
      g_debug ("the login session's Id is not a string");
      installed (i);
      return;
    }
  GVariantBuilder details;
  g_variant_builder_init (&details, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&details, "{sv}", "session-id",
                         g_variant_new_string (g_variant_get_string (value, nullptr)));
  ChandlerAsync *a = chandler_async_new (owner, on_revoked, g_rc_box_acquire (i),
                                         (GDestroyNotify) install_release);
  g_dbus_connection_call (
      G_DBUS_CONNECTION (source), POLKIT, "/org/freedesktop/PolicyKit1/Authority",
      POLKIT ".Authority", "RevokeTemporaryAuthorizations",
      g_variant_new ("((sa{sv}))", "unix-session", &details), nullptr, G_DBUS_CALL_FLAGS_NONE,
      REVOKE_TIMEOUT_MS, chandler_async_cancellable (a), chandler_async_ready, a);
}

static void
on_system_bus (GObject *source G_GNUC_UNUSED, GAsyncResult *result, GObject *owner, void *data)
{
  Install *i = data;
  g_autoptr (GError) error = nullptr;
  g_autoptr (GDBusConnection) bus = g_bus_get_finish (result, &error);
  if (bus == nullptr || owner == nullptr)
    {
      if (error != nullptr)
        g_debug ("no system bus: %s %d", g_quark_to_string (error->domain), error->code);
      installed (i);
      return;
    }
  /* logind's "auto" session: the caller's own, the one polkit requires. */
  ChandlerAsync *a = chandler_async_new (owner, on_session, g_rc_box_acquire (i),
                                         (GDestroyNotify) install_release);
  g_dbus_connection_call (bus, LOGIND, "/org/freedesktop/login1/session/auto",
                          "org.freedesktop.DBus.Properties", "Get",
                          g_variant_new ("(ss)", LOGIND ".Session", "Id"), G_VARIANT_TYPE ("(v)"),
                          G_DBUS_CALL_FLAGS_NONE, REVOKE_TIMEOUT_MS, chandler_async_cancellable (a),
                          chandler_async_ready, a);
}

static void
on_install_done (GObject *source, GAsyncResult *result, GObject *owner G_GNUC_UNUSED, void *data)
{
  Install *i = data;
  g_autoptr (GError) error = nullptr;
  g_autoptr (PkResults) results = pk_client_generic_finish (PK_CLIENT (source), result, &error);
  i->async = nullptr;
  i->outcome = results_outcome (results, error);
  i->detail = g_strdup (detail_of (i->outcome, error));
  /* Whatever happened, and even when the window has gone: the revoke is owned by the
   * packages object, which the Install holds. */
  ChandlerAsync *a = chandler_async_new (G_OBJECT (i->packages), on_system_bus,
                                         g_rc_box_acquire (i), (GDestroyNotify) install_release);
  g_bus_get (G_BUS_TYPE_SYSTEM, chandler_async_cancellable (a), chandler_async_ready, a);
}

void
chandler_packages_install (ChandlerPackages *packages, GObject *owner, const ChandlerPlan *plan,
                           ChandlerStageFunc stage, ChandlerInstallFunc done, void *data)
{
  Install *i = g_rc_box_new0 (Install);
  i->packages = g_object_ref (packages);
  g_weak_ref_init (&i->owner, owner);
  i->ids = g_strdupv (plan->ids);
  i->on_stage = stage;
  i->done = done;
  i->data = data;
  /* Trusted packages only: there is no "install anyway" (plan §3.3). */
  PkBitfield flags = pk_bitfield_from_enums (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED, -1);
  i->async = chandler_async_new (owner, on_install_done, i, (GDestroyNotify) install_release);
  pk_client_install_packages_async (packages->installer, flags, i->ids,
                                    chandler_async_cancellable (i->async), on_install_progress, i,
                                    chandler_async_ready, i->async);
}

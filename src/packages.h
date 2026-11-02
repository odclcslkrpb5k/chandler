/* The PackageKit adapter (ER-3, plan §3.3). Nothing of packagekit-glib2 leaves this file.
 * Whether the daemon is there, what state a set of packages is in, and installing. */
#pragma once

#include "outcome.h"

/* A package PackageKit itself named in answer to a resolve: the only kind of thing that
 * can ever be installed (plan §3.3). Opaque, and minted only in packages.c (BD-5). */
typedef struct _ChandlerPackageId ChandlerPackageId;

typedef enum
{
  CHANDLER_PKG_INSTALLED,
  CHANDLER_PKG_AVAILABLE,   /* exactly one native, newest candidate */
  CHANDLER_PKG_UNAVAILABLE, /* not in the software sources */
  CHANDLER_PKG_AMBIGUOUS,   /* several candidates after filtering: refuse rather than guess */
} ChandlerPkgState;

const char *chandler_pkg_state_name (ChandlerPkgState state);

/* The answer for a set of package names. */
typedef struct _ChandlerResolved ChandlerResolved;
void chandler_resolved_free (ChandlerResolved *resolved);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (ChandlerResolved, chandler_resolved_free)

/* A name that wasn't asked about is UNAVAILABLE. */
ChandlerPkgState chandler_resolved_state (const ChandlerResolved *resolved, const char *name);
/* For tests: the candidate's package ID, or nullptr unless the name is AVAILABLE. */
const char *chandler_resolved_id_for_test (const ChandlerResolved *resolved, const char *name);

/* An application's state from its packages: INSTALLED when all are, AVAILABLE when the rest
 * can be installed, otherwise the first UNAVAILABLE or AMBIGUOUS one's. */
ChandlerPkgState chandler_resolved_app_state (const ChandlerResolved *resolved,
                                              const char *const *pkgnames);

#define CHANDLER_TYPE_PACKAGES (chandler_packages_get_type ())
G_DECLARE_FINAL_TYPE (ChandlerPackages, chandler_packages, CHANDLER, PACKAGES, GObject)

/* Asks the daemon for its properties: OK, or NO_DAEMON whatever the reason (not installed,
 * masked, not starting, no system bus). `packages` is borrowed; g_object_ref it to keep it.
 * Runs while `owner` lives. */
typedef void (*ChandlerPackagesFunc) (ChandlerPackages *packages, ChandlerOutcome outcome,
                                      GObject *owner, void *data);
void chandler_packages_open (GObject *owner, ChandlerPackagesFunc done, void *data);

/* Resolves `names`: one transaction for what is installed, one for native, newest, binary
 * candidates. `resolved` is nullptr unless `outcome` is OK; it is borrowed. */
typedef void (*ChandlerResolveFunc) (const ChandlerResolved *resolved, ChandlerOutcome outcome,
                                     GObject *owner, void *data);
void chandler_packages_resolve (ChandlerPackages *packages, GObject *owner,
                                const char *const *names, ChandlerResolveFunc done, void *data);

/* For tests: the outcome a GError from PackageKit maps to. */
ChandlerOutcome chandler_packages_outcome_for_test (const GError *error);
/* For tests: the outcome of every daemon error code, by its PackageKit number. */
ChandlerOutcome chandler_packages_daemon_outcome_for_test (int code);

/* --- Installing (plan §3.3) --- */

/* What an install will do, as PackageKit's simulation of it said: the chosen packages, and
 * how many others come with them. Minted only by chandler_packages_check, from a fresh
 * resolve: install takes nothing else. */
typedef struct _ChandlerPlan ChandlerPlan;
void chandler_plan_free (ChandlerPlan *plan);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (ChandlerPlan, chandler_plan_free)
/* Packages other than the chosen ones that the install brings or updates. */
guint chandler_plan_extra (const ChandlerPlan *plan);
/* Some of those are updates of installed packages. */
bool chandler_plan_updates (const ChandlerPlan *plan);
/* The two simulations would do exactly the same: a confirm still stands. */
bool chandler_plan_same (const ChandlerPlan *a, const ChandlerPlan *b);

/* Resolves `names` again and simulates installing those not installed yet. `plan` (owned by
 * the callee) is nullptr unless `outcome` is OK. ALREADY: every name is installed. GONE: a
 * name no longer has exactly one candidate. WOULD_CHANGE and UNTRUSTED: the simulation
 * does more than install and update; `detail` then says what ("remove hello"). Otherwise
 * `detail` is nullptr, or PackageKit's own text for OTHER (shown, never logged: DP-3). */
typedef void (*ChandlerCheckFunc) (ChandlerPlan *plan, ChandlerOutcome outcome, const char *detail,
                                   GObject *owner, void *data);
void chandler_packages_check (ChandlerPackages *packages, GObject *owner, const char *const *names,
                              ChandlerCheckFunc done, void *data);

/* Installs exactly the plan's packages, trusted only, with a password prompt allowed.
 * `stage` runs with ASKING when PackageKit starts waiting for the password, and once with
 * WORKING when it has gone past that (closing the window then no longer leaves a prompt
 * behind); ASKING may not come at all (polkit's kept authorization). When
 * the transaction has finished, polkit's kept authorization for this session is revoked
 * (plan §3.3), whether or not `owner` still lives, and then `done` runs while it does.
 * `detail` as for chandler_packages_check. */
typedef enum
{
  CHANDLER_INSTALL_ASKING,
  CHANDLER_INSTALL_WORKING,
} ChandlerInstallStage;
typedef void (*ChandlerStageFunc) (ChandlerInstallStage stage, GObject *owner, void *data);
typedef void (*ChandlerInstallFunc) (ChandlerOutcome outcome, const char *detail, GObject *owner,
                                     void *data);
void chandler_packages_install (ChandlerPackages *packages, GObject *owner,
                                const ChandlerPlan *plan, ChandlerStageFunc stage,
                                ChandlerInstallFunc done, void *data);

/* The PackageKit adapter (ER-3, plan §3.3). Nothing of packagekit-glib2 leaves this file.
 * Milestone 2: whether the daemon is there, and what state a set of packages is in. */
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

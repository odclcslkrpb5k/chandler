/* The AppStream adapter (ER-3, plan §3.2): the distribution's catalog of applications.
 * Nothing of libappstream leaves this file; callers get owned ChandlerApp copies. */
#pragma once

typedef struct
{
  char *id;           /* AppStream component id, e.g. "org.gnome.Evince" */
  char *name;         /* valid UTF-8 */
  char *summary;      /* valid UTF-8, may be "" */
  char **pkgnames;    /* at least one */
  char *icon_stock;   /* icon theme name, or nullptr */
  char *icon_cached; /* absolute path of a cached PNG under the catalog's icon root, or nullptr */
} ChandlerApp;

void chandler_app_free (ChandlerApp *app);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (ChandlerApp, chandler_app_free)

#define CHANDLER_TYPE_CATALOG (chandler_catalog_get_type ())
G_DECLARE_FINAL_TYPE (ChandlerCatalog, chandler_catalog, CHANDLER, CATALOG, GObject)

/* `catalog` is nullptr when loading failed; `error` then says why. Runs while `owner` lives.
 * `catalog` is borrowed: g_object_ref it to keep it. */
typedef void (*ChandlerCatalogFunc) (ChandlerCatalog *catalog, const GError *error, GObject *owner,
                                     void *data);

/* The system's catalog (the distribution's AppStream data only: no desktop files, no
 * Flatpak), loaded asynchronously. */
void chandler_catalog_open (GObject *owner, ChandlerCatalogFunc done, void *data);

/* Tests: only the catalog under `dir` (DEP-11 YAML in dir/yaml, icons in dir/icons). Same
 * code path as chandler_catalog_open, with the system's locations switched off. */
void chandler_catalog_open_at (GObject *owner, const char *dir, ChandlerCatalogFunc done,
                               void *data);

/* No application data at all: the distribution's metadata is missing (run apt update). */
bool chandler_catalog_is_empty (ChandlerCatalog *catalog);

/* Desktop applications with a package, no bundle, that open any of `types` (a type and its
 * aliases, chandler_aliases_expand), each once, sorted by name. GPtrArray of ChandlerApp. */
GPtrArray *chandler_catalog_for_types (ChandlerCatalog *catalog, const char *const *types);

/* The same filter over AppStream's own text search of names, summaries and keywords. */
GPtrArray *chandler_catalog_search (ChandlerCatalog *catalog, const char *text);

/* The origins (catalog names) the given apps came from, for tests: a fixture catalog must
 * not be joined by the host's. */
char **chandler_catalog_origins_for_test (ChandlerCatalog *catalog, const char *const *types);

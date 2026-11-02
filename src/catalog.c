#include "catalog.h"
#include "async.h"
#include "pkgname.h"
#include "text.h"

struct _ChandlerCatalog
{
  GObject parent;
  AsPool *pool;
  char **icon_roots; /* cached icons must lie under one of these directories */
};

G_DEFINE_FINAL_TYPE (ChandlerCatalog, chandler_catalog, G_TYPE_OBJECT)

/* Where the distribution's catalogs keep their icons (AppStream's standard locations;
 * /var/lib/app-info links to /var/lib/swcatalog). A cached icon must lie under one. */
static const char *const system_icon_roots[]
    = { "/usr/share/swcatalog/icons", "/var/lib/swcatalog/icons", "/var/cache/swcatalog/icons",
        nullptr };

static void
chandler_catalog_finalize (GObject *object)
{
  ChandlerCatalog *self = CHANDLER_CATALOG (object);
  g_clear_object (&self->pool);
  g_strfreev (self->icon_roots);
  G_OBJECT_CLASS (chandler_catalog_parent_class)->finalize (object);
}

static void
chandler_catalog_class_init (ChandlerCatalogClass *klass)
{
  G_OBJECT_CLASS (klass)->finalize = chandler_catalog_finalize;
}

static void
chandler_catalog_init (ChandlerCatalog *self G_GNUC_UNUSED)
{
}

void
chandler_app_free (ChandlerApp *app)
{
  if (app == nullptr)
    return;
  g_free (app->id);
  g_free (app->name);
  g_free (app->summary);
  g_strfreev (app->pkgnames);
  g_free (app->icon_stock);
  g_free (app->icon_cached);
  g_free (app);
}

typedef struct
{
  ChandlerCatalog *catalog; /* owned until handed over */
  ChandlerCatalogFunc done;
  void *data;
} Open;

static void
open_free (Open *open)
{
  g_clear_object (&open->catalog);
  g_free (open);
}

static void
on_loaded (GObject *source, GAsyncResult *result, GObject *owner, void *data)
{
  Open *open = data;
  g_autoptr (GError) error = nullptr;
  bool ok = as_pool_load_finish (AS_POOL (source), result, &error);
  if (owner == nullptr)
    return;
  if (!ok)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        open->done (nullptr, error, owner, open->data);
      return;
    }
  open->done (open->catalog, nullptr, owner, open->data);
}

/* The one place a pool is configured, in an order that matters:
 * as_pool_set_load_std_data_locations (FALSE) resets the flags to none, and setting
 * LOAD_OS_CATALOG after it brings the system's catalog back. Flags first, then (tests only)
 * the switch-off and the fixture's directory. */
static void
open_pool (GObject *owner, const char *dir, ChandlerCatalogFunc done, void *data)
{
  Open *open = g_new0 (Open, 1);
  open->catalog = g_object_new (CHANDLER_TYPE_CATALOG, nullptr);
  open->done = done;
  open->data = data;
  AsPool *pool = as_pool_new ();
  as_pool_set_flags (pool, AS_POOL_FLAG_LOAD_OS_CATALOG);
  if (dir != nullptr)
    {
      as_pool_set_load_std_data_locations (pool, false);
      as_pool_add_extra_data_location (pool, dir, AS_FORMAT_STYLE_CATALOG);
      char *roots[] = { g_build_filename (dir, "icons", nullptr), nullptr };
      open->catalog->icon_roots = g_strdupv (roots);
      g_free (roots[0]);
    }
  else
    {
      GPtrArray *roots = g_ptr_array_new ();
      for (const char *const *r = system_icon_roots; *r != nullptr; r++)
        g_ptr_array_add (roots, g_strdup (*r));
      g_ptr_array_add (roots, nullptr);
      open->catalog->icon_roots = (char **) g_ptr_array_free (roots, false);
    }
  open->catalog->pool = pool;
  ChandlerAsync *a = chandler_async_new (owner, on_loaded, open, (GDestroyNotify) open_free);
  as_pool_load_async (pool, chandler_async_cancellable (a), chandler_async_ready, a);
}

void
chandler_catalog_open (GObject *owner, ChandlerCatalogFunc done, void *data)
{
  open_pool (owner, nullptr, done, data);
}

void
chandler_catalog_open_at (GObject *owner, const char *dir, ChandlerCatalogFunc done, void *data)
{
  open_pool (owner, dir, done, data);
}

bool
chandler_catalog_is_empty (ChandlerCatalog *catalog)
{
  return as_pool_is_empty (catalog->pool);
}

/* A cached icon's file, if it is one and lies under the catalog's icon root: metadata
 * never makes Chandler read a path of its choosing (plan §3.2). */
static char *
cached_icon_path (const ChandlerCatalog *catalog, AsIcon *icon)
{
  const char *file = as_icon_get_filename (icon);
  if (file == nullptr || !g_path_is_absolute (file))
    return nullptr;
  g_autofree char *canonical = g_canonicalize_filename (file, nullptr);
  for (char **r = catalog->icon_roots; *r != nullptr; r++)
    {
      g_autofree char *root = g_strconcat (*r, G_DIR_SEPARATOR_S, nullptr);
      if (g_str_has_prefix (canonical, root))
        return g_steal_pointer (&canonical);
    }
  return nullptr;
}

/* STOCK and CACHED only: REMOTE would fetch from the network, LOCAL reads any path. */
static void
take_icons (const ChandlerCatalog *catalog, AsComponent *cpt, ChandlerApp *app)
{
  GPtrArray *icons = as_component_get_icons (cpt);
  for (guint i = 0; icons != nullptr && i < icons->len; i++)
    {
      AsIcon *icon = g_ptr_array_index (icons, i);
      switch (as_icon_get_kind (icon))
        {
        case AS_ICON_KIND_STOCK:
          if (app->icon_stock == nullptr && as_icon_get_name (icon) != nullptr)
            app->icon_stock = chandler_text_valid (as_icon_get_name (icon));
          break;
        case AS_ICON_KIND_CACHED:
          if (app->icon_cached == nullptr)
            app->icon_cached = cached_icon_path (catalog, icon);
          break;
        case AS_ICON_KIND_REMOTE:
        case AS_ICON_KIND_LOCAL:
        case AS_ICON_KIND_UNKNOWN:
        case AS_ICON_KIND_LAST: break;
        }
    }
}

/* A desktop application Chandler can install: it names packages, each a valid Debian
 * package name, and no bundle (snap, flatpak) delivers it. */
static bool
wanted (AsComponent *cpt)
{
  if (as_component_get_kind (cpt) != AS_COMPONENT_KIND_DESKTOP_APP)
    return false;
  char **pkgnames = as_component_get_pkgnames (cpt);
  if (pkgnames == nullptr || pkgnames[0] == nullptr)
    return false;
  for (char **p = pkgnames; *p != nullptr; p++)
    if (!chandler_pkgname_valid (*p))
      return false;
  const GPtrArray *bundles = as_component_get_bundles (cpt);
  return bundles == nullptr || bundles->len == 0;
}

static ChandlerApp *
copy_app (const ChandlerCatalog *catalog, AsComponent *cpt)
{
  ChandlerApp *app = g_new0 (ChandlerApp, 1);
  app->id = chandler_text_valid (as_component_get_id (cpt));
  app->name = chandler_text_valid (as_component_get_name (cpt));
  app->summary = chandler_text_valid (as_component_get_summary (cpt));
  char **pkgnames = as_component_get_pkgnames (cpt);
  GPtrArray *names = g_ptr_array_new ();
  for (char **p = pkgnames; *p != nullptr; p++)
    g_ptr_array_add (names, g_strdup (*p)); /* checked by wanted (): ASCII, valid */
  g_ptr_array_add (names, nullptr);
  app->pkgnames = (char **) g_ptr_array_free (names, false);
  take_icons (catalog, cpt, app);
  return app;
}

static int
by_name (const void *a, const void *b)
{
  const ChandlerApp *x = *(ChandlerApp *const *) a;
  const ChandlerApp *y = *(ChandlerApp *const *) b;
  g_autofree char *kx = g_utf8_collate_key (x->name, -1);
  g_autofree char *ky = g_utf8_collate_key (y->name, -1);
  int c = strcmp (kx, ky);
  return c != 0 ? c : strcmp (x->id, y->id);
}

/* Adds the wanted components of `box` not already in `seen` (component ids). */
static void
collect (const ChandlerCatalog *catalog, AsComponentBox *box, GHashTable *seen, GPtrArray *out)
{
  guint n = as_component_box_get_size (box);
  for (guint i = 0; i < n; i++)
    {
      AsComponent *cpt = as_component_box_index_safe (box, i);
      if (cpt == nullptr || !wanted (cpt))
        continue;
      const char *id = as_component_get_id (cpt);
      if (id == nullptr || !g_hash_table_add (seen, g_strdup (id)))
        continue;
      g_ptr_array_add (out, copy_app (catalog, cpt));
    }
}

GPtrArray *
chandler_catalog_for_types (ChandlerCatalog *catalog, const char *const *types)
{
  GPtrArray *out = g_ptr_array_new_with_free_func ((GDestroyNotify) chandler_app_free);
  g_autoptr (GHashTable) seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, nullptr);
  for (const char *const *t = types; *t != nullptr; t++)
    {
      g_autoptr (AsComponentBox) box
          = as_pool_get_components_by_provided_item (catalog->pool, AS_PROVIDED_KIND_MEDIATYPE, *t);
      if (box != nullptr)
        collect (catalog, box, seen, out);
    }
  g_ptr_array_sort (out, by_name);
  return out;
}

GPtrArray *
chandler_catalog_search (ChandlerCatalog *catalog, const char *text)
{
  GPtrArray *out = g_ptr_array_new_with_free_func ((GDestroyNotify) chandler_app_free);
  g_autoptr (GHashTable) seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, nullptr);
  g_autoptr (AsComponentBox) box = as_pool_search (catalog->pool, text);
  if (box != nullptr)
    collect (catalog, box, seen, out);
  g_ptr_array_sort (out, by_name);
  return out;
}

char **
chandler_catalog_origins_for_test (ChandlerCatalog *catalog, const char *const *types)
{
  g_autoptr (GHashTable) origins = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, nullptr);
  for (const char *const *t = types; *t != nullptr; t++)
    {
      g_autoptr (AsComponentBox) box
          = as_pool_get_components_by_provided_item (catalog->pool, AS_PROVIDED_KIND_MEDIATYPE, *t);
      guint n = box != nullptr ? as_component_box_get_size (box) : 0;
      for (guint i = 0; i < n; i++)
        {
          AsComponent *cpt = as_component_box_index_safe (box, i);
          const char *origin = cpt != nullptr ? as_component_get_origin (cpt) : nullptr;
          g_hash_table_add (origins, g_strdup (origin != nullptr ? origin : "(none)"));
        }
    }
  GPtrArray *out = g_ptr_array_new ();
  GHashTableIter iter;
  void *key;
  g_hash_table_iter_init (&iter, origins);
  while (g_hash_table_iter_next (&iter, &key, nullptr))
    g_ptr_array_add (out, g_strdup (key));
  g_ptr_array_add (out, nullptr);
  return (char **) g_ptr_array_free (out, false);
}

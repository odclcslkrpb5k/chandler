/* The AppStream adapter against real libappstream and a catalog cut from the archive
 * (tests/data/catalog, tools/make-fixture.py). */
#include "aliases.h"
#include "catalog.h"
#include "async.h"

typedef struct
{
  ChandlerCatalog *catalog;
  bool done;
} Loaded;

static void
on_catalog (ChandlerCatalog *catalog, const GError *error, GObject *owner G_GNUC_UNUSED, void *data)
{
  Loaded *l = data;
  if (catalog == nullptr)
    gbk_fatal ("fixture catalog: %s", error->message);
  l->catalog = g_object_ref (catalog);
  l->done = true;
}

static ChandlerCatalog *
load (void)
{
  g_autofree char *dir
      = g_build_filename (g_getenv ("CHANDLER_SOURCE_ROOT"), "tests", "data", "catalog", nullptr);
  g_autoptr (GObject) owner = g_object_new (G_TYPE_OBJECT, nullptr);
  Loaded l = { 0 };
  chandler_catalog_open_at (owner, dir, on_catalog, &l);
  while (!l.done)
    g_main_context_iteration (nullptr, true);
  g_assert_cmpint (chandler_async_live (), ==, 0);
  return l.catalog;
}

static const ChandlerApp *
find (const GPtrArray *apps, const char *id)
{
  for (guint i = 0; i < apps->len; i++)
    {
      const ChandlerApp *app = g_ptr_array_index (apps, i);
      if (g_str_equal (app->id, id))
        return app;
    }
  return nullptr;
}

/* find(), for an app the test goes on to use: g_assert_nonnull can return under GTest, so
 * the next line could dereference nullptr. */
static const ChandlerApp *
need (const GPtrArray *apps, const char *id)
{
  const ChandlerApp *app = find (apps, id);
  if (app == nullptr)
    gbk_fatal ("%s is not in the results", id);
  return app;
}

static int
count (const GPtrArray *apps, const char *id)
{
  int n = 0;
  for (guint i = 0; i < apps->len; i++)
    n += g_str_equal (((const ChandlerApp *) g_ptr_array_index (apps, i))->id, id);
  return n;
}

/* Only the fixture's catalogs answer: the host's (when a test runs outside the container)
 * must not join them (plan §3.2). */
static void
test_only_fixture (void)
{
  g_autoptr (ChandlerCatalog) catalog = load ();
  const char *const types[]
      = { "application/pdf", "audio/flac", "audio/x-flac", "image/png", "text/markdown", nullptr };
  g_auto (GStrv) origins = chandler_catalog_origins_for_test (catalog, types);
  g_assert_cmpuint (g_strv_length (origins), >, 0);
  for (char **o = origins; *o != nullptr; o++)
    if (!g_str_has_prefix (*o, "chandler-fixture") && !g_str_equal (*o, "../escape"))
      gbk_fatal ("a component from %s, not the fixture", *o);
  g_assert_false (chandler_catalog_is_empty (catalog));
}

static void
test_pdf (void)
{
  g_autoptr (ChandlerCatalog) catalog = load ();
  const char *const types[] = { "application/pdf", nullptr };
  g_autoptr (GPtrArray) apps = chandler_catalog_for_types (catalog, types);
  /* In two catalogs, listed once. */
  g_assert_cmpint (count (apps, "org.gnome.Evince"), ==, 1);
  g_assert_nonnull (find (apps, "xournal.desktop"));
  g_assert_null (find (apps, "chandler.test.Bundled"));
  g_assert_null (find (apps, "chandler.test.NoPackage"));
  /* "Bad Name" would make PackageKit refuse every name sent with it. */
  g_assert_null (find (apps, "chandler.test.BadPackageName"));

  const ChandlerApp *evince = need (apps, "org.gnome.Evince");
  g_assert_cmpstr (evince->pkgnames[0], ==, "evince");
  g_assert_nonnull (evince->icon_cached);
  g_assert_true (g_file_test (evince->icon_cached, G_FILE_TEST_IS_REGULAR));
  g_assert_true (g_str_has_suffix (evince->icon_cached, ".png"));

  /* Text, not markup: kept as written, shown later with gtk_label_set_text. */
  const ChandlerApp *markup = need (apps, "chandler.test.Markup");
  g_assert_cmpstr (markup->name, ==, "<b>Bold</b> & Viewer");
  g_assert_nonnull (strchr (markup->summary, '\x1b'));
}

static void
test_alias_only_apps (void)
{
  g_autoptr (ChandlerCatalog) catalog = load ();
  g_autoptr (ChandlerAliases) aliases = chandler_aliases_new ();
  const char pair[] = "audio/x-flac audio/flac\n";
  g_assert_cmpuint (chandler_aliases_add_text (aliases, pair, strlen (pair)), ==, 0);
  ChandlerMediaType flac;
  g_assert_true (chandler_mediatype_parse ("audio/flac", &flac));
  g_auto (GStrv) types = chandler_aliases_expand (aliases, &flac);

  const char *const only_canonical[] = { "audio/flac", nullptr };
  g_autoptr (GPtrArray) narrow = chandler_catalog_for_types (catalog, only_canonical);
  g_autoptr (GPtrArray) wide = chandler_catalog_for_types (catalog, (const char *const *) types);
  g_assert_null (find (narrow, "org.gnome.Rhythmbox3.desktop"));
  g_assert_nonnull (find (wide, "org.gnome.Rhythmbox3.desktop"));
  g_assert_nonnull (find (wide, "io.github.quodlibet.QuodLibet"));
  g_assert_nonnull (find (wide, "org.musicbrainz.Picard"));
}

static void
test_addon_excluded (void)
{
  g_autoptr (ChandlerCatalog) catalog = load ();
  const char *const types[] = { "text/markdown", nullptr };
  g_autoptr (GPtrArray) apps = chandler_catalog_for_types (catalog, types);
  g_assert_nonnull (find (apps, "me.mitya57.ReText"));
  g_assert_null (find (apps, "org.kde.okular-md"));
}

/* A catalog's origin names its icon directory; one that climbs out of it ("../escape")
 * gets no icon, though the file it points at exists. */
static void
test_icon_outside_root (void)
{
  g_autoptr (ChandlerCatalog) catalog = load ();
  const char *const types[] = { "application/pdf", nullptr };
  g_autoptr (GPtrArray) apps = chandler_catalog_for_types (catalog, types);
  const ChandlerApp *escape = need (apps, "chandler.test.Escape");
  g_autofree char *bait = g_build_filename (g_getenv ("CHANDLER_SOURCE_ROOT"), "tests", "data",
                                            "catalog", "escape", "64x64", "escape.png", nullptr);
  g_assert_true (g_file_test (bait, G_FILE_TEST_IS_REGULAR));
  g_assert_null (escape->icon_cached);
}

/* A stock icon is a theme name: one that is a path is dropped, so GTK is never handed a file
 * the metadata chose. */
static void
test_stock_path (void)
{
  g_autoptr (ChandlerCatalog) catalog = load ();
  const char *const types[] = { "application/x-chandler-stockpath", nullptr };
  g_autoptr (GPtrArray) apps = chandler_catalog_for_types (catalog, types);
  const ChandlerApp *app = need (apps, "chandler.test.StockPath");
  g_assert_null (app->icon_stock);
}

typedef struct
{
  bool done;
  bool decoy;
} System;

static void
on_system (ChandlerCatalog *catalog, const GError *error, GObject *owner G_GNUC_UNUSED, void *data)
{
  System *s = data;
  if (catalog == nullptr)
    gbk_fatal ("system catalog: %s", error->message);
  const char *const types[] = { "application/pdf", nullptr };
  g_autoptr (GPtrArray) apps = chandler_catalog_for_types (catalog, types);
  s->decoy = find (apps, "chandler.decoy.System") != nullptr;
  s->done = true;
}

/* The image's decoy system catalog is there to be found, so test_only_fixture's refusal of
 * it means something (TS-6). */
static void
test_system_has_decoy (void)
{
  g_autoptr (GObject) owner = g_object_new (G_TYPE_OBJECT, nullptr);
  System s = { 0 };
  chandler_catalog_open (owner, on_system, &s);
  while (!s.done)
    g_main_context_iteration (nullptr, true);
  if (!s.decoy)
    gbk_fatal ("the system catalog lacks the image's decoy (Dockerfile)");
}

static void
test_search (void)
{
  g_autoptr (ChandlerCatalog) catalog = load ();
  g_autoptr (GPtrArray) apps = chandler_catalog_search (catalog, "rhythmbox");
  g_assert_nonnull (find (apps, "org.gnome.Rhythmbox3.desktop"));
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, nullptr);
  /* AppStream writes a cache: give it a private one. */
  g_autofree char *cache = g_dir_make_tmp ("chandler-test-XXXXXX", nullptr);
  if (cache == nullptr)
    gbk_fatal ("no temporary directory for the AppStream cache");
  g_setenv ("XDG_CACHE_HOME", cache, true);
  g_test_add_func ("/catalog/only-fixture", test_only_fixture);
  g_test_add_func ("/catalog/pdf", test_pdf);
  g_test_add_func ("/catalog/alias-only-apps", test_alias_only_apps);
  g_test_add_func ("/catalog/addon-excluded", test_addon_excluded);
  g_test_add_func ("/catalog/search", test_search);
  g_test_add_func ("/catalog/icon-outside-root", test_icon_outside_root);
  g_test_add_func ("/catalog/stock-path", test_stock_path);
  g_test_add_func ("/catalog/system-has-decoy", test_system_has_decoy);
  return g_test_run ();
}

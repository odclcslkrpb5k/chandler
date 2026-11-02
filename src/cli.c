#include "cli.h"
#include "aliases.h"
#include "catalog.h"
#include "lifetime.h"
#include "packages.h"
#include "text.h"

typedef struct
{
  ChandlerMediaType type;
  char **types; /* the type and its aliases */
  GPtrArray *apps; /* of ChandlerApp */
  int status;
} List;

static void
print_field (const char *s, const char *end)
{
  g_autofree char *clean = chandler_text_for_terminal (s);
  g_print ("%s%s", clean, end);
}

static void
print_apps (const List *list, const ChandlerResolved *resolved)
{
  for (guint i = 0; i < list->apps->len; i++)
    {
      const ChandlerApp *app = g_ptr_array_index (list->apps, i);
      const char *state = resolved != nullptr
                              ? chandler_pkg_state_name (chandler_resolved_app_state (
                                    resolved, (const char *const *) app->pkgnames))
                              : "unknown";
      g_autofree char *pkgs = g_strjoinv (",", app->pkgnames);
      print_field (state, "\t");
      print_field (app->id, "\t");
      print_field (pkgs, "\t");
      print_field (app->name, "\t");
      print_field (app->summary, "\n");
    }
}

static void
on_resolved (const ChandlerResolved *resolved, ChandlerOutcome outcome, GObject *owner, void *data)
{
  const List *list = data;
  if (outcome != CHANDLER_OUTCOME_OK)
    g_printerr ("chandler: couldn't ask PackageKit about the packages (%s)\n",
                chandler_outcome_name (outcome));
  print_apps (list, resolved);
  g_application_release (G_APPLICATION (owner));
}

static void
on_packages (ChandlerPackages *packages, ChandlerOutcome outcome, GObject *owner, void *data)
{
  List *list = data;
  if (outcome != CHANDLER_OUTCOME_OK)
    {
      g_printerr ("chandler: %s\n", chandler_outcome_text (outcome));
      print_apps (list, nullptr);
      g_application_release (G_APPLICATION (owner));
      return;
    }
  g_autoptr (GPtrArray) names = g_ptr_array_new ();
  for (guint i = 0; i < list->apps->len; i++)
    {
      const ChandlerApp *app = g_ptr_array_index (list->apps, i);
      for (char **p = app->pkgnames; *p != nullptr; p++)
        g_ptr_array_add (names, *p);
    }
  g_ptr_array_add (names, nullptr);
  chandler_packages_resolve (packages, owner, (const char *const *) names->pdata, on_resolved,
                             list);
}

static void
on_catalog (ChandlerCatalog *catalog, const GError *error, GObject *owner, void *data)
{
  List *list = data;
  if (catalog == nullptr || chandler_catalog_is_empty (catalog))
    {
      g_printerr ("chandler: %s\n", catalog == nullptr
                                        ? "couldn't read the software catalog"
                                        : "no software catalog: run \"sudo apt update\"");
      if (error != nullptr)
        g_debug ("catalog: %s %d", g_quark_to_string (error->domain), error->code);
      list->status = 2;
      g_application_release (G_APPLICATION (owner));
      return;
    }
  list->apps = chandler_catalog_for_types (catalog, (const char *const *) list->types);
  if (list->apps->len == 0)
    {
      g_printerr ("chandler: no application in your software sources opens %s\n", list->type.value);
      list->status = 1;
      g_application_release (G_APPLICATION (owner));
      return;
    }
  chandler_packages_open (owner, on_packages, list);
}

static void
on_aliases (const ChandlerAliases *aliases, GObject *owner, void *data)
{
  List *list = data;
  list->types = chandler_aliases_expand (aliases, &list->type);
  chandler_catalog_open (owner, on_catalog, list);
}

static void
on_activate (GApplication *app, void *data)
{
  g_application_hold (app);
  chandler_aliases_load (G_OBJECT (app), on_aliases, data);
}

int
chandler_cli_list (const ChandlerMediaType *type)
{
  List list = { .type = *type };
  g_autoptr (GApplication) app = g_application_new (nullptr, G_APPLICATION_NON_UNIQUE);
  gbk_connect (G_OBJECT (app), app, "activate", G_CALLBACK (on_activate), &list);
  char name[] = "chandler";
  char *only_name[] = { name, nullptr };
  int status = g_application_run (app, 1, only_name);
  g_strfreev (list.types);
  g_clear_pointer (&list.apps, g_ptr_array_unref);
  return status != 0 ? status : list.status;
}

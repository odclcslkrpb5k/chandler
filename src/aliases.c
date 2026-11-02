#include "aliases.h"
#include "async.h"

struct _ChandlerAliases
{
  GHashTable *canonical; /* alias -> canonical (owned strings) */
  GHashTable *spellings; /* canonical -> GPtrArray of its aliases (owned) */
};

ChandlerAliases *
chandler_aliases_new (void)
{
  ChandlerAliases *a = g_new0 (ChandlerAliases, 1);
  a->canonical = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
  a->spellings
      = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_ptr_array_unref);
  return a;
}

void
chandler_aliases_free (ChandlerAliases *aliases)
{
  if (aliases == nullptr)
    return;
  g_hash_table_unref (aliases->canonical);
  g_hash_table_unref (aliases->spellings);
  g_free (aliases);
}

static void
add_pair (ChandlerAliases *a, const ChandlerMediaType *alias, const ChandlerMediaType *canonical)
{
  if (g_str_equal (alias->value, canonical->value)
      || g_hash_table_contains (a->canonical, alias->value))
    return;
  g_hash_table_insert (a->canonical, g_strdup (alias->value), g_strdup (canonical->value));
  GPtrArray *list = g_hash_table_lookup (a->spellings, canonical->value);
  if (list == nullptr)
    {
      list = g_ptr_array_new_with_free_func (g_free);
      g_hash_table_insert (a->spellings, g_strdup (canonical->value), list);
    }
  g_ptr_array_add (list, g_strdup (alias->value));
}

unsigned
chandler_aliases_add_text (ChandlerAliases *aliases, const char *text, size_t len)
{
  unsigned skipped = 0;
  g_autofree char *copy = g_strndup (text, len);
  g_auto (GStrv) lines = g_strsplit (copy, "\n", -1);
  for (char **line = lines; *line != nullptr; line++)
    {
      if (**line == '\0' || **line == '#')
        continue;
      g_auto (GStrv) words = g_strsplit_set (*line, " \t", -1);
      ChandlerMediaType alias, canonical;
      if (g_strv_length (words) != 2 || !chandler_mediatype_parse (words[0], &alias)
          || !chandler_mediatype_parse (words[1], &canonical))
        {
          skipped++;
          continue;
        }
      add_pair (aliases, &alias, &canonical);
    }
  return skipped;
}

static void
add_once (GPtrArray *out, const char *type)
{
  for (guint i = 0; i < out->len; i++)
    if (g_str_equal (g_ptr_array_index (out, i), type))
      return;
  g_ptr_array_add (out, g_strdup (type));
}

char **
chandler_aliases_expand (const ChandlerAliases *aliases, const ChandlerMediaType *type)
{
  GPtrArray *out = g_ptr_array_new ();
  add_once (out, type->value);
  const char *canonical = g_hash_table_lookup (aliases->canonical, type->value);
  if (canonical == nullptr)
    canonical = type->value;
  add_once (out, canonical);
  const GPtrArray *list = g_hash_table_lookup (aliases->spellings, canonical);
  for (guint i = 0; list != nullptr && i < list->len; i++)
    add_once (out, g_ptr_array_index (list, i));
  g_ptr_array_add (out, nullptr);
  return (char **) g_ptr_array_free (out, false);
}

static void
read_aliases (ChandlerAliases *aliases, const char *data_dir)
{
  g_autofree char *path = g_build_filename (data_dir, "mime", "aliases", nullptr);
  g_autofree char *text = nullptr;
  gsize len = 0;
  g_autoptr (GError) error = nullptr;
  if (!g_file_get_contents (path, &text, &len, &error))
    {
      if (!g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        g_debug ("mime aliases unreadable: %s %d", g_quark_to_string (error->domain), error->code);
      return;
    }
  unsigned skipped = chandler_aliases_add_text (aliases, text, len);
  if (skipped > 0)
    g_debug ("mime aliases: %u malformed lines skipped", skipped);
}

/* Worker thread: user data dir first, then the system's, as shared-mime-info layers them. */
static void
load_in_thread (GTask *task, void *source G_GNUC_UNUSED, void *data G_GNUC_UNUSED,
                GCancellable *cancellable G_GNUC_UNUSED)
{
  ChandlerAliases *aliases = chandler_aliases_new ();
  read_aliases (aliases, g_get_user_data_dir ());
  for (const char *const *d = g_get_system_data_dirs (); *d != nullptr; d++)
    read_aliases (aliases, *d);
  g_task_return_pointer (task, aliases, (GDestroyNotify) chandler_aliases_free);
}

typedef struct
{
  ChandlerAliasesFunc done;
  void *data;
} Load;

static void
on_loaded (GObject *source G_GNUC_UNUSED, GAsyncResult *result, GObject *owner, void *data)
{
  g_autoptr (ChandlerAliases) aliases = g_task_propagate_pointer (G_TASK (result), nullptr);
  if (aliases == nullptr)
    aliases = chandler_aliases_new ();
  if (owner != nullptr)
    {
      const Load *load = data;
      load->done (g_steal_pointer (&aliases), owner, load->data);
    }
}

void
chandler_aliases_load (GObject *owner, ChandlerAliasesFunc done, void *data)
{
  Load *load = g_new0 (Load, 1);
  load->done = done;
  load->data = data;
  ChandlerAsync *a = chandler_async_new (owner, on_loaded, load, g_free);
  g_autoptr (GTask) task
      = g_task_new (nullptr, chandler_async_cancellable (a), chandler_async_ready, a);
  g_task_run_in_thread (task, load_in_thread);
}

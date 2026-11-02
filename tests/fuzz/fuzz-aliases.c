/* chandler_aliases_add_text on any bytes, then every expansion it can produce: never
 * crashes, each list starts with its type and names each type once. */
#include "aliases.h"
#include "fuzz.h"

int
LLVMFuzzerTestOneInput (const uint8_t *data, size_t size)
{
  g_autoptr (ChandlerAliases) aliases = chandler_aliases_new ();
  chandler_aliases_add_text (aliases, (const char *) data, size);
  g_autofree char *text = g_strndup ((const char *) data, size);
  g_auto (GStrv) words = g_strsplit_set (text, " \t\n", -1);
  for (char **w = words; *w != nullptr; w++)
    {
      ChandlerMediaType t;
      if (!chandler_mediatype_parse (*w, &t))
        continue;
      g_auto (GStrv) all = chandler_aliases_expand (aliases, &t);
      if (!g_str_equal (all[0], t.value))
        gbk_fatal ("expansion does not start with its type");
      for (char **p = all; *p != nullptr; p++)
        for (char **q = p + 1; *q != nullptr; q++)
          if (g_str_equal (*p, *q))
            gbk_fatal ("expansion repeats a type");
    }
  return 0;
}

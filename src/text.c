#include "text.h"

char *
chandler_text_valid (const char *s)
{
  if (s == nullptr)
    return g_strdup ("");
  return g_utf8_make_valid (s, -1);
}

char *
chandler_text_for_terminal (const char *s)
{
  g_autofree char *valid = chandler_text_valid (s);
  GString *out = g_string_sized_new (strlen (valid));
  for (const char *p = valid; *p != '\0'; p = g_utf8_next_char (p))
    {
      gunichar c = g_utf8_get_char (p);
      GUnicodeType type = g_unichar_type (c);
      /* Controls, and the invisible characters that rearrange what is shown: bidirectional
       * overrides and zero-width characters (format), line and paragraph separators. */
      if (type == G_UNICODE_CONTROL || type == G_UNICODE_FORMAT || type == G_UNICODE_LINE_SEPARATOR
          || type == G_UNICODE_PARAGRAPH_SEPARATOR)
        continue;
      g_string_append_unichar (out, c);
    }
  return g_string_free (out, false);
}

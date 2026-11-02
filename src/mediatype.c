#include "mediatype.h"

/* RFC 6838 4.2: restricted-name = restricted-name-first *126restricted-name-chars */
#define NAME_MAX_CHARS 127

static bool
name_char (char c, bool first)
{
  if (g_ascii_isalnum (c))
    return true;
  return !first && strchr ("!#$&-^_.+", c) != nullptr && c != '\0';
}

/* Length of the restricted-name at `s`, ending at `stop`; 0 if it isn't one. */
static size_t
name_length (const char *s, char stop)
{
  size_t n = 0;
  while (s[n] != stop)
    {
      if (s[n] == '\0' || n == NAME_MAX_CHARS || !name_char (s[n], n == 0))
        return 0;
      n++;
    }
  return n;
}

bool
chandler_mediatype_parse (const char *text, ChandlerMediaType *out)
{
  if (text == nullptr)
    return false;
  size_t type = name_length (text, '/');
  if (type == 0)
    return false;
  size_t subtype = name_length (text + type + 1, '\0');
  if (subtype == 0)
    return false;
  size_t len = type + 1 + subtype;
  for (size_t i = 0; i < len; i++)
    out->value[i] = g_ascii_tolower (text[i]);
  out->value[len] = '\0';
  return true;
}

bool
chandler_mediatype_is_octet_stream (const ChandlerMediaType *type)
{
  return g_str_equal (type->value, "application/octet-stream");
}

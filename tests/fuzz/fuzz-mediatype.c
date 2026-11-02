/* chandler_mediatype_parse on any bytes: never crashes, and what it accepts is lowercase,
 * bounded, one slash, and parses again to itself. */
#include "fuzz.h"
#include "mediatype.h"

int
LLVMFuzzerTestOneInput (const uint8_t *data, size_t size)
{
  g_autofree char *text = g_strndup ((const char *) data, size);
  ChandlerMediaType t;
  if (!chandler_mediatype_parse (text, &t))
    return 0;
  size_t len = strlen (t.value);
  if (len >= CHANDLER_MEDIATYPE_MAX || strchr (t.value, '/') != strrchr (t.value, '/'))
    gbk_fatal ("accepted a malformed media type");
  for (size_t i = 0; i < len; i++)
    if (g_ascii_isupper (t.value[i]))
      gbk_fatal ("accepted media type is not lowercase");
  ChandlerMediaType again;
  if (!chandler_mediatype_parse (t.value, &again) || !g_str_equal (again.value, t.value))
    gbk_fatal ("accepted media type does not parse to itself");
  return 0;
}

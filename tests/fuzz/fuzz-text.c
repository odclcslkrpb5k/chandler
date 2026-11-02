/* The text sanitizers on any bytes: always valid UTF-8, and the terminal copy holds no
 * control character. */
#include "fuzz.h"
#include "text.h"

int
LLVMFuzzerTestOneInput (const uint8_t *data, size_t size)
{
  g_autofree char *input = g_strndup ((const char *) data, size);
  g_autofree char *valid = chandler_text_valid (input);
  if (!g_utf8_validate (valid, -1, nullptr))
    gbk_fatal ("chandler_text_valid returned invalid UTF-8");
  g_autofree char *term = chandler_text_for_terminal (input);
  if (!g_utf8_validate (term, -1, nullptr))
    gbk_fatal ("chandler_text_for_terminal returned invalid UTF-8");
  /* An oracle independent of the code under test: explicit ranges, not GLib's tables. */
  for (const char *p = term; *p != '\0'; p = g_utf8_next_char (p))
    {
      gunichar c = g_utf8_get_char (p);
      if (c < 0x20 || (c >= 0x7f && c <= 0x9f) || (c >= 0x200b && c <= 0x200f)
          || (c >= 0x2028 && c <= 0x202e) || (c >= 0x2066 && c <= 0x2069) || c == 0xfeff)
        gbk_fatal ("U+%04X reached the terminal copy", (unsigned) c);
    }
  return 0;
}

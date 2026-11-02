/* chandler_request_parse on any argv: the input's NUL-separated pieces are the arguments.
 * Never crashes; text it keeps is valid UTF-8; a type it keeps is a parsed media type. */
#include "fuzz.h"
#include "request.h"

int
LLVMFuzzerTestOneInput (const uint8_t *data, size_t size)
{
  g_autoptr (GPtrArray) args = g_ptr_array_new_with_free_func (g_free);
  g_ptr_array_add (args, g_strdup ("chandler"));
  size_t start = 0;
  for (size_t i = 0; i <= size; i++)
    if (i == size || data[i] == '\0')
      {
        g_ptr_array_add (args, g_strndup ((const char *) data + start, i - start));
        start = i + 1;
      }
  ChandlerRequest r;
  chandler_request_parse ((int) args->len, (const char *const *) args->pdata, &r);
  if (r.text != nullptr && (!g_utf8_validate (r.text, -1, nullptr) || *r.text == '\0'))
    gbk_fatal ("request text is empty or not UTF-8");
  if (r.unsupported != nullptr && strchr (r.unsupported, '=') != nullptr)
    gbk_fatal ("an unsupported flag kept its value");
  if (r.kind == CHANDLER_REQUEST_TYPE || r.kind == CHANDLER_REQUEST_LIST)
    {
      ChandlerMediaType again;
      if (!chandler_mediatype_parse (r.type.value, &again))
        gbk_fatal ("request type is not a media type");
    }
  chandler_request_clear (&r);
  return 0;
}

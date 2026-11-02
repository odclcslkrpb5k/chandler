#include "pkgname.h"

bool
chandler_pkgname_valid (const char *name)
{
  if (name == nullptr || !(g_ascii_islower (name[0]) || g_ascii_isdigit (name[0])))
    return false;
  size_t n = 1;
  for (; name[n] != '\0'; n++)
    if (!(g_ascii_islower (name[n]) || g_ascii_isdigit (name[n])
          || strchr ("+.-", name[n]) != nullptr))
      return false;
  return n >= 2;
}

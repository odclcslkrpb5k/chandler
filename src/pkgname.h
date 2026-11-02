/* A Debian package name, as the catalog states it (BD-1): [a-z0-9][a-z0-9+.-]+ (Debian
 * Policy 5.6.1). Checked before it reaches PackageKit, where one malformed name would make
 * the daemon refuse the whole resolve, and so every row with it. */
#pragma once

bool chandler_pkgname_valid (const char *name);

/* An application's icon in a results row (plan §3.2): the theme's icon when the catalog
 * names one the theme has, else the catalog's cached PNG read asynchronously, else a
 * generic icon. REMOTE and LOCAL icons never reach here (catalog.c drops them). */
#pragma once

#include "catalog.h"

#define CHANDLER_ICON_SIZE 48

/* A GtkImage showing `app`'s icon; a cached file arrives later, while the image lives. */
GtkWidget *chandler_icon_new (const ChandlerApp *app);

/* For tests: "stock", "cached" (the file has been shown), or "generic". */
const char *chandler_icon_source_for_test (GtkWidget *image);

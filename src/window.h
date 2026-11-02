/* Chandler's one window (plan §3.6): a search entry, a heading, an info bar, and the results
 * (or a sentence saying why there are none). Milestone 3: it finds and shows; installing
 * arrives in milestone 4. */
#pragma once

#include "request.h"

#define CHANDLER_TYPE_WINDOW (chandler_window_get_type ())
G_DECLARE_FINAL_TYPE (ChandlerWindow, chandler_window, CHANDLER, WINDOW, GtkApplicationWindow)

/* The system's catalog. */
ChandlerWindow *chandler_window_new (GtkApplication *app);

/* Tests: only the catalog under `catalog_dir` (chandler_catalog_open_at). */
ChandlerWindow *chandler_window_new_at (GtkApplication *app, const char *catalog_dir);

/* Shows what a launch asked for (EMPTY, TYPE or TEXT; any other kind is a bug), replacing
 * the current search. A second launch's request arrives here too. */
void chandler_window_show_request (ChandlerWindow *self, const ChandlerRequest *request);

/* The search the window last ran, as the entry spelled it ("" for none). Borrowed. */
const char *chandler_window_get_query (const ChandlerWindow *self);

/* For tests: what the window shows, one line each:
 *   heading\t<text>
 *   info\t<text or "">
 *   status\t<text>          (when there are no rows)
 *   row\t<chandler_row_describe_for_test>   (per row, in order) */
char *chandler_window_describe_for_test (ChandlerWindow *self);

/* For tests: puts `text` in the search entry as typing would (the search follows after
 * the typing delay). */
void chandler_window_type_for_test (ChandlerWindow *self, const char *text);

/* For tests: shows at most `limit` rows (ROW_LIMIT otherwise). */
void chandler_window_set_row_limit_for_test (ChandlerWindow *self, guint limit);

/* For tests: activates row `index` as Enter on it would. */
void chandler_window_activate_row_for_test (ChandlerWindow *self, int index);

/* Chandler's one window. Milestone 1: empty; it grows the search entry, results list and
 * info bar in milestone 3. */
#pragma once

#define CHANDLER_TYPE_WINDOW (chandler_window_get_type ())
G_DECLARE_FINAL_TYPE (ChandlerWindow, chandler_window, CHANDLER, WINDOW, GtkApplicationWindow)

ChandlerWindow *chandler_window_new (GtkApplication *app);

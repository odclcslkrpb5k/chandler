/* Which key presses are auto-repeats (plan §3.6). GTK 3's key events don't say, and a held
 * key arrives as presses without releases. chandler_keys_watch follows a window's key events
 * before its focused widget sees them: a press of a key that is already down (no release
 * seen since its last press) is a repeat. A key let go while the window lacked focus stays
 * "down" until its next release, which errs towards calling a press a repeat. */
#pragma once

void chandler_keys_watch (GtkWindow *window);

/* Whether the key press being delivered now to `widget` (in a watched window) is an
 * auto-repeat. False outside a watched window. */
bool chandler_keys_is_repeat (GtkWidget *widget);

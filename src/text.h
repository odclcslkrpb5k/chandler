/* Text from outside (BD-1): AppStream metadata and the command line. */
#pragma once

/* A valid UTF-8 copy of `s` (invalid sequences replaced with U+FFFD); "" for nullptr. */
char *chandler_text_valid (const char *s);

/* A copy of `s` safe to write to a terminal: valid UTF-8 with every control character
 * (C0, including TAB and newline, DEL and C1), format character (bidirectional overrides,
 * zero-width characters) and line or paragraph separator removed, so metadata can't move the
 * cursor, set the title, reorder what is shown or forge a column. */
char *chandler_text_for_terminal (const char *s);

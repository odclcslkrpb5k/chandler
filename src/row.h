/* One application in the results list (plan §3.6): icon, name, summary, package names, and
 * what can be done with it. Text is set as text, never markup (prelude.h), and filtered of
 * controls and invisible characters that rearrange what is shown. */
#pragma once

#include "catalog.h"
#include "packages.h"

typedef enum
{
  CHANDLER_ROW_UNKNOWN,  /* PackageKit couldn't say: nothing is shown */
  CHANDLER_ROW_CHECKING, /* asking PackageKit */
  CHANDLER_ROW_INSTALLED,
  CHANDLER_ROW_AVAILABLE, /* an Install button */
  CHANDLER_ROW_UNAVAILABLE,
  CHANDLER_ROW_AMBIGUOUS,
  CHANDLER_ROW_CONFIRM,    /* the install brings other packages: a second click goes on */
  CHANDLER_ROW_INSTALLING, /* with words saying how far it is */
} ChandlerRowState;

#define CHANDLER_TYPE_ROW (chandler_row_get_type ())
G_DECLARE_FINAL_TYPE (ChandlerRow, chandler_row, CHANDLER, ROW, GtkListBoxRow)

/* Signal "install" (no arguments): the user pressed Install, or confirmed. A key's click
 * counts only when its press was not an auto-repeat (keys.h), so a held Enter never presses
 * it (plan §3.6). The window must be watched by chandler_keys_watch. */
GtkWidget *chandler_row_new (const ChandlerApp *app);

const char *chandler_row_get_id (const ChandlerRow *row);
/* The application's packages, as the catalog names them. Borrowed. */
const char *const *chandler_row_get_pkgnames (const ChandlerRow *row);
ChandlerRowState chandler_row_get_state (const ChandlerRow *row);

void chandler_row_set_state (ChandlerRow *row, ChandlerRowState state);
ChandlerRowState chandler_row_state_from_pkg (ChandlerPkgState state);
/* CONFIRM, saying how many other packages come too. */
void chandler_row_set_confirm (ChandlerRow *row, guint extra, bool updates);
/* INSTALLING, with these words. */
void chandler_row_set_installing (ChandlerRow *row, const char *text);
/* While another row installs, this one's buttons can't be pressed (plan §3.3). */
void chandler_row_set_blocked (ChandlerRow *row, bool blocked);

/* Enter or a click on the row: focus its button, never press it (plan §3.6). */
void chandler_row_focus_action (const ChandlerRow *row);

/* For tests: "<id>\t<state>\t<icon source>\t<name>\t<summary>\t<packages>\t<button>\t<action>",
 * where <button> is "-" without one, else "sensitive" or "insensitive", and "+focus" when
 * focused; <action> is the button's label or the installing words, else "-". */
char *chandler_row_describe_for_test (ChandlerRow *row);
/* For tests: a pointer's click on the shown button. */
void chandler_row_click_for_test (ChandlerRow *row);
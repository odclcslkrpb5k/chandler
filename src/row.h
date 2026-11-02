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
  CHANDLER_ROW_AVAILABLE,
  CHANDLER_ROW_UNAVAILABLE,
  CHANDLER_ROW_AMBIGUOUS,
} ChandlerRowState;

#define CHANDLER_TYPE_ROW (chandler_row_get_type ())
G_DECLARE_FINAL_TYPE (ChandlerRow, chandler_row, CHANDLER, ROW, GtkListBoxRow)

GtkWidget *chandler_row_new (const ChandlerApp *app);

/* The application's packages, as the catalog names them. Borrowed. */
const char *const *chandler_row_get_pkgnames (const ChandlerRow *row);

void chandler_row_set_state (ChandlerRow *row, ChandlerRowState state);
ChandlerRowState chandler_row_state_from_pkg (ChandlerPkgState state);

/* Enter or a click on the row: focus its button, never press it (plan §3.6). */
void chandler_row_focus_action (ChandlerRow *row);

/* For tests: "<id>\t<state>\t<icon source>\t<name>\t<summary>\t<packages>\t<button>", where
 * <button> is "-" without one, else "sensitive" or "insensitive", and "+focus" when
 * focused. */
char *chandler_row_describe_for_test (ChandlerRow *row);

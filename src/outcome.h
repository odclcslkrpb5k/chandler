/* What became of a PackageKit request, in Chandler's terms (plan §3.4). The PackageKit
 * adapter decides the outcome from error domains and codes, never message text (ER-8);
 * this file only says it in words. */
#pragma once

typedef enum
{
  CHANDLER_OUTCOME_OK,
  CHANDLER_OUTCOME_CANCELLED,       /* not an error: the request just stops (ER-10) */
  CHANDLER_OUTCOME_AUTH_INCOMPLETE, /* polkit: dismissed and refused look the same */
  CHANDLER_OUTCOME_NO_NETWORK,
  CHANDLER_OUTCOME_LOCKED,
  CHANDLER_OUTCOME_GONE,
  CHANDLER_OUTCOME_ALREADY,
  CHANDLER_OUTCOME_BROKEN,
  CHANDLER_OUTCOME_NO_SPACE,
  CHANDLER_OUTCOME_WOULD_CHANGE,
  CHANDLER_OUTCOME_UNTRUSTED,
  CHANDLER_OUTCOME_CANNOT_CANCEL,
  CHANDLER_OUTCOME_NO_DAEMON,
  CHANDLER_OUTCOME_OTHER,
} ChandlerOutcome;

/* One sentence for the user; nullptr for OK and CANCELLED, which show nothing. */
const char *chandler_outcome_text (ChandlerOutcome outcome);

/* A stable name for logs and --list ("no-network"). */
const char *chandler_outcome_name (ChandlerOutcome outcome);

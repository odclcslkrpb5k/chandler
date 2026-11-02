#include "outcome.h"

const char *
chandler_outcome_text (ChandlerOutcome outcome)
{
  switch (outcome)
    {
    case CHANDLER_OUTCOME_OK:
    case CHANDLER_OUTCOME_CANCELLED:
    case CHANDLER_OUTCOME_ALREADY: return nullptr;
    case CHANDLER_OUTCOME_AUTH_INCOMPLETE: return "Not installed: authentication didn't complete.";
    case CHANDLER_OUTCOME_NO_NETWORK: return "Couldn't download the package.";
    case CHANDLER_OUTCOME_LOCKED:
      return "Another package manager is running. Close it and try again.";
    case CHANDLER_OUTCOME_GONE: return "No longer available: run \"sudo apt update\".";
    case CHANDLER_OUTCOME_BROKEN:
      return "Your package system needs attention: use Synaptic or apt.";
    case CHANDLER_OUTCOME_NO_SPACE: return "Not enough disk space.";
    case CHANDLER_OUTCOME_WOULD_CHANGE:
      return "This would also change other software: use Synaptic or apt.";
    case CHANDLER_OUTCOME_UNTRUSTED: return "Not signed by a trusted source.";
    case CHANDLER_OUTCOME_CANNOT_CANCEL: return "Too late to cancel.";
    case CHANDLER_OUTCOME_NO_DAEMON:
      return "PackageKit isn't available: Chandler can list apps but not install them.";
    case CHANDLER_OUTCOME_OTHER: return "Something went wrong in PackageKit.";
    }
  gbk_fatal ("unknown outcome %d", (int) outcome);
}

const char *
chandler_outcome_name (ChandlerOutcome outcome)
{
  switch (outcome)
    {
    case CHANDLER_OUTCOME_OK: return "ok";
    case CHANDLER_OUTCOME_CANCELLED: return "cancelled";
    case CHANDLER_OUTCOME_AUTH_INCOMPLETE: return "auth-incomplete";
    case CHANDLER_OUTCOME_NO_NETWORK: return "no-network";
    case CHANDLER_OUTCOME_LOCKED: return "locked";
    case CHANDLER_OUTCOME_GONE: return "gone";
    case CHANDLER_OUTCOME_ALREADY: return "already";
    case CHANDLER_OUTCOME_BROKEN: return "broken";
    case CHANDLER_OUTCOME_NO_SPACE: return "no-space";
    case CHANDLER_OUTCOME_WOULD_CHANGE: return "would-change";
    case CHANDLER_OUTCOME_UNTRUSTED: return "untrusted";
    case CHANDLER_OUTCOME_CANNOT_CANCEL: return "cannot-cancel";
    case CHANDLER_OUTCOME_NO_DAEMON: return "no-daemon";
    case CHANDLER_OUTCOME_OTHER: return "other";
    }
  gbk_fatal ("unknown outcome %d", (int) outcome);
}

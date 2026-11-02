/* What Chandler was asked, parsed once from its command line (BD-1, plan §3.5). GTK's app
 * chooser runs `gnome-software --search=<type>` (the shim makes that Chandler), so
 * gnome-software's flags are understood too; flags Chandler has no use for are ignored. */
#pragma once

#include "mediatype.h"

typedef enum
{
  CHANDLER_REQUEST_EMPTY,   /* the window, with an empty search */
  CHANDLER_REQUEST_TYPE,    /* apps that open `type` */
  CHANDLER_REQUEST_TEXT,    /* apps whose name or summary matches `text` */
  CHANDLER_REQUEST_LIST,    /* --list: `type`'s apps as text on stdout, no window */
  CHANDLER_REQUEST_VERSION, /* --version */
  CHANDLER_REQUEST_USAGE,   /* --help, or a command line that can't be used: `problem` */
} ChandlerRequestKind;

typedef struct
{
  ChandlerRequestKind kind;
  ChandlerMediaType type; /* TYPE, LIST */
  char *text;             /* TEXT: valid UTF-8, never empty */
  char *problem;          /* USAGE: what was wrong, or nullptr for --help */
  /* A gnome-software flag that asks for something Chandler doesn't do (--install=,
   * --local-filename=, ...): the window says so rather than open silently empty. */
  char *unsupported;
} ChandlerRequest;

/* argv[0] is the program. Never fails: a bad command line is a USAGE request. */
void chandler_request_parse (int argc, const char *const *argv, ChandlerRequest *out);
void chandler_request_clear (ChandlerRequest *request);

extern const char chandler_usage[];

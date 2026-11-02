/* `chandler --list <type>`: the apps that open a type, as text, through the same catalog and
 * packages code as the window. The format is for people and is NOT stable: nothing may
 * parse it (plan §3.5). Each line: state, component id, packages, name, summary, separated
 * by tabs; control characters (tabs included) are removed from every field. */
#pragma once

#include "mediatype.h"

/* Exit status: 0 with results, 1 with none, 2 when the catalog couldn't be read. */
int chandler_cli_list (const ChandlerMediaType *type);

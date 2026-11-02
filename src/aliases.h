/* MIME type aliases from shared-mime-info (mime/aliases: one "alias canonical" pair per
 * line). Apps declare either spelling in AppStream: Rhythmbox lists only audio/x-flac, the
 * alias of audio/flac, so a lookup asks for every spelling (plan §3.2). */
#pragma once

#include "mediatype.h"

typedef struct _ChandlerAliases ChandlerAliases;

ChandlerAliases *chandler_aliases_new (void);
void chandler_aliases_free (ChandlerAliases *aliases);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (ChandlerAliases, chandler_aliases_free)

/* Adds the pairs in one aliases file's contents. Lines that are not two media types are
 * skipped and counted; the count is returned. */
unsigned chandler_aliases_add_text (ChandlerAliases *aliases, const char *text, size_t len);

/* `type`, its canonical type and every alias of that canonical type, each once, `type`
 * first. Free with g_strfreev. */
char **chandler_aliases_expand (const ChandlerAliases *aliases, const ChandlerMediaType *type);

/* Reads mime/aliases from the user's and the system's data directories on a worker thread.
 * `done` runs on the main thread while `owner` is alive, with the table (never nullptr: a
 * missing file is no aliases); then the table is freed. */
typedef void (*ChandlerAliasesFunc) (const ChandlerAliases *aliases, GObject *owner, void *data);
void chandler_aliases_load (GObject *owner, ChandlerAliasesFunc done, void *data);

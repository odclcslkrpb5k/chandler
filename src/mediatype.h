/* A media type ("application/pdf"): checked against RFC 6838's restricted-name grammar,
 * without parameters, and lowercased. Only chandler_mediatype_parse builds one (BD-5), so a
 * ChandlerMediaType anywhere in the program is valid. A value, not a pointer: no ownership. */
#pragma once

/* type (127) + "/" + subtype (127) + NUL */
#define CHANDLER_MEDIATYPE_MAX 256

typedef struct
{
  char value[CHANDLER_MEDIATYPE_MAX];
} ChandlerMediaType;

/* False, leaving *out untouched, when `text` is not a media type. */
[[nodiscard]] bool chandler_mediatype_parse (const char *text, ChandlerMediaType *out);

/* "application/octet-stream": what GTK's dialog sends when a file has no known type, and what
 * Quoin sends for several types. Never a useful question (plan §6). */
bool chandler_mediatype_is_octet_stream (const ChandlerMediaType *type);

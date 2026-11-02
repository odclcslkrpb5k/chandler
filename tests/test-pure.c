/* The pure modules: media types, text, the command line, MIME aliases, outcomes. */
#include "aliases.h"
#include "outcome.h"
#include "pkgname.h"
#include "request.h"
#include "text.h"

static void
test_mediatype (void)
{
  ChandlerMediaType t;
  g_assert_true (chandler_mediatype_parse ("application/pdf", &t));
  g_assert_cmpstr (t.value, ==, "application/pdf");
  g_assert_true (chandler_mediatype_parse ("Image/SVG+XML", &t));
  g_assert_cmpstr (t.value, ==, "image/svg+xml");
  g_assert_true (chandler_mediatype_parse ("application/vnd.oasis.opendocument.text", &t));

  const char *const bad[] = {
    "",
    "pdf",
    "/pdf",
    "application/",
    "application//pdf",
    "application/pdf/x",
    "AC/DC band",
    "text/plain; charset=utf-8",
    "-x/y",
    "x/.y",
    "x/y\n",
    "é/x",
    " text/plain",
  };
  for (size_t i = 0; i < G_N_ELEMENTS (bad); i++)
    if (chandler_mediatype_parse (bad[i], &t))
      gbk_fatal ("accepted %s as a media type", bad[i]);
  g_assert_false (chandler_mediatype_parse (nullptr, &t));

  /* 127 characters per name are allowed, 128 are not (RFC 6838). */
  g_autofree char *n127 = g_strnfill (127, 'a');
  g_autofree char *n128 = g_strnfill (128, 'a');
  g_autofree char *ok = g_strdup_printf ("%s/%s", n127, n127);
  g_autofree char *long_type = g_strdup_printf ("%s/x", n128);
  g_autofree char *long_sub = g_strdup_printf ("x/%s", n128);
  g_assert_true (chandler_mediatype_parse (ok, &t));
  g_assert_cmpuint (strlen (t.value), ==, 255);
  g_assert_false (chandler_mediatype_parse (long_type, &t));
  g_assert_false (chandler_mediatype_parse (long_sub, &t));

  g_assert_true (chandler_mediatype_parse ("application/octet-stream", &t));
  g_assert_true (chandler_mediatype_is_octet_stream (&t));
}

static void
test_text (void)
{
  g_autofree char *empty = chandler_text_valid (nullptr);
  g_assert_cmpstr (empty, ==, "");
  g_autofree char *repaired = chandler_text_valid ("a\xff"
                                                   "b");
  g_assert_true (g_utf8_validate (repaired, -1, nullptr));
  g_assert_cmpstr (repaired, ==, "a�b");
  g_autofree char *term
      = chandler_text_for_terminal ("Red \x1b[31mx\x1b[0m\tcol\nline\x7f\u0085 é");
  g_assert_cmpstr (term, ==, "Red [31mx[0mcolline é");
  g_autofree char *bidi = chandler_text_for_terminal ("a\u202eb\u200bc\u2028d\ufeffe\u2067f");
  g_assert_cmpstr (bidi, ==, "abcdef");
}

typedef struct
{
  const char *argv; /* space-separated; "|" stands for an empty argument */
  ChandlerRequestKind kind;
  const char *value; /* the type, the text or the unsupported flag */
} Case;

static void
test_request (void)
{
  const Case cases[] = {
    /* Exactly what GTK 3.24.52 sent from Quoin's dialog (plan §1.2 item 2). */
    { "--search=application/octet-stream", CHANDLER_REQUEST_EMPTY, nullptr },
    { "--search=image/svg+xml", CHANDLER_REQUEST_TYPE, "image/svg+xml" },
    { "--search=application/pdf", CHANDLER_REQUEST_TYPE, "application/pdf" },
    { "", CHANDLER_REQUEST_EMPTY, nullptr },
    { "--mode=overview", CHANDLER_REQUEST_EMPTY, nullptr },
    { "--mode overview", CHANDLER_REQUEST_EMPTY, nullptr },
    { "application/pdf", CHANDLER_REQUEST_TYPE, "application/pdf" },
    { "--search=AC/DC", CHANDLER_REQUEST_TYPE, "ac/dc" },
    { "--search=viewer", CHANDLER_REQUEST_TEXT, "viewer" },
    { "--search=application/pdf evince", CHANDLER_REQUEST_TEXT, "evince" },
    { "--search |", CHANDLER_REQUEST_EMPTY, nullptr },
    { "--search", CHANDLER_REQUEST_USAGE, nullptr },
    { "--list application/pdf", CHANDLER_REQUEST_LIST, "application/pdf" },
    { "--list=text/markdown", CHANDLER_REQUEST_LIST, "text/markdown" },
    { "--list pdf", CHANDLER_REQUEST_USAGE, nullptr },
    { "--version", CHANDLER_REQUEST_VERSION, nullptr },
    { "--help", CHANDLER_REQUEST_USAGE, nullptr },
    { "--install=/home/someone/x.deb", CHANDLER_REQUEST_EMPTY, "--install" },
    { "--local-filename=/tmp/x.deb", CHANDLER_REQUEST_EMPTY, "--local-filename" },
    { "--mode=updates", CHANDLER_REQUEST_EMPTY, "--mode" },
    { "--verbose --search=text/markdown", CHANDLER_REQUEST_TYPE, "text/markdown" },
    { "-- --version", CHANDLER_REQUEST_TEXT, "--version" },
    /* The space form's value is consumed, never searched for (it is often a path). */
    { "--local-filename /home/someone/x.deb", CHANDLER_REQUEST_EMPTY, "--local-filename" },
    { "--install some.app.Id --search=text/markdown", CHANDLER_REQUEST_TYPE, "text/markdown" },
    { "--prefer-local application/pdf", CHANDLER_REQUEST_TYPE, "application/pdf" },
    { "--details", CHANDLER_REQUEST_EMPTY, "--details" },
    /* --list stands alone. */
    { "--list application/pdf x", CHANDLER_REQUEST_USAGE, nullptr },
    { "--search=x --list application/pdf", CHANDLER_REQUEST_USAGE, nullptr },
  };
  for (size_t i = 0; i < G_N_ELEMENTS (cases); i++)
    {
      g_autofree char *line = g_strconcat ("chandler ", cases[i].argv, nullptr);
      g_auto (GStrv) argv = g_strsplit (line, " ", -1);
      int argc = (int) g_strv_length (argv);
      if (*cases[i].argv == '\0')
        argc = 1;
      for (int k = 0; k < argc; k++)
        if (g_str_equal (argv[k], "|"))
          argv[k][0] = '\0';
      ChandlerRequest r;
      chandler_request_parse (argc, (const char *const *) argv, &r);
      if (r.kind != cases[i].kind)
        gbk_fatal ("%s: kind %d, expected %d", line, (int) r.kind, (int) cases[i].kind);
      const char *got = nullptr;
      switch (r.kind)
        {
        case CHANDLER_REQUEST_TYPE:
        case CHANDLER_REQUEST_LIST: got = r.type.value; break;
        case CHANDLER_REQUEST_TEXT: got = r.text; break;
        case CHANDLER_REQUEST_EMPTY: got = r.unsupported; break;
        case CHANDLER_REQUEST_VERSION:
        case CHANDLER_REQUEST_USAGE: break;
        }
      if (cases[i].value != nullptr)
        g_assert_cmpstr (got, ==, cases[i].value);
      chandler_request_clear (&r);
    }
}

static void
test_request_usage_problem (void)
{
  const char *const argv[] = { "chandler", "--list", "pdf" };
  ChandlerRequest r;
  chandler_request_parse (3, argv, &r);
  g_assert_cmpint (r.kind, ==, CHANDLER_REQUEST_USAGE);
  g_assert_nonnull (r.problem);
  chandler_request_clear (&r);
  const char *const help[] = { "chandler", "--help" };
  chandler_request_parse (2, help, &r);
  g_assert_null (r.problem);
  chandler_request_clear (&r);
}

static void
test_aliases (void)
{
  g_autofree char *path = g_build_filename (g_getenv ("CHANDLER_SOURCE_ROOT"), "tests", "data",
                                            "mime", "aliases", nullptr);
  g_autofree char *text = nullptr;
  gsize len = 0;
  g_autoptr (GError) error = nullptr;
  if (!g_file_get_contents (path, &text, &len, &error))
    gbk_fatal ("aliases fixture: %s", error->message);
  g_autoptr (ChandlerAliases) aliases = chandler_aliases_new ();
  g_assert_cmpuint (chandler_aliases_add_text (aliases, text, len), ==, 0);
  g_assert_cmpuint (chandler_aliases_add_text (aliases, "one\nx/y z/w q/r\n#c\n", 17), ==, 2);

  ChandlerMediaType flac, xflac, plain;
  g_assert_true (chandler_mediatype_parse ("audio/flac", &flac));
  g_assert_true (chandler_mediatype_parse ("audio/x-flac", &xflac));
  g_assert_true (chandler_mediatype_parse ("text/plain", &plain));
  g_auto (GStrv) from_canonical = chandler_aliases_expand (aliases, &flac);
  g_auto (GStrv) from_alias = chandler_aliases_expand (aliases, &xflac);
  g_assert_cmpstr (from_canonical[0], ==, "audio/flac");
  g_assert_true (g_strv_contains ((const char *const *) from_canonical, "audio/x-flac"));
  g_assert_cmpstr (from_alias[0], ==, "audio/x-flac");
  g_assert_true (g_strv_contains ((const char *const *) from_alias, "audio/flac"));
  /* Each spelling once. */
  for (char **p = from_alias; *p != nullptr; p++)
    for (char **q = p + 1; *q != nullptr; q++)
      g_assert_cmpstr (*p, !=, *q);
  g_auto (GStrv) none = chandler_aliases_expand (aliases, &plain);
  g_assert_cmpstr (none[0], ==, "text/plain");
}

static void
test_pkgname (void)
{
  const char *const good[] = { "evince", "libreoffice-draw", "g++", "python3.13", "0ad", "x2" };
  const char *const bad[]
      = { "", "a", "Evince", "-x", "+x", "foo bar", "a$b", "x_y", "\u00e9t\u00e9" };
  for (size_t i = 0; i < G_N_ELEMENTS (good); i++)
    if (!chandler_pkgname_valid (good[i]))
      gbk_fatal ("refused package name %s", good[i]);
  for (size_t i = 0; i < G_N_ELEMENTS (bad); i++)
    if (chandler_pkgname_valid (bad[i]))
      gbk_fatal ("accepted package name %s", bad[i]);
  g_assert_false (chandler_pkgname_valid (nullptr));
}

static void
test_outcomes (void)
{
  for (int o = CHANDLER_OUTCOME_OK; o <= CHANDLER_OUTCOME_OTHER; o++)
    {
      g_assert_nonnull (chandler_outcome_name ((ChandlerOutcome) o));
      const char *text = chandler_outcome_text ((ChandlerOutcome) o);
      bool silent = o == CHANDLER_OUTCOME_OK || o == CHANDLER_OUTCOME_CANCELLED
                    || o == CHANDLER_OUTCOME_ALREADY;
      g_assert_true ((text == nullptr) == silent);
    }
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, nullptr);
  g_test_add_func ("/pure/mediatype", test_mediatype);
  g_test_add_func ("/pure/text", test_text);
  g_test_add_func ("/pure/request", test_request);
  g_test_add_func ("/pure/request-usage-problem", test_request_usage_problem);
  g_test_add_func ("/pure/aliases", test_aliases);
  g_test_add_func ("/pure/pkgname", test_pkgname);
  g_test_add_func ("/pure/outcomes", test_outcomes);
  return g_test_run ();
}

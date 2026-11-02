#include "request.h"
#include "text.h"

const char chandler_usage[]
    = "Usage: chandler [MEDIA-TYPE | --search=TERM | --list MEDIA-TYPE | --version]\n"
      "Finds the applications that can open a file type and installs the one you choose.\n";

/* gnome-software options that ask for things Chandler doesn't do. */
/* gnome-software options that ask for things Chandler doesn't do, and whether each takes a
 * value. The value is consumed in either spelling (--install=ID, --install ID): left behind,
 * the space form's value (often a path) would become a text search. */
static const struct
{
  const char *name;
  bool takes_value;
} unsupported_flags[] = {
  { "--install", true },       { "--local-filename", true }, { "--details", true },
  { "--details-pkg", true },   { "--interaction", true },    { "--show-metainfo", true },
  { "--prefer-local", false },
};

static void
reset (ChandlerRequest *r, ChandlerRequestKind kind)
{
  g_clear_pointer (&r->text, g_free);
  g_clear_pointer (&r->problem, g_free);
  r->kind = kind;
}

static void
usage (ChandlerRequest *r, char *problem)
{
  reset (r, CHANDLER_REQUEST_USAGE);
  r->problem = problem;
}

/* A search term: a media type is a type lookup, anything else a text search. */
static void
search (ChandlerRequest *r, const char *term)
{
  ChandlerMediaType type;
  if (chandler_mediatype_parse (term, &type))
    {
      reset (r, chandler_mediatype_is_octet_stream (&type) ? CHANDLER_REQUEST_EMPTY
                                                           : CHANDLER_REQUEST_TYPE);
      r->type = type;
      return;
    }
  g_autofree char *valid = chandler_text_valid (term);
  g_strstrip (valid);
  reset (r, *valid == '\0' ? CHANDLER_REQUEST_EMPTY : CHANDLER_REQUEST_TEXT);
  if (r->kind == CHANDLER_REQUEST_TEXT)
    r->text = g_steal_pointer (&valid);
}

/* "--name=value" or "--name value": the value, advancing *i past it; nullptr if `arg` is not
 * this option. *missing is set when the option is there without a value. */
static const char *
option_value (const char *name, const char *const *argv, int argc, int *i, bool *missing)
{
  const char *arg = argv[*i];
  size_t n = strlen (name);
  if (strncmp (arg, name, n) != 0)
    return nullptr;
  if (arg[n] == '=')
    return arg + n + 1;
  if (arg[n] != '\0')
    return nullptr;
  if (*i + 1 >= argc || argv[*i + 1] == nullptr)
    {
      *missing = true;
      return nullptr;
    }
  (*i)++;
  return argv[*i];
}

/* The flag's name alone: its value may be a path (--local-filename=), which is the user's
 * business and never belongs in a log or a message that may be pasted somewhere (DP-3). */
static void
note_unsupported (ChandlerRequest *r, const char *arg)
{
  if (r->unsupported == nullptr)
    r->unsupported = g_strndup (arg, strcspn (arg, "="));
}

/* The entry for `arg` ("--install" or "--install=..."), or -1. */
static int
unsupported_index (const char *arg)
{
  for (size_t k = 0; k < G_N_ELEMENTS (unsupported_flags); k++)
    {
      size_t n = strlen (unsupported_flags[k].name);
      if (strncmp (arg, unsupported_flags[k].name, n) == 0 && (arg[n] == '\0' || arg[n] == '='))
        return (int) k;
    }
  return -1;
}

void
chandler_request_parse (int argc, const char *const *argv, ChandlerRequest *out)
{
  *out = (ChandlerRequest) { .kind = CHANDLER_REQUEST_EMPTY };
  bool options = true;
  int requests = 0; /* arguments that say what to show: --list must be the only one */
  bool list = false;
  for (int i = 1; i < argc; i++)
    {
      const char *arg = argv[i];
      if (arg == nullptr)
        return; /* an argv shorter than argc: what came before stands */
      bool missing = false;
      const char *value;
      if (out->kind == CHANDLER_REQUEST_USAGE || out->kind == CHANDLER_REQUEST_VERSION)
        return;
      int unsupported;
      if (!options || arg[0] != '-' || arg[1] == '\0')
        {
          search (out, arg);
          requests++;
          continue;
        }
      if (g_str_equal (arg, "--"))
        options = false;
      else if (g_str_equal (arg, "--version"))
        reset (out, CHANDLER_REQUEST_VERSION);
      else if (g_str_equal (arg, "--help") || g_str_equal (arg, "-h"))
        usage (out, nullptr);
      else if ((value = option_value ("--list", argv, argc, &i, &missing)) != nullptr)
        {
          ChandlerMediaType type;
          if (!chandler_mediatype_parse (value, &type))
            usage (out, g_strdup ("--list needs a media type, such as application/pdf"));
          else
            {
              reset (out, CHANDLER_REQUEST_LIST);
              out->type = type;
            }
          list = true;
          requests++;
        }
      else if ((value = option_value ("--search", argv, argc, &i, &missing)) != nullptr)
        {
          search (out, value);
          requests++;
        }
      else if ((value = option_value ("--mode", argv, argc, &i, &missing)) != nullptr)
        {
          requests++;
          if (g_str_equal (value, "overview"))
            reset (out, CHANDLER_REQUEST_EMPTY);
          else
            note_unsupported (out, arg);
        }
      else if ((unsupported = unsupported_index (arg)) >= 0)
        {
          note_unsupported (out, arg);
          /* Consume the space form's value; a missing one is no reason to refuse. */
          if (unsupported_flags[unsupported].takes_value && strchr (arg, '=') == nullptr
              && i + 1 < argc)
            i++;
        }
      else if (missing)
        usage (out, g_strdup_printf ("%s needs a value", arg));
      else
        g_debug ("ignoring an unknown option");
    }
  if (list && requests > 1 && out->kind != CHANDLER_REQUEST_USAGE
      && out->kind != CHANDLER_REQUEST_VERSION)
    usage (out, g_strdup ("--list takes a media type and nothing else"));
}

void
chandler_request_clear (ChandlerRequest *request)
{
  reset (request, CHANDLER_REQUEST_EMPTY);
  g_clear_pointer (&request->unsupported, g_free);
}

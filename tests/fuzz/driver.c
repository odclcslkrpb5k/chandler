/* Replays every file in the directories given through LLVMFuzzerTestOneInput, so seed
 * corpora run under the sanitizers as ordinary tests. Fails on an empty corpus (TS-6). */
#include "fuzz.h"

static int
run_file (const char *path)
{
  g_autofree char *data = nullptr;
  gsize len = 0;
  g_autoptr (GError) error = nullptr;
  if (!g_file_get_contents (path, &data, &len, &error))
    {
      fprintf (stderr, "%s\n", error->message);
      return 1;
    }
  LLVMFuzzerTestOneInput ((const uint8_t *) data, len);
  return 0;
}

int
main (int argc, char **argv)
{
  int files = 0;
  for (int i = 1; i < argc; i++)
    {
      g_autoptr (GError) error = nullptr;
      g_autoptr (GDir) dir = g_dir_open (argv[i], 0, &error);
      if (dir == nullptr)
        {
          fprintf (stderr, "%s\n", error->message);
          return 1;
        }
      const char *name;
      while ((name = g_dir_read_name (dir)) != nullptr)
        {
          g_autofree char *path = g_build_filename (argv[i], name, nullptr);
          if (run_file (path) != 0)
            return 1;
          files++;
        }
    }
  if (files == 0)
    {
      fprintf (stderr, "no seed files: nothing was replayed\n");
      return 1;
    }
  printf ("replayed %d seed files\n", files);
  return 0;
}

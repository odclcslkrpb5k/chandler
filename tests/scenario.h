/* The main() of a test binary run by tests/with-fake-pk.py: one case per scenario file,
 * each against its own fake PackageKit on a private bus (plan §4.3).
 *
 *   <binary> --scenarios    lists the cases (the runner requires them == the files)
 *   <binary> <case>         runs one */
#pragma once

typedef struct
{
  const char *name;
  void (*run) (void);
} ScenarioCase;

static int
scenario_main (int argc, char **argv, const ScenarioCase *cases, size_t n)
{
  if (argc == 2 && g_str_equal (argv[1], "--scenarios"))
    {
      for (size_t i = 0; i < n; i++)
        printf ("%s\n", cases[i].name);
      return 0;
    }
  if (argc != 2)
    gbk_fatal ("usage: %s --scenarios | <case> (run by with-fake-pk.py)", argv[0]);
  /* TS-6: assert what was selected. Only the runner's private bus, with the fake for this
   * very case, may answer. */
  const char *fake = g_getenv ("CHANDLER_FAKE_PK");
  const char *bus = g_getenv ("DBUS_SYSTEM_BUS_ADDRESS");
  if (fake == nullptr || !g_str_equal (fake, argv[1]) || bus == nullptr
      || strstr (bus, "/run/dbus") != nullptr)
    gbk_fatal ("not started by with-fake-pk.py for this case");
  /* The scenarios are recordings from an amd64 machine: elsewhere every native candidate
   * in them is foreign, and the cases would fail for that reason, not a real one. */
  if (!g_str_equal (CHANDLER_NATIVE_ARCH, "amd64"))
    gbk_fatal ("the PackageKit scenarios are amd64 recordings; this build is %s",
               CHANDLER_NATIVE_ARCH);
  for (size_t i = 0; i < n; i++)
    if (g_str_equal (cases[i].name, argv[1]))
      {
        cases[i].run ();
        return 0;
      }
  gbk_fatal ("no case %s", argv[1]);
}

test_env = environment()
# Any GLib warning or critical (e.g. a failed g_return_if_fail) aborts the test.
test_env.set('G_DEBUG', 'fatal-warnings,fatal-criticals,gc-friendly')
test_env.set('G_ENABLE_DIAGNOSTIC', '1')
# No OpenGL: Mesa's GLX setup, when GDK probes it, leaks allocations LSan would report
# (fontconfig stays the only suppression).
test_env.set('GDK_GL', 'disable')
# No a11y bus or dconf: results must not depend on the machine (also set by start_x, which
# is skipped when DISPLAY is already set).
test_env.set('NO_AT_BRIDGE', '1')
test_env.set('GTK_A11Y', 'none')
test_env.set('GSETTINGS_BACKEND', 'memory')
# A callback of ours holding the main loop longer than this aborts (lifetime.h).
# tests/test-contract.c fails if it is unset or different.
test_env.set('GBK_STALL_MS', '250')
test_env.set('ASAN_OPTIONS', 'detect_leaks=1:abort_on_error=1:strict_string_checks=1:detect_stack_use_after_return=1')
test_env.set('UBSAN_OPTIONS', 'halt_on_error=1:print_stacktrace=1')
test_env.set('LSAN_OPTIONS', 'suppressions=' + meson.current_source_dir() / 'lsan.supp' + ':print_suppressions=0')
# Hermetic: never read the invoking user's real config, data or cache.
test_env.set('XDG_CONFIG_HOME', '/nonexistent/config')
test_env.set('XDG_DATA_HOME', '/nonexistent/data')
test_env.set('XDG_CACHE_HOME', '/nonexistent/cache')
test_env.set('CHANDLER_SOURCE_ROOT', meson.project_source_root())
test_env.set('CHANDLER_ARTIFACTS', meson.project_source_root() / 'out' / 'artifacts')

test_inc = [config_inc, core_inc]

# Vendored from Quoin, built and run under Chandler's flags (CP-2): the copy is proven
# here, not only in Quoin.
vendored_tests = {'lifetime' : meson.project_source_root() / 'vendor' / 'quoin' / 'tests' / 'test-lifetime.c'}
unit_tests = ['contract']
gui_tests = ['window']

foreach name, source : vendored_tests
  exe = executable('test-' + name, source,
    include_directories : test_inc,
    link_with : [lifetime_lib],
    dependencies : deps,
  )
  test(name, exe, env : test_env, protocol : 'tap', args : ['--tap'], timeout : 120)
endforeach
foreach name : unit_tests
  exe = executable('test-' + name, 'test-' + name + '.c',
    include_directories : test_inc,
    link_with : [lifetime_lib],
    dependencies : deps,
  )
  test(name, exe, env : test_env, protocol : 'tap', args : ['--tap'], timeout : 120)
endforeach
foreach name : gui_tests
  exe = executable('test-' + name, 'test-' + name + '.c',
    include_directories : test_inc,
    link_with : [gui_lib, lifetime_lib],
    dependencies : deps,
  )
  # One at a time: they share the Xvfb display.
  test(name, exe, env : test_env, protocol : 'tap', args : ['--tap'], timeout : 120,
       is_parallel : false)
endforeach

# The binary reports the version meson was configured with (the git hash).
test('version', find_program('python3'),
  args : [meson.current_source_dir() / 'check-version.py', chandler_exe, meson.project_version()],
  env : test_env)

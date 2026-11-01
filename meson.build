project('chandler', 'c',
  # The git hash, written by tools/in-container.sh before every setup. One source of truth:
  # meson's summary, --version and the binary all read it.
  version : files('out/VERSION'),
  meson_version : '>= 1.3',
  default_options : [
    'c_std=gnu23',
    'warning_level=3',
    'werror=true',
    'b_pie=true',
  ],
)

cc = meson.get_compiler('c')

# Toolchain is pinned by the Docker image, so every flag here is required, not probed:
# an unsupported flag should fail the build rather than silently weaken it.
assert(get_option('werror'), 'every warning is an error: the fixtures below rely on it')

# Warnings beyond warning_level=3 (-Wall -Wextra -Wpedantic). Each has a fixture below that
# must fail with the list and compile without that one flag, so a flag that does nothing
# (a default, a duplicate, dead under C23) can't sit here looking like protection.
warning_flags = [
  '-Wformat=2',
  '-Wconversion',                  # in C, includes -Wsign-conversion
  '-Wshadow',
  '-Wvla',
  '-Wnull-dereference',
  '-Wswitch-enum',                 # a `default` doesn't excuse a missing enum value
  '-Wmissing-prototypes',
  '-Wcast-qual',
  '-Wwrite-strings',
  '-Wundef',
  '-Wdouble-promotion',
]
# GCC is the compiler for every shipped and tested build. Clang builds only the analysis
# database (tools/analysis.py).
gcc_warning_flags = [
  '-Wjump-misses-init',            # goto/switch skipping a g_auto* initialisation
  '-Wcast-align=strict',
  '-Wtrampolines',
  '-Wduplicated-cond', '-Wduplicated-branches', '-Wlogical-op',
  '-Wsuggest-attribute=format',    # our own printf-like functions are G_GNUC_PRINTF
]
# Clang's -Wextra, unlike GCC's, leaves out -Wimplicit-fallthrough.
clang_warning_flags = ['-Wimplicit-fallthrough']
if cc.get_id() == 'gcc'
  warning_flags += gcc_warning_flags
elif cc.get_id() == 'clang'
  warning_flags += clang_warning_flags
endif

add_project_arguments(
  # Every translation unit starts with the prelude (system headers, then poisoned APIs).
  '-include', meson.project_source_root() / 'src' / 'prelude.h',
  warning_flags,

  # Semantics: never let the optimiser exploit UB we might have.
  '-fno-strict-aliasing',
  '-fno-strict-overflow',
  '-fno-delete-null-pointer-checks',
  '-fstrict-flex-arrays=3',

  '-D_GNU_SOURCE',
  '-DG_LOG_DOMAIN="chandler"',
  '-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_88',
  '-DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_88',
  '-DGDK_VERSION_MIN_REQUIRED=GDK_VERSION_3_24',
  '-DGDK_VERSION_MAX_ALLOWED=GDK_VERSION_3_24',
  '-DGDK_DISABLE_DEPRECATED',
  '-DGTK_DISABLE_DEPRECATED',
  language : 'c',
)

# Prove the warnings are honoured. cc.compiles() leaves out warning_level's flags, so they
# are spelled out (warning_level=3); -O2 because some warnings come from optimisation passes.
assert(get_option('warning_level') == '3')
fixture_base = ['-Wall', '-Wextra', '-Wpedantic', '-O2', '-Werror']
if cc.get_id() == 'gcc'
  flag_fixtures = {
    '-Wformat=2' : '#include <stdio.h>\nvoid f(const char *fmt);\nvoid f(const char *fmt) { printf(fmt, 1); }',
    '-Wconversion' : 'int f(long x);\nint f(long x) { return x; }',
    '-Wshadow' : 'int f(int x);\nint f(int x) { if (x) { int x = 2; return x; } return 0; }',
    '-Wvla' : 'int f(int n);\nint f(int n) { int a[n]; a[0] = 1; return a[0]; }',
    '-Wnull-dereference' : 'int f(int *p);\nint f(int *p) { if (p) return 0; return *p; }',
    '-Wswitch-enum' : 'enum e { A, B };\nint f(enum e x);\nint f(enum e x) { switch (x) { case A: return 1; default: return 0; } }',
    '-Wmissing-prototypes' : 'int f(void) { return 0; }',
    '-Wcast-qual' : 'char *f(const char *s);\nchar *f(const char *s) { return (char *) s; }',
    '-Wwrite-strings' : 'char *f(void);\nchar *f(void) { return "x"; }',
    '-Wundef' : '#if FOO\nint x;\n#endif\nint y;',
    '-Wdouble-promotion' : 'double f(float x);\ndouble f(float x) { return x * 2.0; }',
    '-Wsuggest-attribute=format' : '#include <stdarg.h>\n#include <stdio.h>\nvoid f(const char *fmt, ...);\nvoid f(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); }',
    '-Wjump-misses-init' : 'int f(int x);\nint f(int x) { if (x) goto out; int y = 2; return y; out: return 0; }',
    '-Wcast-align=strict' : 'int *f(char *p);\nint *f(char *p) { return (int *) p; }',
    '-Wtrampolines' : '#pragma GCC diagnostic ignored "-Wpedantic"\nint f(int x);\nint f(int x) { int g(void) { return x; } int (*h)(void) = g; return h(); }',
    '-Wduplicated-cond' : 'int f(int x);\nint f(int x) { if (x == 1) return 1; else if (x == 1) return 2; return 0; }',
    '-Wduplicated-branches' : 'int f(int x);\nint f(int x) { int r; if (x) r = 1; else r = 1; return r; }',
    '-Wlogical-op' : 'int f(int x);\nint f(int x) { return x == 1 && x == 1; }',
  }
  foreach flag : warning_flags
    if flag not in flag_fixtures
      error('warning flag ' + flag + ' has no fixture in meson.build')
    endif
    code = flag_fixtures[flag]
    others = []
    foreach f : warning_flags
      if f != flag
        others += f
      endif
    endforeach
    if cc.compiles(code, args : fixture_base + warning_flags, name : 'fixture ' + flag + ' fails')
      error('the fixture for ' + flag + ' compiles: the flag is not honoured')
    endif
    if not cc.compiles(code, args : fixture_base + others, name : 'fixture ' + flag + ' needs it')
      error(flag + ' does nothing: its fixture fails without it (a default or another flag)')
    endif
  endforeach
  # Protections that come from defaults or from the flags above, not from a flag of their
  # own (on GCC 15 with C23 these were explicit flags once, and did nothing).
  rejected = {
    'implicit declaration' : 'int f(void);\nint f(void) { return g(); }',
    'int conversion' : 'int *f(int x);\nint *f(int x) { return x; }',
    'incompatible pointers' : 'int *f(long *p);\nint *f(long *p) { return p; }',
    'sign conversion' : 'unsigned f(int x);\nunsigned f(int x) { return x; }',
    'implicit fallthrough' : 'int f(int x);\nint f(int x) { int r = 0; switch (x) { case 1: r = 1; case 2: r += 2; break; default: break; } return r; }',
    'format security' : '#include <stdio.h>\nvoid f(const char *s);\nvoid f(const char *s) { printf(s); }',
    'pointer arithmetic on void' : 'void *f(void *p);\nvoid *f(void *p) { return p + 1; }',
    'paired bidi controls' : 'int f(void);\nint f(void) { return 0; } /* a \u202a b \u202c c */',
    'directional mark' : 'int f(void);\nint f(void) { return 0; } /* a \u200f b */',
  }
  foreach what, code : rejected
    if cc.compiles(code, args : fixture_base + warning_flags, name : 'rejects ' + what)
      error('a fixture compiles that must not: ' + what)
    endif
  endforeach
endif

if get_option('hardening')
  # OpenSSF Compiler Options Hardening Guide (2026-08-20), production set for x86_64.
  # Not used in sanitizer builds: FORTIFY interferes with ASan. (-Wl,-z,nodlopen is left
  # out: ld emits no NOOPEN flag for a PIE executable, so it would protect nothing.)
  add_project_arguments(
    '-U_FORTIFY_SOURCE', '-D_FORTIFY_SOURCE=3',
    '-fstack-clash-protection', '-fstack-protector-strong',
    '-fcf-protection=full',
    '-fzero-call-used-regs=used-gpr',
    '-ftrivial-auto-var-init=zero',
    '-fzero-init-padding-bits=all',
    language : 'c',
  )
  add_project_link_arguments(
    '-Wl,-z,noexecstack',
    '-Wl,-z,relro', '-Wl,-z,now',
    '-Wl,--as-needed', '-Wl,--no-copy-dt-needed-entries',
    language : 'c',
  )
endif

if get_option('analyzer')
  # GBK_ANALYZER: vendor/quoin/src/analyzer-glib.h tells the analyzer GLib's allocators.
  add_project_arguments('-fanalyzer', '-DGBK_ANALYZER', '-Wno-analyzer-malloc-leak',
                        language : 'c')
else
  # OpenSSF: -fexceptions so pthread cancellation unwinds through C frames. Cancellation is
  # banned outright (prelude.h), so nothing can unwind; the analyzer build leaves it out
  # because with it, -fanalyzer reports every resource as leaked on unwind paths that
  # cannot happen here.
  add_project_arguments('-fexceptions', language : 'c')
endif

conf = configuration_data()
conf.set_quoted('CHANDLER_VERSION', meson.project_version())
configure_file(output : 'chandler-config.h', configuration : conf)
config_inc = include_directories('.', 'vendor/quoin/src')

glib_dep = dependency('glib-2.0', version : '>= 2.88')
gobject_dep = dependency('gobject-2.0', version : '>= 2.88')
gio_dep = dependency('gio-2.0', version : '>= 2.88')
gtk_dep = dependency('gtk+-3.0', version : '>= 3.24.52')
appstream_dep = dependency('appstream', version : '>= 1.1.2')
packagekit_dep = dependency('packagekit-glib2', version : '>= 1.3.4')
deps = [glib_dep, gobject_dep, gio_dep, gtk_dep, appstream_dep, packagekit_dep]

# Prove the bans in prelude.h actually bite. A ban that silently stopped working (prelude
# no longer force-included, macro not #undef'd, ...) fails the configure step here.
probe_args = ['-include', meson.project_source_root() / 'src' / 'prelude.h',
              '-I' + meson.project_build_root(),
              '-I' + meson.project_source_root() / 'vendor' / 'quoin' / 'src', '-Werror']
if not cc.compiles('int f(void); int f(void) { return 0; }',
                   args : probe_args, dependencies : deps, name : 'prelude baseline')
  error('prelude.h baseline probe does not compile; ban probes would be meaningless')
endif
# Every poisoned name is read from the prelude and probed, so a ban can't be added, garbled
# or dropped without configure noticing. A function is probed by taking its address; a
# macro (which the prelude #undefs before poisoning) needs a use of its own, below.
fs = import('fs')
poisoned = []
foreach line : fs.read('src' / 'prelude.h').split('\n')
  if line.strip().startswith('#pragma GCC poison ')
    poisoned += line.strip().substring(19).split()
  endif
endforeach
# A floor, so a parse that silently finds nothing (or a prelude gutted by mistake) fails.
assert(poisoned.length() >= 170, 'only @0@ poisoned names parsed from prelude.h'.format(poisoned.length()))
macro_probes = {
  'g_error' : 'g_error("x");',
  'alloca' : 'char *p = alloca(4); p[0] = 0;',
  'g_alloca' : 'char *p = g_alloca(4); p[0] = 0;',
  'g_alloca0' : 'char *p = g_alloca0(4); p[0] = 0;',
  'g_newa' : 'char *p = g_newa(char, 4); p[0] = 0;',
  'g_newa0' : 'char *p = g_newa0(char, 4); p[0] = 0;',
  'g_signal_connect' : 'g_signal_connect(nullptr, "x", nullptr, nullptr);',
  'g_signal_connect_after' : 'g_signal_connect_after(nullptr, "x", nullptr, nullptr);',
  'g_signal_connect_swapped' : 'g_signal_connect_swapped(nullptr, "x", nullptr, nullptr);',
  'PK_TYPE_TASK' : '(void) PK_TYPE_TASK;',
}
# Banned in case a header ever declares them again; nothing declares them now, so there is
# nothing to probe.
unprobeable = {
  'gets' : 'removed from C (C11); glibc no longer declares it under -std=gnu23',
}
foreach name, _ : macro_probes
  assert(name in poisoned, 'macro_probes names ' + name + ', which prelude.h does not poison')
endforeach
foreach name, _ : unprobeable
  assert(name in poisoned, 'unprobeable names ' + name + ', which prelude.h does not poison')
endforeach
foreach name : poisoned
  if name in unprobeable
    continue
  endif
  code = name in macro_probes ? macro_probes[name] : '(void) &' + name + ';'
  probe = 'void f(void); void f(void) { ' + code + ' }'
  # A probe that fails for another reason (a misspelt name, a wrong argument count) would
  # otherwise "prove" its ban forever. CHANDLER_PRELUDE_NO_BANS exists only for this;
  # tools/check-source.py refuses it elsewhere.
  if not cc.compiles(probe, args : probe_args + ['-DCHANDLER_PRELUDE_NO_BANS'],
                     dependencies : deps, name : 'probe ' + name + ' is valid')
    error('the probe for banned "' + name + '" does not compile even without the ban')
  endif
  if cc.compiles(probe, args : probe_args, dependencies : deps, name : 'ban ' + name)
    error('banned "' + name + '" compiles; the prelude ban is not working')
  endif
endforeach

if cc.get_id() == 'clang'
  fallthrough = 'int f(int x);\nint f(int x) { int r = 0; switch (x) { case 1: r = 1; case 2: r += 2; break; default: break; } return r; }'
  if cc.compiles(fallthrough, args : fixture_base + warning_flags, name : 'clang fixture -Wimplicit-fallthrough fails')
    error('the fixture for -Wimplicit-fallthrough compiles under clang: the flag is not honoured')
  endif
  if not cc.compiles(fallthrough, args : fixture_base, name : 'clang fixture -Wimplicit-fallthrough needs it')
    error('-Wimplicit-fallthrough does nothing under clang: its fixture fails without it')
  endif
endif

foreach macro : ['G_DISABLE_ASSERT', 'G_DISABLE_CHECKS', 'G_DISABLE_CAST_CHECKS']
  if cc.compiles('int f(void); int f(void) { return 0; }', args : probe_args + ['-D' + macro],
                 dependencies : deps, name : 'refuses ' + macro)
    error(macro + ' compiles; the prelude must refuse it')
  endif
endforeach

subdir('src')
subdir('tests')

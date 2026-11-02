# vendor/quoin/src/lifetime.c is the only file allowed the raw signal/source APIs (prelude.h).
# GBK_LIFETIME_IMPL is set for it and nothing else (tools/check-source.py, rule nobans).
lifetime_lib = static_library('gbk-lifetime',
  meson.project_source_root() / 'vendor' / 'quoin' / 'src' / 'lifetime.c',
  c_args : '-DGBK_LIFETIME_IMPL',
  include_directories : config_inc,
  dependencies : deps,
)

# Pure: no main loop, no GTK, no library adapters. Fuzzed.
pure_lib = static_library('chandler-pure',
  'mediatype.c',
  'pkgname.c',
  'text.c',
  'request.c',
  'outcome.c',
  include_directories : config_inc,
  dependencies : deps,
)

# Everything below the window: async completion, aliases, and the two adapters.
core_lib = static_library('chandler-core',
  'async.c',
  'aliases.c',
  'catalog.c',
  'packages.c',
  'cli.c',
  include_directories : config_inc,
  link_with : [pure_lib, lifetime_lib],
  dependencies : deps,
)

# GUI: needs a display (tests run under Xvfb).
gui_lib = static_library('chandler-gui',
  'window.c',
  include_directories : config_inc,
  link_with : [core_lib, lifetime_lib],
  dependencies : deps,
)

core_inc = include_directories('.')

chandler_exe = executable('chandler', 'main.c',
  include_directories : config_inc,
  link_with : [gui_lib, core_lib, pure_lib, lifetime_lib],
  dependencies : deps,
)

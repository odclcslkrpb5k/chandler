# vendor/quoin/src/lifetime.c is the only file allowed the raw signal/source APIs (prelude.h).
# GBK_LIFETIME_IMPL is set for it and nothing else; tests/test-contract.c checks the effect.
lifetime_lib = static_library('gbk-lifetime',
  meson.project_source_root() / 'vendor' / 'quoin' / 'src' / 'lifetime.c',
  c_args : '-DGBK_LIFETIME_IMPL',
  include_directories : config_inc,
  dependencies : deps,
)

# GUI: needs a display (tests run under Xvfb).
gui_lib = static_library('chandler-gui',
  'window.c',
  include_directories : config_inc,
  link_with : [lifetime_lib],
  dependencies : deps,
)

core_inc = include_directories('.')

chandler_exe = executable('chandler', 'main.c',
  include_directories : config_inc,
  link_with : [gui_lib, lifetime_lib],
  dependencies : deps,
)

nessie_sources = [
  'frontend/common.c',
  'main.c',
]

nessie_dependencies = [
  nessie_core_dep,
]

frontend = get_option('frontend')

if frontend == 'raylib'
  nessie_sources += files('frontend/raylib.c')
  nessie_dependencies += dependency('raylib')
endif

nessie_exe = executable(
  'nessie',
  nessie_sources,
  dependencies : nessie_dependencies,
  install : true,
)

nessie_core_args = ['-DBUILDING_CORE']

nessie_core_dependencies = [
]

nessie_core_sources = [
  'nes.c',
  'mapper.c',
  'util.c',
  'scheduler.c',
  'error.c',
  'cpu.c',
  'ines.c',
]

nessie_core_lib = library(
  'core',
  nessie_core_sources,
  install : true,
  c_shared_args : nessie_core_args,
  dependencies : nessie_core_dependencies,
)

nessie_core_dep = declare_dependency(
  include_directories : include_directories('.'),
  dependencies : nessie_core_dependencies,
  link_with : nessie_core_lib,
)
meson.override_dependency('nessie-core', nessie_core_dep)

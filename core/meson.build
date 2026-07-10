lib_args = ['-DBUILDING_CORE']

core_dependencies = [
]

core_sources = [
  'nes.c',
  'mapper.c',
  'util.c',
  'scheduler.c',
  'error.c',
  'cpu.c',
  'ines.c',
]

core_lib = library(
  'core',
  core_sources,
  install : true,
  c_shared_args : lib_args,
  dependencies : core_dependencies,
)

core_dep = declare_dependency(
  include_directories : include_directories('.'),
  dependencies : core_dependencies,
  link_with : core_lib,
)
meson.override_dependency('core', core_dep)

project('lfi-static-bind', ['c'], default_options: ['warning_level=1', 'buildtype=debugoptimized'])

subdir('src')

libelf = subproject('libelf').get_variable('libelf_dep')
argtable = subproject('argtable3').get_variable('argtable_dep')

executable(
  'lfi-static-bind',
  srcs,
  dependencies: [libelf, argtable],
)

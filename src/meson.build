abbs_dep_sources = [
  'main.c',
  'vercomp.c',
]

abbs_dep_deps = [
  dependency('glib-2.0'),
  dependency('sqlite3'),
]

executable('abbs-dep', abbs_dep_sources,
  cpp_args: '-DSQLITE_CORE',
  dependencies: abbs_dep_deps,
  install: true,
)

executable('toposort-test', ['toposort.c'])

project('abbs-dep', 'c',
          version: '0.1.0',
    meson_version: '>= 0.50.0',
  default_options: ['warning_level=3', 'c_std=gnu11'],
)

config_h = configuration_data()
config_h.set_quoted('PACKAGE_VERSION', meson.project_version())
configure_file(
  output: 'abbs_dep-config.h',
  configuration: config_h,
)
add_project_arguments([
  '-I' + meson.build_root(),
], language: 'c')

subdir('src')


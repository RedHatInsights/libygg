api_version = '0.1'

libygg_sources = [
  'ygg-metadata.c',
  'ygg-worker.c',
]

libygg_headers = [
  'ygg.h',
  'ygg-metadata.h',
  'ygg-worker.h',
]

libygg_inc = include_directories('.')

version_split = meson.project_version().split('.')
version_conf = configuration_data()
version_conf.set('VERSION', meson.project_version())
version_conf.set('MAJOR_VERSION', version_split[0])
version_conf.set('MINOR_VERSION', version_split[1])
version_conf.set('MICRO_VERSION', version_split[2])

configure_file(
          input: 'ygg-version.h.in',
         output: 'ygg-version.h',
  configuration: version_conf,
        install: true,
    install_dir: join_paths(get_option('includedir'), 'libygg')
)

configure_file(
  input: 'ygg-constants.h.in',
  output: '@BASENAME@',
  configuration: config_data,
  install: true,
  install_dir: join_paths(get_option('includedir'), 'libygg')
)

libygg_deps = [
  dependency('gio-2.0', version: '>=2.56.0'),
]

add_project_arguments(
  '-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_56',
  '-DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_68',
  language: 'c'
)

libygg_lib = shared_library('libygg-' + api_version,
  libygg_sources,
  dependencies: libygg_deps,
       install: true,
)

install_headers(libygg_headers, subdir: 'libygg')

pkg = import('pkgconfig')
pkg.generate(
  description: 'A shared library for yggdrasil workers',
    libraries: libygg_lib,
         name: 'libygg',
     filebase: 'libygg-' + api_version,
      version: meson.project_version(),
      subdirs: 'libygg',
     requires: ['gio-2.0'],
  install_dir: join_paths(get_option('libdir'), 'pkgconfig')
)

gnome = import('gnome')
libygg_gir = gnome.generate_gir(libygg_lib,
              sources: [libygg_headers, libygg_sources],
            nsversion: api_version,
            namespace: 'Ygg',
        symbol_prefix: 'ygg',
    identifier_prefix: 'Ygg',
             includes: [ 'Gio-2.0' ],
              install: true,
       fatal_warnings: get_option('werror'),
)

test_sources = [
  'test-ygg-metadata.c'
]

test_deps = libygg_deps

test('test-ygg-metadata',
  executable('test-ygg-metadata',
    test_sources,
    dependencies: test_deps,
    link_with: [libygg_lib],
  ),
  env: [
    'G_TEST_SRCDIR=@0@'.format(meson.current_source_dir()),
    'G_TEST_BUILDDIR=@0@'.format(meson.current_build_dir()),
  ],
  protocol: 'tap',
)
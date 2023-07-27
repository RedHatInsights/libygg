echo_sources = [
  'main.c',
]

echo_deps = [
  dependency('gio-2.0'),
]

executable('yggdrasil-worker-echo',
  dependencies: echo_deps,
  include_directories: libygg_inc,
  link_with: [libygg],
  sources: echo_sources,
  install: get_option('install_examples')
)

configure_file(
  configuration: config_data,
  input: 'com.redhat.Yggdrasil1.Worker1.echo.service.in',
  output: '@BASENAME@',
  install_dir: dbus.get_variable('system_bus_services_dir'),
  install: get_option('install_examples'),
)

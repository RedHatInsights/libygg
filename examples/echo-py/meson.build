python = import('python')

dependency('gobject-introspection-1.0')

configure_file(
  input: 'main.py',
  output: 'yggdrasil-worker-echo.py',
  configuration: {
    'python': python.find_installation('python3').full_path(),
  },
  install_dir: get_option('bindir'),
  install: get_option('install_examples'),
  install_mode: 'rwxr-xr-x',
)

configure_file(
  configuration: config_data,
  input: 'com.redhat.Yggdrasil1.Worker1.echo_py.service.in',
  output: '@BASENAME@',
  install_dir: dbus.get_variable('system_bus_services_dir'),
  install: get_option('install_examples'),
)

if get_option('install_examples')
  install_data('com.redhat.Yggdrasil1.Worker1.echo_py.conf',
    install_dir: join_paths(dbus.get_variable(pkgconfig: 'datadir'), 'dbus-1', 'system.d'),
  )
endif

# libygg

_libygg_ is a GObject-based library providing an API for applications to
interact with _yggdrasil_.

## Requirements

* `pkgconfig(dbus-1)` >= 1.14.0
* `pkgconfig(gio-2.0)` >= 2.56.0
* `pkgconfig(gobject-introspection-1.0)`

## Installation

### Source

_libygg_ is compiled with [meson](https://mesonbuild.com). Use typical `meson
setup`, `meson compile`, `meson install` approach to compile and install
_libygg_.

```bash
meson setup builddir
meson compile -C builddir
meson install -C builddir
```

It is also possible to compile _libygg_ into an RPM suitable for installation on
Fedora-based distributions. See the README in [dist/srpm](./dist/srpm) for
details.

### Python stubs

_libygg_ can be used in Python as a `gi.repository` module (see the `examples/` directory).

You can install the type stubs into your virtual environment:

```bash
python3 -m pip install PyGObject-stubs
python3 -m pip install ./python
# Or, without cloning this repository:
python3 -m pip install git+https://github.com/RedHatInsights/libygg.git@main#subdirectory=python
```

## Features

* Worker Developer API

### Worker Developer API

`YggWorker` is a `GObject` subclass that provides an API for developing workers
in C (or any other language that supports gobject-introspection). To create a
worker using this library, you must supply the worker with a callback function
that is invoked when the worker receives data. The library handles all the D-Bus
connection logic.

#### Directly

The API is available directly via the C library.

```c
static void handle_rx (gchar *addr,
                       gchar *id,
                       gchar *response_to,
                       GHashTable *metadata,
                       GBytes *data,
                       gpointer user_data)
{
    g_debug ("%s", (guint8 *) g_bytes_get_data (data, NULL));
}

gint main (gint argc, gchar *argv[])
{
    YggWorker *worker = ygg_worker_new ("echo", FALSE, NULL, handle_rx, NULL);
    GError *error = NULL;
    if (!ygg_worker_connect (worker, &error)) {
        g_error ("%s", error->message);
    }

    GMainLoop *loop = g_main_loop_new (NULL, FALSE);
    g_main_loop_run (loop);

    return 0;
}
```

#### Indirectly

Additionally, the API can be used through gobject-introspection, as in the
Python example below.

```python
def handle_rx(worker, addr, id, response_to, meta_data, data):
    # data is an instance of GLib.Bytes (GBytes), so the raw bytes array must be
    # accessed via the GLib API (g_bytes_*).
    logging.debug(data.get_bytes())

if __name__ == "__main__":
    worker = Ygg.Worker(directive="echo", remote_content=False, features=None)
    worker.set_rx_func(handle_rx)
    worker.connect()

    loop = GLib.MainLoop()
    loop.run()
```

See [examples](./examples) for details on how to use the library in different
programming languages.

## Running

Under normal conditions, the worker will get started by systemd or D-Bus
service activation. When running a worker manually, make sure to set the value
of the `DBUS_STARTER_BUS_TYPE` environment variable to "session" or "system"
accordingly. `ygg_worker_connect` relies on the value of this variable to
determine which bus to connect to. If this value is missing, the worker will not
connect to a bus at all.

## Contact

Chat on Matrix: [#yggd:matrix.org](https://matrix.to/#/#yggd:matrix.org).

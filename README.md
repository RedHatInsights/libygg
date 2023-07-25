# libygg

_libygg_ is a GObject-based library providing an API for applications to
interact with _yggdrasil_.

## Requirements

* `pkgconfig(gio-2.0)` >= 2.56.0
* `pkgconfig(dbus-1)` >= 1.14.0

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

### Indirectly

Additionally, the API can be used through gobject-introspection, as in the
Python example below.

```python
def handle_rx(worker, addr, id, response_to, meta_data, data):
    logging.debug(data)

if __name__ == "__main__":
    worker = Ygg.Worker(directive="echo", remote_content=False, features=None)
    worker.set_rx_func(handle_rx)
    worker.connect()

    loop = GLib.MainLoop()
    loop.run()
```

See [examples](./examples) for details on how to use the library in different
programming languages.

## Contact

Chat on Matrix: [#yggd:matrix.org](https://matrix.to/#/#yggd:matrix.org).

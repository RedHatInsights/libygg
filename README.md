# libygg

_libygg_ is a GObject-based library providing an API for applications to
interact with _yggdrasil_.

## Features

* Worker Developer API

### Worker Developer API

`YggWorker` is a `GObject` subclass that provides an API for developing workers
in C (or any other language that supports gobject-introspection). To create a
worker using this library, you must supply the worker with a callback function
that is invoked when the worker receives data. The library handles all the D-Bus
connection logic.

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

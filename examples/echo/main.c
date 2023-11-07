/*
 * main.c
 *
 * Copyright 2023 Link Dupont <link@sub-pop.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <glib.h>
#include <glib-unix.h>
#include "ygg.h"

#if !GLIB_CHECK_VERSION(2, 58, 0)
#  define G_SOURCE_FUNC(f) ((GSourceFunc) (void (*)(void)) (f))

static gchar *
g_date_time_format_iso8601 (GDateTime *datetime)
{
  return g_date_time_format (datetime, "%C%y-%m-%dT%H:%M:%S.%fZ");
}
#endif

/** sleep_delay:
 *
 * The number of seconds the handler will sleep before execution.
 */
static gint sleep_delay = 0;

/** loop_times:
 *
 * The number of times the response handler is invoked.
 */
static gint loop_times = 1;

/** loops:
 *
 * The number of times the response handler **has been** invoked.
 */
static gint loops = 1;

typedef struct _RxData
{
  YggWorker   *worker;
  gchar       *addr;
  gchar       *id;
  gchar       *response_to;
  YggMetadata *metadata;
  GBytes      *data;
} _RxData;

static _RxData *
_rx_data_new (YggWorker *worker, gchar *addr, gchar *id, gchar *response_to, YggMetadata *metadata, GBytes *data)
{
  _RxData *rx_data = g_malloc0 (sizeof (_RxData));

  rx_data->worker = g_object_ref (worker);
  rx_data->addr = g_strdup (addr);
  rx_data->id = g_strdup (id);
  rx_data->response_to = g_strdup (response_to);
  rx_data->metadata = g_object_ref (metadata);
  rx_data->data = g_bytes_ref (data);

  return rx_data;
}

static void
_rx_data_free (_RxData *data)
{
  g_bytes_unref (data->data);
  g_object_unref (data->metadata);
  g_free (data->response_to);
  g_free (data->id);
  g_free (data->addr);
  g_object_unref (data->worker);

  g_free (data);
}

static void
foreach (const gchar *key,
              const gchar *value,
              gpointer     user_data)
{
  g_debug ("metadata[%s] = %s", (gchar *) key, (gchar *) value);
}

/**
 * transmit_done:
 *
 * A #GAsyncReadyCallback function invoked when ygg_worker_transmit() completes.
 */
static void
transmit_done (GObject      *source_object,
               GAsyncResult *result,
               gpointer      user_data)
{
  YggWorker *worker = YGG_WORKER (source_object);
  gint response_code = G_MININT;
  g_autoptr (YggMetadata) response_metadata = NULL;
  g_autoptr (GBytes) response_data = NULL;
  GError *err = NULL;

  g_debug ("transmit_done");
  g_assert_null (err);
  gboolean success = ygg_worker_transmit_finish (worker,
                                                 result,
                                                 &response_code,
                                                 &response_metadata,
                                                 &response_data,
                                                 &err);
  if (!success && err != NULL) {
    g_critical ("failed to transmit data: %s", err->message);
    return;
  }

  g_assert_nonnull (response_metadata);
  g_assert_nonnull (response_data);

  g_print ("response_code = %i\n", response_code);
  ygg_metadata_foreach (response_metadata, foreach, NULL);
  if (g_bytes_get_size (response_data) > 0) {
    g_print ("response_data = %s", (gchar *) g_bytes_get_data (response_data, NULL));
  }

  g_autoptr (GDateTime) now = g_date_time_new_now_utc ();
  g_autofree gchar *datetimestamp = g_date_time_format_iso8601 (now);
  g_assert_null (err);
  if (!ygg_worker_set_feature (worker, "UpdatedAt", datetimestamp, &err)) {
    if (err != NULL) {
      g_critical ("failed to set feature: %s", err->message);
      return;
    }
  }
}

static void handle_event (YggDispatcherEvent event,
                          gpointer           user_data)
{
  switch (event) {
  case YGG_DISPATCHER_EVENT_RECEIVED_DISCONNECT:
    g_print ("YGG_DISPATCHER_EVENT_RECEIVED_DISCONNECT");
    break;
  case YGG_DISPATCHER_EVENT_CONNECTION_RESTORED:
    g_print ("YGG_DISPATCHER_EVENT_CONNECTION_RESTORED");
    break;
  case YGG_DISPATCHER_EVENT_UNEXPECTED_DISCONNECT:
    g_print ("YGG_DISPATCHER_EVENT_UNEXPECTED_DISCONNECT");
    break;
  default:
    g_error ("unknown YggDispatcherEvent");
    break;
  }
}

static gboolean
tx_cb (gpointer user_data)
{
  _RxData *d = (_RxData *) user_data;

  g_debug ("loop iteration %d of %d", loops, loop_times);

  g_autofree gchar *new_message_uuid = g_uuid_string_random ();
  ygg_worker_transmit (d->worker,
                       d->addr,
                       new_message_uuid,
                       d->id,
                       d->metadata,
                       d->data,
                       NULL,
                       (GAsyncReadyCallback) transmit_done,
                       NULL);

  if (loops < loop_times) {
    g_atomic_int_inc (&loops);
    return G_SOURCE_CONTINUE;
  } else {
    g_atomic_int_set (&loops, 1);
    return G_SOURCE_REMOVE;
  }
}

/**
 * A #YggRxFunc callback that is invoked each time the worker receives data
 * from the dispatcher.
 */
static void handle_rx (YggWorker   *worker,
                       gchar       *addr,
                       gchar       *id,
                       gchar       *response_to,
                       YggMetadata *metadata,
                       GBytes      *data,
                       gpointer     user_data)
{
  g_debug ("handle_rx");
  g_debug ("addr = %s", addr);
  g_debug ("id = %s", id);
  g_debug ("response_to = %s", response_to);
  ygg_metadata_foreach (metadata, foreach, NULL);
  g_debug ("data = %s", (guint8 *) g_bytes_get_data (data, NULL));


  // Emit the "WORKING" event.
  GError *err = NULL;
  g_assert_null (err);
  g_autoptr (YggMetadata) event_data = ygg_metadata_new ();
  ygg_metadata_set (event_data, "message", (gchar *) g_bytes_get_data (data, NULL));
  if (!ygg_worker_emit_event (worker, YGG_WORKER_EVENT_WORKING, id, response_to, event_data, &err)) {
    if (err != NULL) {
      g_error ("%s", err->message);
    }
  }

  // Pack the message data into a container and add the actual call to
  // ygg_worker_transmit to a GSourceFunc, to be called by an idle timer after
  // a delay of 'sleep_delay' seconds.
  _RxData *rxdata = _rx_data_new (worker, addr, id, response_to, metadata, data);
  g_timeout_add_seconds_full (G_PRIORITY_DEFAULT,
                              sleep_delay,
                              G_SOURCE_FUNC (tx_cb),
                              rxdata,
                              (GDestroyNotify) _rx_data_free);

  g_free (addr);
  g_free (id);
  g_free (response_to);
  g_object_unref (metadata);
  g_bytes_unref (data);
}

static gboolean
sigint_callback (gpointer main_loop) {
  // Hint: this is good place to perform any resource clean up
  // before the application terminates.
  g_debug ("handling signal SIGINT");
  // Stops main loop
  g_main_loop_quit (main_loop);
  return G_SOURCE_REMOVE;
}

gint
main (gint   argc,
      gchar *argv[])
{
  GError *error = NULL;

  GOptionEntry option_entries[] = {
    {
      "sleep",
      's',
      G_OPTION_FLAG_NONE,
      G_OPTION_ARG_INT,
      &sleep_delay,
      "Sleep time in seconds before echoing the response",
      "SECONDS"
    },
    {
      "loop",
      'l',
      G_OPTION_FLAG_NONE,
      G_OPTION_ARG_INT,
      &loop_times,
      "Number of times to repeat the echo",
      "TIMES"
    },
    { NULL, 0, 0, 0, NULL, NULL, NULL }
  };

  // Create option context and add entries.
  g_autoptr (GOptionContext) opt_ctx = g_option_context_new ("");
  g_option_context_add_main_entries (opt_ctx, option_entries, NULL);

  g_assert_null (error);
  if (!g_option_context_parse (opt_ctx, &argc, &argv, &error)) {
    g_error ("failed to parse options: %s", error->message);
    exit (1);
  }

  // Create main context for main loop
  GMainContext *main_context = g_main_context_default ();
  g_autoptr (GMainLoop) loop = g_main_loop_new (main_context, FALSE);

  // Attach signal sources to the main_context
  g_autoptr (GSource) sigint_source = g_unix_signal_source_new (SIGINT);
  g_source_attach (sigint_source, main_context);
  g_source_set_callback (sigint_source, G_SOURCE_FUNC (sigint_callback), loop, NULL);

  YggMetadata *features = ygg_metadata_new ();
  ygg_metadata_set (features, "version", "1");

  g_autoptr (YggWorker) worker = ygg_worker_new ("echo", FALSE, features);
  if (!ygg_worker_set_rx_func (worker, handle_rx, NULL, NULL)) {
    g_error ("failed to set rx_func");
  }
  if (!ygg_worker_set_event_func (worker, handle_event, NULL, NULL)) {
    g_error ("failed to set event_func");
  }

  g_assert_null (error);
  if (!ygg_worker_connect (worker, &error)) {
    g_error ("%s", error->message);
    exit (1);
  }

  g_debug ("starting main loop of echo worker");
  g_main_loop_run (loop);

  g_debug ("terminating echo worker");
  // Note: No need to call g_source_remove() for sigint_source, because callback function
  // sigint_callback returned G_SOURCE_REMOVE and the source was automatically removed
  g_source_destroy (sigint_source);

  return 0;
}



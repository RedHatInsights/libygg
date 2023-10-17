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


  GError *err = NULL;
  g_assert_null (err);
  g_autoptr (YggMetadata) event_data = ygg_metadata_new ();
  ygg_metadata_set (event_data, "message", (gchar *) g_bytes_get_data (data, NULL));
  if (!ygg_worker_emit_event (worker, YGG_WORKER_EVENT_WORKING, id, response_to, event_data, &err)) {
    if (err != NULL) {
      g_error ("%s", err->message);
    }
  }
  g_autofree gchar *new_message_uuid = g_uuid_string_random ();
  ygg_worker_transmit (worker,
                       addr,
                       new_message_uuid,
                       id,
                       metadata,
                       data,
                       NULL,
                       (GAsyncReadyCallback) transmit_done,
                       NULL);
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
  g_set_prgname ("yggdrasil-worker-echo");

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

  GError *error = NULL;
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



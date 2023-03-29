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
#include "ygg.h"

static void
hash_foreach (gpointer key,
              gpointer value,
              gpointer user_data)
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
  GHashTable *response_metadata = NULL;
  GBytes *response_data = NULL;
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
  g_hash_table_foreach (response_metadata, hash_foreach, NULL);
  if (g_bytes_get_size (response_data) > 0) {
    g_print ("response_data = %s", (gchar *) g_bytes_get_data (response_data, NULL));
  }

  GDateTime *now = g_date_time_new_now_utc ();
  g_assert_null (err);
  if (!ygg_worker_set_feature (worker, "UpdatedAt", g_date_time_format_iso8601 (now), &err)) {
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
static void handle_rx (YggWorker  *worker,
                       gchar      *addr,
                       gchar      *id,
                       gchar      *response_to,
                       GHashTable *metadata,
                       GBytes     *data,
                       gpointer    user_data)
{
  g_debug ("handle_rx");
  g_debug ("addr = %s", addr);
  g_debug ("id = %s", id);
  g_debug ("response_to = %s", response_to);
  g_hash_table_foreach (metadata, hash_foreach, NULL);
  g_debug ("data = %s", (guint8 *) g_bytes_get_data (data, NULL));


  GError *err = NULL;
  g_assert_null (err);
  if (!ygg_worker_emit_event (worker, YGG_WORKER_EVENT_WORKING, g_strconcat ("working on data: ", (gchar *) g_bytes_get_data (data, NULL), NULL), &err)) {
    if (err != NULL) {
      g_error ("%s", err->message);
    }
  }

  ygg_worker_transmit (worker,
                       addr,
                       g_uuid_string_random (),
                       id,
                       metadata,
                       data,
                       NULL,
                       (GAsyncReadyCallback) transmit_done,
                       NULL);
}

gint
main (gint   argc,
      gchar *argv[])
{
  g_set_prgname ("yggdrasil-worker-echo");

  GHashTable *features = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
  g_hash_table_insert (features, (gpointer) "version", (gpointer) "1");

  YggWorker *worker = ygg_worker_new ("echo", FALSE, features);
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

  GMainLoop *loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (loop);

  return 0;
}


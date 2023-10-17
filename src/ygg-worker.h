/*
 * ygg-worker.h
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

#pragma once

#include <glib-object.h>
#include <gio/gio.h>
#include "ygg-metadata.h"

G_BEGIN_DECLS

/**
 * YGG_WORKER_ERROR:
 *
 * Error domain for Yggdrasil workers. Errors in this domain will be from the
 * #YggWorkerError enumeration.
 */
#define YGG_WORKER_ERROR ygg_worker_error_quark ()
GQuark ygg_worker_error_quark (void);

/**
 * YggWorkerError:
 * @YGG_WORKER_ERROR_INVALID_DIRECTIVE: Directive contains an invalid character.
 * @YGG_WORKER_ERROR_UNKNOWN_METHOD: An unknown method was invoked on the worker.
 * @YGG_WORKER_ERROR_MISSING_FEATURE: The worker's feature table has no value for
 * the given key.
 *
 * Error codes returned by #YggWorker routines.
 */
typedef enum
{
  YGG_WORKER_ERROR_INVALID_DIRECTIVE,
  YGG_WORKER_ERROR_UNKNOWN_METHOD,
  YGG_WORKER_ERROR_MISSING_FEATURE
} YggWorkerError;

/**
 * YggWorkerEvent:
 * @YGG_WORKER_EVENT_BEGIN: Signal to indicate the worker has accepted the data
 *                          and is beginning to "work".
 * @YGG_WORKER_EVENT_END: Signal to indicate the worker has completed operating
 *                        on its last received data.
 * @YGG_WORKER_EVENT_WORKING: Signal to indicate the worker is busy working.
 *
 * Signals emitted by #YggWorker instances to indicate the operating state of
 * the worker to interested parties.
 */
typedef enum
{
  YGG_WORKER_EVENT_BEGIN = 1,
  YGG_WORKER_EVENT_END,
  YGG_WORKER_EVENT_WORKING
} YggWorkerEvent;

/**
 * YggDispatcherEvent:
 * @YGG_DISPATCHER_EVENT_RECEIVED_DISCONNECT: Received when the dispatcher
 * receives the "disconnect" command.
 * @YGG_DISPATCHER_EVENT_UNEXPECTED_DISCONNECT: Received when the transport
 * unexpectedly disconnects from the network.
 * @YGG_DISPATCHER_EVENT_CONNECTION_RESTORED: Received when the transport
 * reconnects to the network.
 *
 * Events received from the dispatcher when certain conditions arise, such as
 * unexpected network disconnections or control commands received from the
 * operating service.
 */
typedef enum
{
  YGG_DISPATCHER_EVENT_RECEIVED_DISCONNECT = 1,
  YGG_DISPATCHER_EVENT_UNEXPECTED_DISCONNECT,
  YGG_DISPATCHER_EVENT_CONNECTION_RESTORED
} YggDispatcherEvent;

#define YGG_TYPE_WORKER (ygg_worker_get_type())

G_DECLARE_FINAL_TYPE (YggWorker, ygg_worker, YGG, WORKER, GObject)

/**
 * YggRxFunc:
 * @worker: (transfer none): A #YggWorker instance.
 * @addr: (transfer full): destination address of the data to be transmitted.
 * @id: (transfer full): a UUID.
 * @response_to: (transfer full) (nullable): a UUID the data is in response to
 *               or %NULL.
 * @metadata: (transfer full) (nullable): A set of key/value pairs, or %NULL.
 * @data: (transfer full): the data.
 * @user_data: (closure): Data passed to the function when it is invoked.
 *
 * Signature for callback function used in ygg_worker_set_rx_func(). It is
 * invoked each time the worker receives data from the dispatcher.
 */
typedef void (* YggRxFunc) (YggWorker   *worker,
                            gchar       *addr,
                            gchar       *id,
                            gchar       *response_to,
                            YggMetadata *metadata,
                            GBytes      *data,
                            gpointer     user_data);

/**
 * YggEventFunc:
 * @event: The event received from the dispatcher.
 * @user_data: (closure): Data passed to the function when it is invoked.
 *
 * Signature for callback function used in ygg_worker_set_event_func(). It is
 * invoked each time the worker receives a
 * com.redhat.Yggdrasil1.Dispatcher1.Event signal.
 */
typedef void (* YggEventFunc) (YggDispatcherEvent event,
                               gpointer           user_data);

YggWorker *ygg_worker_new (const gchar *directive,
                           gboolean     remote_content,
                           YggMetadata *features);

gboolean ygg_worker_connect (YggWorker  *worker,
                             GError    **error);

void ygg_worker_transmit (YggWorker           *worker,
                          gchar               *addr,
                          gchar               *id,
                          gchar               *response_to,
                          YggMetadata         *metadata,
                          GBytes              *data,
                          GCancellable        *cancellable,
                          GAsyncReadyCallback  callback,
                          gpointer             user_data);

gboolean ygg_worker_transmit_finish (YggWorker     *worker,
                                     GAsyncResult  *res,
                                     gint          *response_code,
                                     YggMetadata  **response_metadata,
                                     GBytes       **response_data,
                                     GError       **error);

gboolean ygg_worker_emit_event (YggWorker       *worker,
                                YggWorkerEvent   event,
                                const gchar     *message_id,
                                const gchar     *response_to,
                                YggMetadata     *data,
                                GError         **error);

const gchar * ygg_worker_get_feature (YggWorker    *worker,
                                      const gchar  *key,
                                      GError      **error);

gboolean ygg_worker_set_feature (YggWorker    *worker,
                                 const gchar  *key,
                                 gchar        *value,
                                 GError      **error);

gboolean ygg_worker_set_rx_func (YggWorker      *worker,
                                 YggRxFunc       func,
                                 gpointer        user_data,
                                 GDestroyNotify  notify);

gboolean ygg_worker_set_event_func (YggWorker      *worker,
                                    YggEventFunc    func,
                                    gpointer        user_data,
                                    GDestroyNotify  notify);

G_END_DECLS

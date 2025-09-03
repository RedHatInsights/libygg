/*
 * ygg-worker.c
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

#include <gio/gio.h>

#include "ygg-worker.h"
#include "ygg-constants.h"

typedef struct {
  YggWorker   *worker;
  gchar       *addr;
  gchar       *id;
  gchar       *response_to;
  YggMetadata *metadata;
  GBytes      *data;
} Message;

/**
 * message_new:
 * @worker: (transfer none): A #YggWorker.
 * @addr: (transfer none): Address of the message.
 * @id: (transfer none): ID of the message.
 * @response_to: (transfer none) (nullable): ID of a message to respond to.
 * @metadata: (transfer none) (nullable): Key/value pairs to associate with the
 * message.
 * @data: (transfer none): The data of the message.
 *
 * Creates a new #Message.
 *
 * Returns: (transfer full): A newly created #Message;
 */
static Message *
message_new (YggWorker   *worker,
             gchar       *addr,
             gchar       *id,
             gchar       *response_to,
             YggMetadata *metadata,
             GBytes      *data)
{
  Message *message = (Message *) g_new0 (Message, 1);

  message->worker = g_object_ref (worker);
  message->addr = g_strdup (addr);
  message->id = g_strdup (id);
  message->response_to = g_strdup (response_to);
  message->metadata = g_object_ref (metadata);
  message->data = g_bytes_ref (data);

  return message;
}

static Message *
message_new_from_variant (YggWorker  *worker,
                          GVariant   *parameters,
                          GError    **error)
{
  GError *err = NULL;
  GVariantIter iter;
  g_variant_iter_init (&iter, parameters);

  g_autofree gchar *addr = NULL;
  g_variant_iter_next (&iter, "s", &addr);

  g_autofree gchar *id = NULL;
  g_variant_iter_next (&iter, "s", &id);

  g_autofree gchar *response_to = NULL;
  g_variant_iter_next (&iter, "s", &response_to);

  g_autoptr (YggMetadata) metadata = ygg_metadata_new_from_variant (g_variant_iter_next_value (&iter), &err);
  if (err != NULL && error != NULL) {
    g_critical ("failed to create metadata from variant: %s", err->message);
    g_propagate_error (error, err);
    return NULL;
  }

  GBytes *data = g_variant_get_data_as_bytes (g_variant_iter_next_value (&iter));

  Message *msg = message_new (worker,
                              addr,
                              id,
                              response_to,
                              metadata,
                              data);
  return msg;
}

static void
metadata_foreach_builder_add (const gchar *key,
                              const gchar *val,
                              gpointer     user_data)
{
  GVariantBuilder *builder = (GVariantBuilder *) user_data;
  g_variant_builder_add (builder, "{ss}", key, val);
}

static GVariant *
message_to_variant (Message *msg)
{
  GVariantBuilder builder;
  g_variant_builder_init (&builder, G_VARIANT_TYPE ("(sssa{ss}ay)"));
  g_variant_builder_add (&builder, "s", msg->addr);
  g_variant_builder_add (&builder, "s", msg->id);
  g_variant_builder_add (&builder, "s", msg->response_to);
  g_variant_builder_open (&builder, G_VARIANT_TYPE( "a{ss}"));
  ygg_metadata_foreach (msg->metadata, metadata_foreach_builder_add, &builder);
  g_variant_builder_close (&builder);
  g_variant_builder_open (&builder, G_VARIANT_TYPE ("ay"));
  const guint8 *bytes = g_bytes_get_data (msg->data, NULL);
  for (int i = 0; i < g_bytes_get_size (msg->data); i++) {
    g_variant_builder_add (&builder, "y", bytes[i]);
  }
  g_variant_builder_close (&builder);
  return g_variant_builder_end (&builder);
}

static void
message_free (Message *message)
{
  g_free (message->addr);
  g_free (message->id);
  g_free (message->response_to);
  g_object_unref (message->metadata);
  g_bytes_unref (message->data);
  g_free (message);
}

typedef struct {
  YggWorker *worker;
  gchar     *addr;
  gchar     *id;
  gchar     *cancel_id;
} CancelMessage;

/**
 * cancel_message_new:
 * @worker: (transfer none): A #YggWorker;
 * @addr: (transfer none): Address of the message.
 * @id: (transfer none): ID of the message.
 * @cancel_id: (transfer none): ID of the message to cancel.
 * 
 * Creates a new #CancelMessage.
 * 
 * Returns: (transfer full): A newly created #CancelMessage.
*/
static CancelMessage *
cancel_message_new (YggWorker *worker,
                    gchar     *addr,
                    gchar     *id,
                    gchar     *cancel_id)
{
  CancelMessage *message = (CancelMessage *) g_new0 (CancelMessage, 1);

  message->worker = g_object_ref (worker);
  message->addr = g_strdup (addr);
  message->id = g_strdup (id);
  message->cancel_id = g_strdup (cancel_id);

  return message;
}

static CancelMessage *
cancel_message_new_from_variant (YggWorker  *worker,
                                 GVariant   *parameters)
{
  GVariantIter iter;
  g_variant_iter_init (&iter, parameters);

  g_autofree gchar *addr = NULL;
  g_variant_iter_next (&iter, "s", &addr);

  g_autofree gchar *id = NULL;
  g_variant_iter_next (&iter, "s", &id);

  g_autofree gchar *cancel_id = NULL;
  g_variant_iter_next (&iter, "s", &cancel_id);

  CancelMessage *msg = cancel_message_new (worker, addr, id, cancel_id);
  return msg;
}

static void
cancel_message_free (CancelMessage *message)
{
  g_free (message->addr);
  g_free (message->id);
  g_free (message->cancel_id);
  g_free (message);
}

G_DEFINE_QUARK (ygg-worker-error-quark, ygg_worker_error)

static GDBusNodeInfo *dispatcher_node_info;
static GDBusNodeInfo *worker_node_info;

struct _YggWorker
{
  GObject parent_instance;
};

typedef struct
{
  gchar         *directive;
  gboolean       remote_content;
  YggMetadata   *features;
  YggRxFunc      rx_func;
  gpointer       rx_func_user_data;
  GDestroyNotify rx_func_data_notify;
  YggEventFunc   event_func;
  gpointer       event_func_user_data;
  GDestroyNotify event_func_data_notify;
  YggCancelFunc  cancel_func;
  gpointer       cancel_func_user_data;
  GDestroyNotify cancel_func_data_notify;
  guint          bus_id;
  gchar         *bus_name;
  gchar         *object_path;
} YggWorkerPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (YggWorker, ygg_worker, G_TYPE_OBJECT)

enum {
  PROP_DIRECTIVE = 1,
  PROP_REMOTE_CONTENT,
  PROP_FEATURES,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

enum {
  SIGNAL_CONNECTED = 1,
  SIGNAL_DISCONNECTED,
  N_SIGNALS
};
static guint signals[N_SIGNALS];

/**
 * invoke_rx:
 * @user_data: (transfer full): The received #Message.
 *
 * A #GSourceFunc that handles com.redhat.Yggdrasil1.Worker1.Dispatch
 * asynchronously.
 *
 * Returns: %G_SOURCE_REMOVE, indicating that this callback should only be
 * invoked once.
 */
static gboolean
invoke_rx (gpointer user_data)
{
  g_debug ("invoke_rx");
  Message *msg = (Message *) user_data;
  YggWorker *self = YGG_WORKER (msg->worker);
  YggWorkerPrivate *priv = ygg_worker_get_instance_private (self);
  GError *err = NULL;

  g_assert_null (err);
  if (!ygg_worker_emit_event (self, YGG_WORKER_EVENT_BEGIN, msg->id, msg->response_to, NULL, &err)) {
    if (err != NULL) {
      g_critical ("%s", err->message);
      goto out;
    }
  }

  g_assert_nonnull (priv->rx_func);
  priv->rx_func (self,
                 g_strdup (msg->addr),
                 g_strdup (msg->id),
                 g_strdup (msg->response_to),
                 g_object_ref (msg->metadata),
                 g_bytes_ref (msg->data),
                 priv->rx_func_user_data);

  g_assert_null (err);
  if (!ygg_worker_emit_event (self, YGG_WORKER_EVENT_END, msg->id, msg->response_to, NULL, &err)) {
    if (err != NULL) {
      g_critical ("%s", err->message);
      goto out;
    }
  }

out:
  message_free (msg);

  return G_SOURCE_REMOVE;
}

/**
 * invoke_cancel:
 * @user_data: (transfer full): The received #CancelMessage.
 * 
 * A #GSourceFunc that handles com.redhat.Yggdrasil1.Worker1.Cancel
 * asynchronously.
 * 
 * Returns %G_SOURCE_REMOVE, indicating that this callback should only be
 * invoked once.
*/
static gboolean
invoke_cancel (gpointer user_data)
{
  g_debug ("invoke_cancel");
  CancelMessage *msg = (CancelMessage *) user_data;
  YggWorker *self = YGG_WORKER (msg->worker);
  YggWorkerPrivate *priv = ygg_worker_get_instance_private (self);
  GError *err = NULL;

  g_assert_null (err);
  if (!ygg_worker_emit_event (self, YGG_WORKER_EVENT_BEGIN, msg->id, NULL, NULL, &err)) {
    if (err != NULL) {
      g_critical ("%s", err->message);
      goto out;
    }
  }

  g_assert_nonnull (priv->cancel_func);
  priv->cancel_func (self,
                      msg->addr,
                      msg->id,
                      msg->cancel_id,
                      priv->cancel_func_user_data);

  g_assert_null (err);
  if (!ygg_worker_emit_event (self, YGG_WORKER_EVENT_END, msg->id, NULL, NULL, &err)) {
    if (err != NULL) {
      g_critical ("%s", err->message);
      goto out;
    }
  }

out:
  cancel_message_free (msg);

  return G_SOURCE_REMOVE;
}

static void
dbus_proxy_call_done (GObject      *source_object,
                      GAsyncResult *result,
                      gpointer      user_data)
{
  g_debug ("dbus_proxy_call_done");
  GDBusProxy *proxy = G_DBUS_PROXY (source_object);
  GTask *task = G_TASK (user_data);
  GError *err = NULL;

  g_assert_null (err);
  GVariant *response = g_dbus_proxy_call_finish (proxy, result, &err);
  if (err != NULL) {
    g_critical ("unable to call com.redhat.Yggdrasil1.Dispatcher1.Transmit: %s", err->message);
    g_task_return_error (task, err);
    return;
  }

  g_task_return_pointer (task, response, (GDestroyNotify) g_variant_unref);
}

/**
 * invoke_tx:
 * @user_data: (transfer none): the #GTask responsible for invoke_tx.
 *
 * A #GSourceFunc that invokes com.redhat.Yggdrasil1.Dispatcher1.Transmit
 * synchronously and attaches the response to a #GTask.
 *
 * Returns: #G_SOURCE_REMOVE, indicating that this callback should only be
 * invoked once.
 */
static gboolean
invoke_tx (gpointer user_data)
{
  g_debug ("invoke_tx");
  GTask *task = G_TASK (user_data);
  GError *err = NULL;

  Message *message = (Message *) g_task_get_task_data (task);
  g_return_val_if_fail (message != NULL, G_SOURCE_REMOVE);

  GDBusInterfaceInfo *interface_info = g_dbus_node_info_lookup_interface (dispatcher_node_info, "com.redhat.Yggdrasil1.Dispatcher1");
  g_assert_nonnull (interface_info);

  g_assert_null (err);
  GDBusProxy *proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_STARTER,
                                                     G_DBUS_PROXY_FLAGS_NONE,
                                                     interface_info,
                                                     "com.redhat.Yggdrasil1.Dispatcher1",
                                                     "/com/redhat/Yggdrasil1/Dispatcher1",
                                                     "com.redhat.Yggdrasil1.Dispatcher1",
                                                     NULL,
                                                     &err);
  if (err != NULL) {
    g_critical ("unable to get proxy object for com.redhat.Yggdrasil1.Dispatcher1: %s", err->message);
    g_task_return_error (task, err);
    return G_SOURCE_REMOVE;
  }

  GVariant *parameters = message_to_variant (message);
  g_autofree gchar *printed_params = g_variant_print (parameters, TRUE);
  g_debug ("Transmit parameters: %s", printed_params);

  g_dbus_proxy_call (proxy,
                     "Transmit",
                     parameters,
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,
                     NULL,
                     (GAsyncReadyCallback) dbus_proxy_call_done,
                     task);

  return G_SOURCE_REMOVE;
}

static void
handle_method_call (GDBusConnection       *connection,
                    const gchar           *sender,
                    const gchar           *object_path,
                    const gchar           *interface_name,
                    const gchar           *method_name,
                    GVariant              *parameters,
                    GDBusMethodInvocation *invocation,
                    gpointer               user_data)
{
  YggWorker *self = YGG_WORKER (user_data);

  YggWorkerPrivate *priv = ygg_worker_get_instance_private (self);

  g_autofree gchar *print_params = g_variant_print (parameters, TRUE);
  g_debug ("%s parameters: %s", method_name, print_params);

  if (g_strcmp0 (method_name, "Dispatch") == 0) {
    GError *err = NULL;

    Message *msg = message_new_from_variant (self, parameters, &err);
    if (err != NULL) {
      g_dbus_method_invocation_return_gerror (invocation, err);
      return;
    }

    g_idle_add (invoke_rx, msg);
    g_dbus_method_invocation_return_value (invocation, NULL);
    return;
  } else if (g_strcmp0 (method_name, "Cancel") == 0) {
    if (priv->cancel_func == NULL) {
      g_dbus_method_invocation_return_dbus_error (invocation,
                                                  "org.freedesktop.DBus.UnknownInterface",
                                                  "Cancel method not implemented");
      return;
    }

    CancelMessage *msg = cancel_message_new_from_variant (self, parameters);

    g_idle_add (invoke_cancel, msg);
    g_dbus_method_invocation_return_value (invocation, NULL);
    return;
  } else {
    g_dbus_method_invocation_return_error (invocation,
                                           YGG_WORKER_ERROR,
                                           YGG_WORKER_ERROR_UNKNOWN_METHOD,
                                           "unknown method: %s",
                                           method_name);
    return;
  }
}

static void
metadata_foreach_builder_add_value (const gchar *key,
                                    const gchar *val,
                                    gpointer     user_data)
{
  GVariantBuilder *builder = (GVariantBuilder *) user_data;
  g_variant_builder_add (builder, "{sv}", key, g_variant_new ("s", val));
}

static GVariant*
handle_get_property (GDBusConnection  *connection,
                     const gchar      *sender,
                     const gchar      *object_path,
                     const gchar      *interface_name,
                     const gchar      *property_name,
                     GError          **error,
                     gpointer          user_data)
{
  GVariant *value = NULL;

  YggWorker *self = YGG_WORKER (user_data);
  YggWorkerPrivate *priv = ygg_worker_get_instance_private (self);

  if (g_strcmp0 (property_name, "RemoteContent") == 0) {
    value = g_variant_new_boolean (priv->remote_content);
  } else
  if (g_strcmp0 (property_name, "Features") == 0) {
    g_assert_nonnull (priv->features);
    GVariantBuilder builder;
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{ss}"));
    ygg_metadata_foreach (priv->features, metadata_foreach_builder_add, &builder);
    value = g_variant_builder_end (&builder);
  }

  return value;
}

static gboolean
handle_set_property (GDBusConnection  *connection,
                     const gchar      *sender,
                     const gchar      *object_path,
                     const gchar      *interface_name,
                     const gchar      *property_name,
                     GVariant         *value,
                     GError          **error,
                     gpointer          user_data)
{
  return FALSE;
}

static const GDBusInterfaceVTable interface_vtable = {
  handle_method_call,
  handle_get_property,
  handle_set_property,
  { 0 }
};

static void
handle_signal (GDBusConnection *connection,
               const gchar     *sender_name,
               const gchar     *object_path,
               const gchar     *interface_name,
               const gchar     *signal_name,
               GVariant        *parameters,
               gpointer         user_data)
{
  YggWorker *self = YGG_WORKER (user_data);
  YggWorkerPrivate *priv = ygg_worker_get_instance_private (self);

  g_debug ("received %s signal with parameters: %s", signal_name, g_variant_print (parameters, TRUE));

  YggDispatcherEvent event;
  g_variant_get (parameters, "(u)", &event);

  if (priv->event_func)
    priv->event_func (event, priv->event_func_user_data);
}


static void
on_bus_acquired (GDBusConnection *connection,
                 const gchar     *name,
                 gpointer         user_data)
{
  g_debug ("on_bus_acquired: %s", name);

  YggWorker *self = YGG_WORKER (user_data);
  YggWorkerPrivate *priv = ygg_worker_get_instance_private (self);

  GDBusInterfaceInfo *interface_info = g_dbus_node_info_lookup_interface (worker_node_info, "com.redhat.Yggdrasil1.Worker1");
  g_assert (interface_info);

  GError *err = NULL;
  guint registration_id = g_dbus_connection_register_object (connection,
                                                             priv->object_path,
                                                             interface_info,
                                                             &interface_vtable,
                                                             user_data,
                                                             NULL,
                                                             &err);
  if (registration_id <= 0) {
    g_error ("%s", err->message);
  }

  g_dbus_connection_signal_subscribe (connection,
                                      "com.redhat.Yggdrasil1.Dispatcher1",
                                      "com.redhat.Yggdrasil1.Dispatcher1",
                                      "Event",
                                      "/com/redhat/Yggdrasil1/Dispatcher1",
                                      NULL,
                                      G_DBUS_SIGNAL_FLAGS_NONE,
                                      handle_signal,
                                      self,
                                      NULL);

  g_signal_emit (self, signals[SIGNAL_CONNECTED], 0);
}

static void
on_name_acquired (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
  g_debug ("on_name_acquired: %s", name);
}

static void
on_name_lost (GDBusConnection *connection,
              const gchar     *name,
              gpointer         user_data)
{
  g_debug ("on_name_lost: %s", name);

  YggWorker *self = YGG_WORKER (user_data);
  g_signal_emit (self, signals[SIGNAL_DISCONNECTED], 0);
}

/**
 * ygg_worker_new: (constructor)
 * @directive: (transfer none): The worker directive name.
 * @remote_content: The worker requires content from a remote URL.
 * @features: (transfer none) (nullable): An initial table of values to use as
 * the worker's features map.
 *
 * Creates a new #YggWorker instance.
 *
 * Returns: (transfer full): A new #YggWorker instance.
 */
YggWorker *
ygg_worker_new (const gchar *directive,
                gboolean     remote_content,
                YggMetadata *features)
{
  return g_object_new (YGG_TYPE_WORKER,
                       "directive", directive,
                       "remote-content", remote_content,
                       "features", features,
                       NULL);
}

/**
 * ygg_worker_connect:
 * @worker: A #YggWorker.
 * @error: (nullable): Return location for a #GError or NULL.
 *
 * Connects a given #YggWorker to either a system or session D-Bus connection.
 * It then exports itself onto the bus, implementing the
 * com.redhat.Yggdrasil1.Worker1 interface.
 *
 * Returns: TRUE if the connection to the bus was successful. If FALSE and
 * @error is not NULL, @error will be set.
 */
gboolean
ygg_worker_connect (YggWorker *self, GError **error)
{
  YggWorkerPrivate *priv = ygg_worker_get_instance_private (self);
  g_assert_nonnull (priv->rx_func);

  if (g_regex_match_simple ("-", priv->directive, 0, 0)) {
    if (error != NULL) {
      *error = g_error_new (YGG_WORKER_ERROR,
                            YGG_WORKER_ERROR_INVALID_DIRECTIVE,
                            "%s is not a valid directive",
                            priv->directive);
    }
    return FALSE;
  }

  priv->object_path = g_strjoin ("/", "/com/redhat/Yggdrasil1/Worker1", priv->directive, NULL);
  priv->bus_name = g_strjoin (".", "com.redhat.Yggdrasil1.Worker1", priv->directive, NULL);

  priv->bus_id = g_bus_own_name (G_BUS_TYPE_STARTER,
                                 priv->bus_name,
                                 G_BUS_NAME_OWNER_FLAGS_NONE,
                                 on_bus_acquired,
                                 on_name_acquired,
                                 on_name_lost,
                                 self,
                                 NULL);

  return TRUE;
}

/**
 * ygg_worker_transmit_finish:
 * @worker: A #YggWorker instance.
 * @res: A #GAsyncResult.
 * @response_code: (out): An integer status code.
 * @response_metadata: (out): A map of key/value pairs.
 * @response_data: (out): Data received in response to the transmit.
 * @error: (out) (nullable): The return location for a recoverable error.
 *
 * Finishes transmitting the data started with ygg_worker_transmit().
 *
 * Returns: A %TRUE on success, %FALSE on error.
 */
gboolean
ygg_worker_transmit_finish (YggWorker     *self,
                            GAsyncResult  *res,
                            gint          *response_code,
                            YggMetadata  **response_metadata,
                            GBytes       **response_data,
                            GError       **error)
{
  g_debug ("ygg_worker_transmit_finish");
  g_return_val_if_fail (g_task_is_valid (res, self), FALSE);
  GTask *task = G_TASK (res);
  GError *err = NULL;

  g_autoptr (GVariant) response = g_task_propagate_pointer (task, &err);
  if (err != NULL && error != NULL) {
    g_propagate_error (error, err);
    return FALSE;
  }
  g_object_unref (task);

  g_autofree gchar *printed_variant = g_variant_print (response, TRUE);
  g_debug ("%s", printed_variant);

  GVariantIter iter;
  g_variant_iter_init (&iter, response);
  g_variant_iter_next (&iter, "i", response_code);

  g_assert_null (err);
  *response_metadata = ygg_metadata_new_from_variant (g_variant_iter_next_value(&iter), &err);
  if (err != NULL && error != NULL) {
    g_propagate_error (error, err);
    return FALSE;
  }

  gsize len = 0;
  gchar *data = g_variant_dup_bytestring (g_variant_iter_next_value (&iter), &len);
  *response_data = g_bytes_new_take (data, len);

  return *response_code >= 0;
}

/**
 * ygg_worker_transmit:
 * @worker: A #YggWorker.
 * @addr: (transfer none): destination address of the data to be transmitted.
 * @id: (transfer none): a UUID.
 * @response_to: (transfer none) (nullable): a UUID the data is in response to
 * or %NULL.
 * @metadata: (transfer none) (nullable): Key-value pairs associated with the
 * data or %NULL.
 * @data: (transfer none): the data.
 * @cancellable: (nullable): a #GCancellable or %NULL.
 * @callback: (scope async): A #GAsyncReadyCallback to be invoked when the task is complete.
 * @user_data: (nullable): optional data passed into @callback.
 *
 * Invokes the com.redhat.Yggdrasil1.Dispatcher1.Transmit D-Bus method
 * asynchronously. To receive the response from the D-Bus method, call
 * ygg_worker_transmit_finish().
 */
void
ygg_worker_transmit (YggWorker           *self,
                     gchar               *addr,
                     gchar               *id,
                     gchar               *response_to,
                     YggMetadata         *metadata,
                     GBytes              *data,
                     GCancellable        *cancellable,
                     GAsyncReadyCallback  callback,
                     gpointer             user_data)
{
  g_debug ("ygg_worker_transmit");
  GTask *task = g_task_new (self, cancellable, callback, user_data);
  Message *message = message_new (self,
                                  addr,
                                  id,
                                  response_to,
                                  metadata,
                                  data);
  g_task_set_task_data (task, message, (GDestroyNotify) message_free);
  GSource *source = g_idle_source_new ();
  g_task_attach_source (task, source, invoke_tx);
}

/**
 * ygg_worker_emit_event:
 * @worker: A #YggWorker instance.
 * @event: The #YggWorkerEvent to emit.
 * @message_id: The message ID.
 * @response_to: (nullable): ID of the message this message is in response to.
 * @data: (nullable): Key-value pairs of optional data provided with the event.
 * @error: (nullable): Return location for a recoverable error.
 *
 * Emits a com.redhat.Yggdrasil1.Worker1.Event signal.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 */
gboolean ygg_worker_emit_event (YggWorker       *self,
                                YggWorkerEvent   event,
                                const gchar     *message_id,
                                const gchar     *response_to,
                                YggMetadata     *data,
                                GError         **error)
{
  GError *err = NULL;

  g_debug ("ygg_worker_emit_event");

  YggWorkerPrivate *priv = ygg_worker_get_instance_private (self);

  g_assert_null (err);
  GDBusConnection *connection = g_bus_get_sync (G_BUS_TYPE_STARTER, NULL, &err);
  if (err != NULL && error != NULL) {
    g_critical ("%s", err->message);
    g_propagate_error (error, err);
    return FALSE;
  }

  GVariantBuilder builder;
  g_variant_builder_init (&builder, G_VARIANT_TYPE ("(ussa{ss})"));
  g_variant_builder_add (&builder, "u", event);
  g_variant_builder_add (&builder, "s", message_id);
  if (response_to != NULL) {
    g_variant_builder_add (&builder, "s", response_to);
  } else {
    g_variant_builder_add (&builder, "s", "");
  }
  g_variant_builder_open (&builder, G_VARIANT_TYPE ("a{ss}"));
  if (data != NULL) {
    ygg_metadata_foreach (data, metadata_foreach_builder_add, &builder);
  }
  g_variant_builder_close (&builder);

  return g_dbus_connection_emit_signal (connection,
                                        NULL,
                                        priv->object_path,
                                        "com.redhat.Yggdrasil1.Worker1",
                                        "Event",
                                        g_variant_builder_end (&builder),
                                        error);
}

/**
 * ygg_worker_get_feature:
 * @worker: A #YggWorker instance.
 * @key: (transfer none): The key to look up in the features table.
 * @error: (nullable): The return location for an error.
 *
 * Looks up a value in the features table for @key.
 *
 * Returns: (nullable) (transfer none): The value from the features table.
 */
const gchar *
ygg_worker_get_feature (YggWorker    *self,
                        const gchar  *key,
                        GError      **error)
{
  YggWorkerPrivate *priv = ygg_worker_get_instance_private (self);

  const gchar *value = ygg_metadata_get (priv->features, key);
  if (value == NULL) {
      if (error != NULL) {
        *error = g_error_new (YGG_WORKER_ERROR,
                              YGG_WORKER_ERROR_MISSING_FEATURE,
                              "no value for key '%s'",
                              key);
      }
      return NULL;
  }

  return value;
}

/**
 * ygg_worker_set_feature:
 * @worker: A #YggWorker instance.
 * @key: (transfer none): The key to set in the features table.
 * @value: (transfer none): The value to set in the features table.
 * @error: The return location for an error.
 *
 * Stores @value in the features table for @key.
 *
 * Returns: TRUE if the key did not exist yet.
 */
gboolean
ygg_worker_set_feature (YggWorker    *self,
                        const gchar  *key,
                        gchar        *value,
                        GError      **error)
{
  YggWorkerPrivate *priv = ygg_worker_get_instance_private (self);

  GError *err = NULL;
  GDBusConnection *connection = g_bus_get_sync (G_BUS_TYPE_STARTER, NULL, &err);
  if (err != NULL && error != NULL) {
    g_critical ("%s", err->message);
    g_propagate_error (error, err);
    return FALSE;
  }

  gboolean exists = ygg_metadata_set (priv->features, key, value);

  GVariantBuilder builder;
  g_variant_builder_init (&builder, G_VARIANT_TYPE ("(sa{sv}as)"));
  g_variant_builder_add (&builder, "s", "com.redhat.Yggdrasil1.Worker1");
  g_variant_builder_open (&builder, G_VARIANT_TYPE ("a{sv}"));
  ygg_metadata_foreach (priv->features, metadata_foreach_builder_add_value, &builder);
  g_variant_builder_close (&builder);
  g_variant_builder_add_value (&builder, g_variant_new_array (G_VARIANT_TYPE_STRING, NULL, 0));
  GVariant *parameters = g_variant_builder_end (&builder);
  g_assert_null (err);
  if (!g_dbus_connection_emit_signal (connection, NULL, priv->object_path, "org.freedesktop.DBus.Properties", "PropertiesChanged", parameters, &err)) {
    g_error ("%s", err->message);
  }

  return exists;
}

/**
 * ygg_worker_set_rx_func:
 * @worker: A #YggWorker instance.
 * @func: (scope notified) (closure user_data): A #YggRxFunc callback.
 * @user_data: User data passed to @func when it is invoked.
 * @notify (nullable): A #GDestroyNotify that is called when the reference to
 * @func is dropped.
 *
 * Stores a pointer to a handler function that is invoked whenever data is
 * received by the worker.
 *
 * Returns: %TRUE if setting the function handler succeeded.
 */
gboolean
ygg_worker_set_rx_func (YggWorker      *self,
                        YggRxFunc       func,
                        gpointer        user_data,
                        GDestroyNotify  notify)
{
  YggWorkerPrivate *priv = ygg_worker_get_instance_private (self);

  if (priv->rx_func_data_notify != NULL) {
    priv->rx_func_data_notify (priv->rx_func_user_data);
  }

  priv->rx_func = func;
  priv->rx_func_user_data = user_data;
  priv->rx_func_data_notify = notify;

  return TRUE;
}

/**
 * ygg_worker_set_event_func:
 * @worker: A #YggWorker instance.
 * @func: (scope notified) (closure user_data): A #YggEventFunc callback.
 * @user_data: User data passed to @func when it is invoked.
 * @notify (nullable): A #GDestroyNotify that is called when the reference to
 * @func is dropped.
 *
 * Stores a pointer to a handler function that is invoked whenever an event
 * signal is received by the worker.
 *
 * Returns: %TRUE if setting the function handler succeeded.
 */
gboolean
ygg_worker_set_event_func (YggWorker      *self,
                           YggEventFunc    func,
                           gpointer        user_data,
                           GDestroyNotify  notify)
{
  YggWorkerPrivate *priv = ygg_worker_get_instance_private (self);

  if (priv->event_func_data_notify != NULL) {
    priv->event_func_data_notify (priv->event_func_user_data);
  }

  priv->event_func = func;
  priv->event_func_user_data = user_data;
  priv->event_func_data_notify = notify;

  return TRUE;
}

/**
 * ygg_worker_set_cancel_func:
 * @worker: A #YggWorker instance.
 * @func: (scope notified) (closure user_data): A #YggCancelFunc callback.
 * @user_data: User data passed to @func when it is invoked.
 * @notify (nullable): A #GDestroyNotify that is called when the reference to
 * @func is dropped.
 * 
 * Stores a pointer to a handler function that is invoked when a message
 * cancellation request is received by the worker.
 * 
 * Returns: %TRUE if setting the function handler succeeded.
*/
gboolean
ygg_worker_set_cancel_func (YggWorker      *self,
                            YggCancelFunc   func,
                            gpointer        user_data,
                            GDestroyNotify  notify)
{
  YggWorkerPrivate *priv = ygg_worker_get_instance_private (self);

  if (priv->cancel_func_data_notify != NULL) {
    priv->cancel_func_data_notify (priv->cancel_func_user_data);
  }

  priv->cancel_func = func;
  priv->cancel_func_user_data = user_data;
  priv->cancel_func_data_notify = notify;

  return TRUE;
}

static void
ygg_worker_constructed (GObject *object)
{
  YggWorker *self = YGG_WORKER (object);
  YggWorkerPrivate *priv = ygg_worker_get_instance_private (self);

  if (priv->features == NULL) {
    priv->features = ygg_metadata_new ();
  }

  G_OBJECT_CLASS (ygg_worker_parent_class)->constructed (object);
}

static void
ygg_worker_dispose (GObject *object)
{
  YggWorker *self = (YggWorker *)object;
  YggWorkerPrivate *priv = ygg_worker_get_instance_private (self);

  if (priv->rx_func_data_notify != NULL) {
    priv->rx_func_data_notify (priv->rx_func_user_data);
  }

  if (priv->event_func_data_notify != NULL) {
    priv->event_func_data_notify (priv->event_func_user_data);
  }

  if (priv->cancel_func_data_notify != NULL) {
    priv->cancel_func_data_notify (priv->cancel_func_user_data);
  }

  g_object_unref (priv->features);

  G_OBJECT_CLASS (ygg_worker_parent_class)->dispose (object);
}

static void
ygg_worker_finalize (GObject *object)
{
  YggWorker *self = (YggWorker *)object;
  YggWorkerPrivate *priv = ygg_worker_get_instance_private (self);

  g_free (priv->directive);
  g_free (priv->bus_name);
  g_free (priv->object_path);

  G_OBJECT_CLASS (ygg_worker_parent_class)->finalize (object);
}

static void
ygg_worker_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
  YggWorker *self = YGG_WORKER (object);
  YggWorkerPrivate *priv = ygg_worker_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_DIRECTIVE:
      g_value_set_string (value, priv->directive);
      break;
    case PROP_REMOTE_CONTENT:
      g_value_set_boolean (value, priv->remote_content);
      break;
    case PROP_FEATURES:
      g_value_set_object (value, priv->features);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
ygg_worker_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
  YggWorker *self = YGG_WORKER (object);
  YggWorkerPrivate *priv = ygg_worker_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_DIRECTIVE:
      g_free (priv->directive);
      priv->directive = g_value_dup_string (value);
      break;
    case PROP_REMOTE_CONTENT:
      priv->remote_content = g_value_get_boolean (value);
      break;
    case PROP_FEATURES:
      if (priv->features) {
        g_object_unref (priv->features);
      }
      priv->features = YGG_METADATA (g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
ygg_worker_class_init (YggWorkerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = ygg_worker_constructed;
  object_class->dispose = ygg_worker_dispose;
  object_class->finalize = ygg_worker_finalize;
  object_class->get_property = ygg_worker_get_property;
  object_class->set_property = ygg_worker_set_property;

  /**
   * YggWorker:directive:
   *
   * The unique identifier for the worker.
   */
  properties[PROP_DIRECTIVE] = g_param_spec_string ("directive", NULL, NULL, NULL,
                                                    G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY);

  /**
   * YggWorker:remote-content:
   *
   * When %TRUE, hints to the dispatcher that the worker expects data to be
   * retrieved from a remote location.
   */
  properties[PROP_REMOTE_CONTENT] = g_param_spec_boolean ("remote-content", NULL, NULL, FALSE,
                                                          G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY);

  /**
   * YggWorker:features:
   *
   * An optional mapping of key/value string pairs that the worker may use to
   * communicate state with the dispatcher.
   */
  properties[PROP_FEATURES] = g_param_spec_object ("features", NULL, NULL, YGG_TYPE_METADATA,
                                                  G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_properties (object_class, N_PROPS, properties);

  /**
   * YggWorker::connected:
   * @worker: The #YggWorker instance emitting the signal.
   * 
   * Emitted when the the worker is connected to the bus and ready to receive
   * messages.
   */
  signals[SIGNAL_CONNECTED] = g_signal_newv ("connected",
                                             G_TYPE_FROM_CLASS (object_class),
                                             G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS,
                                             NULL,
                                             NULL,
                                             NULL,
                                             NULL,
                                             G_TYPE_NONE,
                                             0,
                                             NULL);

  /**
   * YggWorker::disconnected:
   * @worker: The #YggWorker instance emitting the signal.
   * 
   * Emitted when the the worker is disconnected from the bus and is no longer
   * able to receive messages.
   */
  signals[SIGNAL_DISCONNECTED] = g_signal_newv ("disconnected",
                                                G_TYPE_FROM_CLASS (object_class),
                                                G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS,
                                                NULL,
                                                NULL,
                                                NULL,
                                                NULL,
                                                G_TYPE_NONE,
                                                0,
                                                NULL);

  GError *err = NULL;

  if (dispatcher_node_info == NULL) {
    g_autofree gchar *dispatcher_xml_data = NULL;
    g_assert (err == NULL);
    if (!g_file_get_contents (DISPATCHER_INTERFACE_XML_PATH, &dispatcher_xml_data, NULL, &err)) {
      if (err != NULL) {
        g_error ("%s", err->message);
      }
    }

    g_assert (err == NULL);
    dispatcher_node_info = g_dbus_node_info_new_for_xml (dispatcher_xml_data, &err);
    if (err != NULL) {
      g_error ("%s", err->message);
    }
    g_assert_nonnull (dispatcher_node_info->interfaces);
  }

  if (worker_node_info == NULL) {
    g_autofree gchar *worker_xml_data = NULL;
    g_assert (err == NULL);
    if (!g_file_get_contents (WORKER_INTERFACE_XML_PATH, &worker_xml_data, NULL, &err)) {
      if (err != NULL) {
        g_error ("%s", err->message);
      }
    }

    g_assert (err == NULL);
    worker_node_info = g_dbus_node_info_new_for_xml (worker_xml_data, &err);
    if (err != NULL ) {
      g_error ("%s", err->message);
    }
    g_assert_nonnull (worker_node_info->interfaces);
  }
}

static void
ygg_worker_init (YggWorker *self)
{
  YggWorkerPrivate *priv = ygg_worker_get_instance_private (self);

  priv->directive = NULL;
  priv->remote_content = FALSE;
}


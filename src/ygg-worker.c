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

/**
 * ReceiveData:
 * @worker: The instance of #YggWorker that is operating on the data.
 * @parameters: The parameters as passed to the Dispatch method.
 *
 * A data-only-struct containing worker and parameters necessary to execute
 * invoke_rx().
 */
typedef struct {
  YggWorker *worker;
  GVariant *parameters;
} ReceiveData;

/**
 * receive_data_new:
 * @worker: (transfer none): The worker instance.
 * @parameters: (transfer full): The parameters of the Dispatch method
 * invocation.
 *
 * Allocates and returns a new #ReceiveData instance.
 *
 * Returns: (transfer full): A newly allocated #ReceiveData.
 */
static ReceiveData *
receive_data_new (YggWorker *worker,
                  GVariant  *parameters)
{
  ReceiveData *data = g_rc_box_new0 (ReceiveData);
  data->worker = worker;
  data->parameters = parameters;
  return data;
}

/**
 * receive_data_release:
 * @data: The instance to be released.
 *
 * Releases @data and its owned members.
 */
static void
receive_data_release (ReceiveData *data)
{
  g_variant_unref (data->parameters);
  g_rc_box_release (data);
}

/**
 * TransmitData:
 * @addr: destination address of the data to be transmitted.
 * @id: a UUID.
 * @response_to: (nullable): a UUID the data is in response to
 *               or NULL.
 * @metadata: (nullable) (element-type utf8 utf8): a #GHashTable
 *            containing key-value pairs associated with the data or NULL.
 * @data: the data.
 *
 * A data-only-struct owning data needed to execute invoke_tx().
 */
typedef struct {
  gchar *addr;
  gchar *id;
  gchar *response_to;
  GHashTable *metadata;
  GBytes *data;
} TransmitData;

/**
 * transmit_data_new:
 * @addr: (transfer full): destination address of the data to be transmitted.
 * @id: (transfer full): a UUID.
 * @response_to: (transfer full) (nullable): a UUID the data is in response to
 *               or NULL.
 * @metadata: (transfer full) (nullable) (element-type utf8 utf8): a #GHashTable
 *            containing key-value pairs associated with the data or NULL.
 * @data: (transfer full): the data.
 *
 * Allocates and returns a new #TransmitData instance.
 *
 * Returns: (transfer full): A newly allocated #TransmitData.
 */
static TransmitData*
transmit_data_new (gchar *addr, gchar *id, gchar *response_to, GHashTable *metadata, GBytes *data)
{
  TransmitData *transmit_data = g_rc_box_new (TransmitData);
  transmit_data->addr = addr;
  transmit_data->id = id;
  transmit_data->response_to = response_to;
  transmit_data->metadata = metadata;
  transmit_data->data = data;
  return transmit_data;
}

/**
 * transmit_data_release:
 * @transmit_data: The instance to be released.
 *
 * Releases @transmit_data and its owned members.
 */
static void
transmit_data_free (TransmitData *transmit_data)
{
  g_free (transmit_data->addr);
  g_free (transmit_data->id);
  g_free (transmit_data->response_to);
  g_hash_table_destroy (transmit_data->metadata);
  g_bytes_unref (transmit_data->data);
  g_rc_box_release (transmit_data);
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
  gchar       *directive;
  gboolean     remote_content;
  GHashTable  *features;
  YggRxFunc    rx;
  YggEventFunc event_rx;
  guint        bus_id;
  gchar       *bus_name;
  gchar       *object_path;
} YggWorkerPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (YggWorker, ygg_worker, G_TYPE_OBJECT)

enum {
  PROP_DIRECTIVE = 1,
  PROP_REMOTE_CONTENT,
  PROP_FEATURES,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

/**
 * invoke_rx:
 * @user_data: (transfer none): the #ReceiveData containing parameters.
 *
 * A #GSourceFunc that handles com.redhat.Yggdrasil1.Worker1.Dispatch
 * asynchronously.
 *
 * Returns: #G_SOURCE_REMOVE, indicating that this callback should only be
 * invoked once.
 */
static gboolean
invoke_rx (gpointer user_data)
{
  g_debug ("invoke_rx");
  ReceiveData *args = (ReceiveData *) user_data;
  YggWorker *self = YGG_WORKER (args->worker);
  YggWorkerPrivate *priv = ygg_worker_get_instance_private (self);
  GVariant *parameters = args->parameters;
  GError *err = NULL;
  g_assert_nonnull (parameters);

  GVariantIter iter;
  g_variant_iter_init (&iter, parameters);

  gchar *addr = NULL;
  g_variant_iter_next (&iter, "s", &addr);
  g_debug ("addr = %s", addr);

  gchar *id = NULL;
  g_variant_iter_next (&iter, "s", &id);
  g_debug ("id = %s", id);

  gchar *response_to = NULL;
  g_variant_iter_next (&iter, "s", &response_to);
  g_debug ("response_to = %s", response_to);

  g_autoptr (GVariant) metadata_value = NULL;
  metadata_value = g_variant_iter_next_value (&iter);
  g_debug("metadata_value = %s", g_variant_print (metadata_value, TRUE));
  GVariantIter metadata_iter;
  g_variant_iter_init (&metadata_iter, metadata_value);
  GHashTable *metadata = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
  GVariant *child;
  while ((child = g_variant_iter_next_value (&metadata_iter))) {
    gchar *key;
    gchar *value;

    g_variant_get (child, "{ss}", &key, &value);
    g_hash_table_insert (metadata, key, value);

    g_variant_unref (child);
  }

  g_autoptr (GVariant) data = NULL;
  data = g_variant_iter_next_value (&iter);
  g_debug("data = %s", g_variant_print (data, TRUE));

  g_assert_null (err);
  if (!ygg_worker_emit_event (self, YGG_WORKER_EVENT_BEGIN, "", &err)) {
    if (err != NULL) {
      g_critical ("%s", err->message);
      goto out;
    }
  }

  g_assert_nonnull (priv->rx);
  priv->rx (addr, id, response_to, metadata, g_variant_get_data_as_bytes (data), self);

  g_assert_null (err);
  if (!ygg_worker_emit_event (self, YGG_WORKER_EVENT_END, "", &err)) {
    if (err != NULL) {
      g_critical ("%s", err->message);
      goto out;
    }
  }
out:
  receive_data_release (args);

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
  g_debug ("invoking invoke_tx");
  GTask *task = G_TASK (user_data);
  GError *err = NULL;

  TransmitData *task_data = (TransmitData *) g_task_get_task_data (task);
  g_return_val_if_fail (task_data != NULL, G_SOURCE_REMOVE);

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

  GVariantBuilder builder;
  g_variant_builder_init (&builder, G_VARIANT_TYPE ("(sssa{ss}ay)"));
  g_variant_builder_add (&builder, "s", task_data->addr);
  g_variant_builder_add (&builder, "s", task_data->id);
  g_variant_builder_add (&builder, "s", task_data->response_to);
  g_variant_builder_open (&builder, G_VARIANT_TYPE ("a{ss}"));
  GHashTableIter iter;
  g_hash_table_iter_init (&iter, task_data->metadata);
  gpointer key = NULL;
  gpointer value = NULL;
  while (g_hash_table_iter_next (&iter, &key, &value)) {
    g_variant_builder_add (&builder, "{ss}", (gchar *) key, (gchar *) value);
  }
  g_variant_builder_close (&builder);
  g_variant_builder_add_value (&builder, g_variant_new_bytestring (g_bytes_get_data (task_data->data, NULL)));
  GVariant *transmit_args = g_variant_builder_end (&builder);
  g_debug ("Transmit parameters: %s", g_variant_print (transmit_args, TRUE));

  g_dbus_proxy_call (proxy,
                     "Transmit",
                     transmit_args,
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

  g_debug ("%s parameters: %s", method_name, g_variant_print (parameters, TRUE));

  if (g_strcmp0 (method_name, "Dispatch") == 0) {
    ReceiveData *args = receive_data_new (self, g_variant_ref_sink (parameters));

    g_dbus_method_invocation_return_value (invocation, NULL);
    g_idle_add (invoke_rx, args);
  } else {
    g_dbus_method_invocation_return_error (invocation,
                                           YGG_WORKER_ERROR,
                                           YGG_WORKER_ERROR_UNKNOWN_METHOD,
                                           "unknown method: %s",
                                           method_name);
  }
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
    g_variant_builder_init (&builder, G_VARIANT_TYPE("a{ss}"));

    GHashTableIter iter;
    g_hash_table_iter_init (&iter, priv->features);
    gpointer k = NULL;
    gpointer v = NULL;
    while (g_hash_table_iter_next (&iter, &k, &v)) {
      g_variant_builder_add (&builder, "{ss}", (gchar *) k, (gchar *) v);
    }

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

  priv->event_rx (event);
}


static void
on_bus_acquired (GDBusConnection *connection,
                 const gchar     *name,
                 gpointer         user_data)
{
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
}

static void
on_name_acquired (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
  g_debug ("acquired name %s", name);
}

static void
on_name_lost (GDBusConnection *connection,
              const gchar     *name,
              gpointer         user_data)
{
  g_debug ("lost name %s", name);
}

/**
 * ygg_worker_new: (constructor)
 * @directive: (transfer none): The worker directive name.
 * @remote_content: The worker requires content from a remote URL.
 * @features: (transfer full) (element-type utf8 utf8) (nullable): An initial table of
 *            values to use as the worker's features map.
 *
 * Creates a new #YggWorker instance.
 *
 * Returns: (transfer full): A new #YggWorker instance.
 */
YggWorker *
ygg_worker_new (const gchar *directive,
                gboolean     remote_content,
                GHashTable  *features)
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
  g_assert_nonnull (priv->rx);

  if (g_regex_match_simple ("-", priv->directive, 0, 0)) {
    if (error != NULL) {
      g_error_new (YGG_WORKER_ERROR,
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

gboolean
ygg_worker_transmit_finish (YggWorker     *self,
                            GAsyncResult  *res,
                            gint          *response_code,
                            GHashTable   **response_metadata,
                            GBytes       **response_data,
                            GError       **error)
{
  g_debug ("ygg_worker_transmit_finish");
  g_return_val_if_fail (g_task_is_valid (res, self), FALSE);
  GTask *task = G_TASK (res);
  GError *err = NULL;

  GVariant *response = g_task_propagate_pointer (task, &err);
  if (err != NULL && error != NULL) {
    g_propagate_error (error, err);
    return FALSE;
  }
  g_debug ("%s", g_variant_print (response, TRUE));

  GVariantIter iter;
  g_variant_iter_init (&iter, response);
  g_variant_iter_next (&iter, "i", response_code);
  GVariant *response_metadata_value = NULL;
  response_metadata_value = g_variant_iter_next_value (&iter);
  GVariantIter metadata_iter;
  g_variant_iter_init (&metadata_iter, response_metadata_value);
  gchar *key = NULL;
  gchar *value = NULL;
  *response_metadata = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
  while (g_variant_iter_next (&metadata_iter, "{ss}", &key, &value)) {
    g_hash_table_insert (*response_metadata, key, value);
  }
  GVariant *data_value;
  data_value = g_variant_iter_next_value (&iter);
  gsize len = 0;
  gchar *data = g_variant_dup_bytestring (data_value, &len);
  *response_data = g_bytes_new_take (data, len);

  return *response_code >= 0;
}

/**
 * ygg_worker_transmit:
 * @worker: A #YggWorker.
 * @addr: (transfer full): destination address of the data to be transmitted.
 * @id: (transfer full): a UUID.
 * @response_to: (transfer full) (nullable): a UUID the data is in response to
 *               or NULL.
 * @metadata: (transfer full) (nullable) (element-type utf8 utf8): a #GHashTable
 *            containing key-value pairs associated with the data or NULL.
 * @data: (transfer full): the data.
 * @cancellable: (nullable): a #GCancellable or NULL.
 * @callback: a #GAsyncReadyCallback to be invoked when the task is complete.
 * @user_data: (nullable): optional data passed into @callback.
 *
 * Invokes the com.redhat.Yggdrasil1.Dispatcher1.Transmit D-Bus method
 * asynchronously. To receive the response from the D-Bus method, call
 * @ygg_worker_transmit_finish.
 */
void
ygg_worker_transmit (YggWorker           *self,
                     gchar               *addr,
                     gchar               *id,
                     gchar               *response_to,
                     GHashTable          *metadata,
                     GBytes              *data,
                     GCancellable        *cancellable,
                     GAsyncReadyCallback  callback,
                     gpointer             user_data)
{
  GTask *task = g_task_new (self, cancellable, callback, user_data);
  TransmitData *transmit_data = transmit_data_new (addr, id, response_to, metadata, data);
  g_task_set_task_data (task, transmit_data, (GDestroyNotify) transmit_data_free);
  GSource *source = g_idle_source_new ();
  g_task_attach_source (task, source, invoke_tx);
}

gboolean ygg_worker_emit_event (YggWorker       *self,
                                YggWorkerEvent   event,
                                const gchar     *message,
                                GError         **error)
{
  GError *err = NULL;

  YggWorkerPrivate *priv = ygg_worker_get_instance_private (self);

  g_assert_null (err);
  GDBusConnection *connection = g_bus_get_sync (G_BUS_TYPE_STARTER, NULL, &err);
  if (err != NULL && error != NULL) {
    g_critical ("%s", err->message);
    g_propagate_error (error, err);
    return FALSE;
  }

  return g_dbus_connection_emit_signal (connection,
                                        NULL,
                                        priv->object_path,
                                        "com.redhat.Yggdrasil1.Worker1",
                                        "Event",
                                        g_variant_new ("(us)", event, message),
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
 * Returns: (nullable) (transfer full): The value from the features table.
 */
gchar *
ygg_worker_get_feature (YggWorker    *self,
                        const gchar  *key,
                        GError      **error)
{
  YggWorkerPrivate *priv = ygg_worker_get_instance_private (self);

  gpointer value;
  if (g_hash_table_lookup_extended (priv->features, key, NULL, &value)) {
    return (gchar *) value;
  }
  if (error != NULL) {
    g_error_new (YGG_WORKER_ERROR, YGG_WORKER_ERROR_MISSING_FEATURE, "no value for key '%s'", key);
  }
  return NULL;
}

/**
 * ygg_worker_set_feature:
 * @worker: A #YggWorker instance.
 * @key: (transfer none): The key to set in the features table.
 * @value: (transfer full): The value to set in the features table.
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

  gboolean exists = g_hash_table_insert (priv->features, g_strdup (key), value);

  GVariantBuilder builder;
  g_variant_builder_init (&builder, G_VARIANT_TYPE ("(sa{sv}as)"));
  g_variant_builder_add (&builder, "s", "com.redhat.Yggdrasil1.Worker1");
  g_variant_builder_open (&builder, G_VARIANT_TYPE ("a{sv}"));
  GHashTableIter iter;
  g_hash_table_iter_init (&iter, priv->features);
  gpointer fkey = NULL;
  gpointer fvalue = NULL;
  while (g_hash_table_iter_next (&iter, &fkey, &fvalue)) {
    g_variant_builder_add (&builder, "{sv}", (gchar *) fkey, g_variant_new_string ((gchar *) fvalue));
  }
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
 * @rx: (scope call): A #YggRxFunc callback.
 *
 * Stores a pointer to a handler function that is invoked whenever data is
 * received by the worker.
 *
 * Returns: %TRUE if setting the function handler succeeded.
 */
gboolean
ygg_worker_set_rx_func (YggWorker *self,
                        YggRxFunc  rx)
{
  YggWorkerPrivate *priv = ygg_worker_get_instance_private (self);
  priv->rx = rx;
  return TRUE;
}

/**
 * ygg_worker_set_event_func:
 * @worker: A #YggWorker instance.
 * @event: (scope call): A #YggEventFunc callback.
 *
 * Stores a pointer to a handler function that is invoked whenever an event
 * signal is received by the worker.
 *
 * Returns: %TRUE if setting the function handler succeeded.
 */
gboolean
ygg_worker_set_event_func (YggWorker    *self,
                           YggEventFunc  event)
{
  YggWorkerPrivate *priv = ygg_worker_get_instance_private (self);
  priv->event_rx = event;
  return TRUE;
}

static void
ygg_worker_finalize (GObject *object)
{
  YggWorker *self = (YggWorker *)object;
  YggWorkerPrivate *priv = ygg_worker_get_instance_private (self);

  g_hash_table_unref (priv->features);
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
      g_value_set_boxed (value, priv->features);
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
        g_hash_table_unref (priv->features);
      }
      priv->features = (GHashTable *) g_value_get_boxed (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
ygg_worker_constructed (GObject *object)
{
  YggWorker *self = YGG_WORKER (object);
  YggWorkerPrivate *priv = ygg_worker_get_instance_private (self);

  if (priv->features == NULL) {
    priv->features = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
  }
}

static void
ygg_worker_class_init (YggWorkerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = ygg_worker_constructed;
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
  properties[PROP_FEATURES] = g_param_spec_boxed ("features", NULL, NULL, G_TYPE_HASH_TABLE,
                                                  G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_properties (object_class, N_PROPS, properties);

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
  priv->features = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
}


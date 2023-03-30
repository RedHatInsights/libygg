/*
 * ygg-metadata.c
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

#include "ygg-metadata.h"

G_DEFINE_QUARK (ygg-metadata-error-quark, ygg_metadata_error)

struct _YggMetadata
{
  GObject parent_instance;
};

typedef struct
{
  GHashTable *metadata;
} YggMetadataPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (YggMetadata, ygg_metadata, G_TYPE_OBJECT)

/**
 * ygg_metadata_new: (constructor)
 *
 * Creates a new #YggMetadata instance.
 *
 * Returns: (transfer full): A new #YggMetadata instance.
 */
YggMetadata *
ygg_metadata_new (void)
{
  return g_object_new (YGG_TYPE_METADATA, NULL);
}


YggMetadata *
ygg_metadata_new_from_variant (GVariant  *variant,
                               GError   **error)
{
  if (!g_variant_check_format_string (variant, "a{ss}", FALSE)) {
    if (error != NULL) {
      *error = g_error_new (YGG_METADATA_ERROR,
                            YGG_METADATA_ERROR_INVALID_FORMAT_STRING,
                            "%s is not a valid format string",
                            g_variant_print (variant, TRUE));
      return NULL;
    }
  }

  YggMetadata *obj = ygg_metadata_new ();

  GVariantIter iter;
  gchar *key;
  gchar *value;
  g_variant_iter_init (&iter, variant);
  while (g_variant_iter_next (&iter, "{&s&s}", &key, &value)) {
    ygg_metadata_set (obj, key, value);
  }

  return obj;
}

/**
 * ygg_metadata_get:
 * @metadata: A #YggMetadata.
 * @key: (transfer none): The key to look up.
 *
 * Looks up a key in the metadata table.
 *
 * Returns: (nullable): The value for @key, or %NULL if the key is not found.
 */
const gchar *
ygg_metadata_get (YggMetadata *self,
                  const gchar *key)
{
  YggMetadataPrivate *priv = ygg_metadata_get_instance_private (self);

  gpointer value = g_hash_table_lookup (priv->metadata, key);
  if (value != NULL) {
    return (const gchar *) value;
  }
  return NULL;
}

/**
 * ygg_metadata_set:
 * @metadata: A #YggMetadata;
 * @key: (transfer none): A key to set.
 * @value: (transfer none): The value to associate with the key.
 *
 * Inserts a new key and value into the metadata table.
 *
 * Returns: %TRUE if the key did not exist.
 */
gboolean
ygg_metadata_set (YggMetadata *self,
                  const gchar *key,
                  const gchar *value)
{
  YggMetadataPrivate *priv = ygg_metadata_get_instance_private (self);

  return g_hash_table_insert (priv->metadata, (gpointer) g_strdup (key), (gpointer) g_strdup (value));
}

/**
 * ygg_metadata_foreach:
 * @metadata: A #YggMetadata;
 * @func: (scope call) (closure user_data): The function to call for each
 * key/value pair.
 * @user_data: User data to pass to the function.
 *
 * Calls the given function for each of the key/value pairs in the #YggMetadata.
 */
void
ygg_metadata_foreach (YggMetadata            *self,
                      YggMetadataForeachFunc  func,
                      gpointer                user_data)
{
  YggMetadataPrivate *priv = ygg_metadata_get_instance_private (self);

  GHashTableIter iter;
  g_hash_table_iter_init (&iter, priv->metadata);
  gpointer key = NULL;
  gpointer value = NULL;
  while (g_hash_table_iter_next (&iter, &key, &value)) {
    func ((const gchar *) key, (const gchar *) value, user_data);
  }
}

static void
builder_foreach (const gchar *key,
                 const gchar *value,
                 gpointer     user_data)
{
  GVariantBuilder *builder = (GVariantBuilder *) user_data;
  g_variant_builder_add (builder, "{ss}", key, value);
}

GVariant *
ygg_metadata_to_variant (YggMetadata *self)
{
  GVariantBuilder builder;
  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{ss}"));
  ygg_metadata_foreach (self, builder_foreach, &builder);
  return g_variant_builder_end (&builder);
}

static void
ygg_metadata_finalize (GObject *object)
{
  YggMetadata *self = (YggMetadata *) object;
  YggMetadataPrivate *priv = ygg_metadata_get_instance_private (self);

  g_hash_table_unref (priv->metadata);

  G_OBJECT_CLASS (ygg_metadata_parent_class)->finalize (object);
}

static void
ygg_metadata_class_init (YggMetadataClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = ygg_metadata_finalize;
}

static void
ygg_metadata_init (YggMetadata *self)
{
  YggMetadataPrivate *priv = ygg_metadata_get_instance_private (self);
  priv->metadata = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
}

/*
 * ygg-metadata.h
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

G_BEGIN_DECLS

/**
 * YGG_METADATA_ERROR:
 *
 * Error domain for #YggMetadata. Errors in this domain will be from the
 * #YggMetadataError enumeration.
 */
#define YGG_METADATA_ERROR ygg_metadata_error_quark ()
GQuark ygg_metadata_error_quark (void);

/**
 * YggMetadataError:
 * @YGG_METADATA_ERROR_INVALID_FORMAT_STRING: A #GVariant contains an invalid
 * format string.
 */
typedef enum
{
  YGG_METADATA_ERROR_INVALID_FORMAT_STRING
} YggMetadataError;

/**
 * YggMetadataForeachFunc:
 * @key: (transfer none): A key.
 * @value: (transfer none): The value corresponding to the key.
 * @user_data: (closure): User data passed to ygg_metadata_foreach().
 *
 * Signature for the type of function passed to ygg_metadata_foreach(). It is
 * called with each key/value pair, together with the @user_data parameter
 * passed to ygg_metadata_foreach().
 */
typedef void (* YggMetadataForeachFunc) (const gchar *key,
                                         const gchar *value,
                                         gpointer     user_data);

#define YGG_TYPE_METADATA (ygg_metadata_get_type())

G_DECLARE_FINAL_TYPE (YggMetadata, ygg_metadata, YGG, METADATA, GObject)

YggMetadata *
ygg_metadata_new (void);

YggMetadata *
ygg_metadata_new_from_variant (GVariant  *variant,
                               GError   **error);

const gchar *
ygg_metadata_get (YggMetadata *metadata,
                  const gchar *key);

gboolean
ygg_metadata_set (YggMetadata *metadata,
                  const gchar *key,
                  const gchar *value);

void
ygg_metadata_foreach (YggMetadata            *metadata,
                      YggMetadataForeachFunc  func,
                      gpointer                user_data);

GVariant *
ygg_metadata_to_variant (YggMetadata *metadata);

G_END_DECLS

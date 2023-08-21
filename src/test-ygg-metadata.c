/*
 * test-ygg-metadata.c
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
#include <locale.h>

#include "ygg.h"

static void
test_ygg_metadata_set (void)
{
  g_autoptr (YggMetadata) metadata = ygg_metadata_new ();
  g_assert_true (ygg_metadata_set (metadata, "ke", "ka"));
  g_assert_true (ygg_metadata_set (metadata, "he", "ha"));
  g_assert_false (ygg_metadata_set (metadata, "ke", "ki"));
}

static void
test_ygg_metadata_to_variant (void)
{
  g_autoptr (YggMetadata) metadata = ygg_metadata_new ();
  g_assert_true (ygg_metadata_set (metadata, "ke", "ka"));
  g_autoptr(GVariant) variant = ygg_metadata_to_variant (metadata);
  g_autofree gchar *pretty_printed_variant = g_variant_print (variant, TRUE);
  g_assert_cmpstr (pretty_printed_variant, ==, "{'ke': 'ka'}");
  g_assert_true (g_variant_check_format_string (variant, "a{ss}", FALSE));
}

static void
test_ygg_metadata_new_from_variant (void)
{
  GError *err = NULL;
  g_autoptr (GVariant) variant = g_variant_new_parsed ("{'ke': 'ka'}");
  g_autoptr (YggMetadata) metadata = ygg_metadata_new_from_variant (variant, &err);
  g_assert_null (err);
  g_assert_cmpstr (ygg_metadata_get (metadata, "ke"), ==, "ka");
}

static void
_foreach (const gchar *key,
          const gchar *val,
          gpointer     user_data)
{
  GString *output = (GString *) user_data;
  g_string_append_printf (output, "'%s' = '%s'\n", key, val);
}

static void
test_ygg_metadata_foreach (void)
{
  GError *err = NULL;
  g_autoptr (GVariant) variant = g_variant_new_parsed ("{'key': 'val', 'a': 'b'}");
  g_autoptr (YggMetadata) metadata = ygg_metadata_new_from_variant (variant, &err);
  g_assert_null (err);
  g_autoptr (GString) output = g_string_new (NULL);
  ygg_metadata_foreach (metadata, _foreach, output);
  g_assert_cmpstr (output->str, ==, "'key' = 'val'\n'a' = 'b'\n");
}

int
main (int   argc,
      char *argv[])
{
  setlocale (LC_ALL, "");
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/ygg/metadata/set", test_ygg_metadata_set);
  g_test_add_func ("/ygg/metadata/to_variant", test_ygg_metadata_to_variant);
  g_test_add_func ("/ygg/metadata/new_from_variant", test_ygg_metadata_new_from_variant);
  g_test_add_func ("/ygg/metadata/foreach", test_ygg_metadata_foreach);

  return g_test_run ();
}

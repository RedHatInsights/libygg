/*
 * test-ygg-worker.c
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

typedef struct {
  GTestDBus *dbus;
  YggWorker *worker;
} TestFixture;

static void
handle_rx (YggWorker   *worker,
           gchar       *addr,
           gchar       *id,
           gchar       *response_to,
           YggMetadata *metadata,
           GBytes      *data,
           gpointer     user_data)
{
}

static void
fixture_setup (TestFixture   *fixture,
               gconstpointer  user_data)
{
  g_autofree gchar *relative, *servicesdir;

  /* Create the global dbus-daemon for this test suite
   */
  fixture->dbus = g_test_dbus_new (G_TEST_DBUS_NONE);

  /* Add the private directory with our in-tree service files.
   */
  relative = g_test_build_filename (G_TEST_BUILT, "services", NULL);
  servicesdir = g_canonicalize_filename (relative, NULL);
  g_test_dbus_add_service_dir (fixture->dbus, servicesdir);

  /* Start the private D-Bus daemon
   */
  g_test_dbus_up (fixture->dbus);

  /* Create the proxy that we're going to test
   */
  fixture->worker = ygg_worker_new ("ygg_worker_test", FALSE, NULL);
  ygg_worker_set_rx_func (fixture->worker, handle_rx, NULL, NULL);
}

static void
fixture_teardown (TestFixture   *fixture,
                  gconstpointer  user_data)
{
  /* Tear down the proxy
   */
  if (fixture->worker)
    g_object_unref (fixture->worker);

  /* Stop the private D-Bus daemon
   */
  g_test_dbus_down (fixture->dbus);
  g_object_unref (fixture->dbus);
}

static void
test_worker_connect (TestFixture   *fixture,
                     gconstpointer  user_data)
{
  GError *error = NULL;
  gboolean got = FALSE;

  got = ygg_worker_connect (fixture->worker, &error);

  g_assert_true (got);
  g_assert_no_error (error);
}

int
main (int   argc,
      char *argv[])
{
  setlocale (LC_ALL, "");
  g_test_init (&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);

  g_test_add ("/ygg/worker/connect",
              TestFixture,
              NULL,
              fixture_setup,
              test_worker_connect,
              fixture_teardown);

  return g_test_run ();
}

/*
 * ngfd - Non-graphic feedback daemon
 *
 * Copyright (C) 2010 Nokia Corporation.
 * Contact: Xun Chen <xun.chen@nokia.com>
 *
 * This work is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This work is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this work; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <glib.h>
#include <gio/gio.h>
#include <stdio.h>

#include <unistd.h>
#include <sys/inotify.h>

#include <ngf/log.h>
#include <ngf/core.h>
#include "profile-plugin.h"
#include "session.h"

#define LOG_CAT                      "session: "
#define SESSION_BUS_ADDRESS_FILENAME "/tmp/session_bus_address.user"
#define EVENT_SIZE                   (sizeof (struct inotify_event))
#define BUF_LEN                      (1024 * (EVENT_SIZE + 16))
#define POLL_TIMEOUT                 5

static DBusConnection *session_bus = NULL;
static NCore          *g_core      = NULL;

static gint        inotify_fd      = 0;
static gint        inotify_wd      = 0;
static GIOChannel *inotify_channel = NULL;
static guint       inotify_source  = 0;
static guint       poll_id         = 0;

static gboolean    poll_session_bus_address_file (gpointer userdata);
static void        setup_file_polling            ();
static gboolean    setup_file_watcher            ();
static void        stop_file_polling             ();
static void        close_file_watcher            ();
static gchar*      parse_address                 (const char *buf);
static gchar*      load_session_bus_address      (const char *filename);
static gboolean    session_new_bus               (const char *address);
static void        session_reconnect             ();



static gboolean
poll_session_bus_address_file (gpointer userdata)
{
    (void) userdata;

    gchar *address = NULL;

    if (g_file_test (SESSION_BUS_ADDRESS_FILENAME, G_FILE_TEST_EXISTS)) {
        N_DEBUG (LOG_CAT "session bus address file exists.");

        if ((address = load_session_bus_address (SESSION_BUS_ADDRESS_FILENAME)) != NULL) {
            stop_file_polling  ();
            session_new_bus    (address);
            g_free             (address);
            setup_file_watcher ();
            return FALSE;
        }
    }

    return TRUE;
}

static void
setup_file_polling ()
{
    N_DEBUG (LOG_CAT "polling for session bus address file every %d seconds.",
        POLL_TIMEOUT);

    if (poll_id > 0) {
        g_source_remove (poll_id);
        poll_id = 0;
    }

    poll_id = g_timeout_add_seconds (POLL_TIMEOUT,
        poll_session_bus_address_file, NULL);
}

static void
stop_file_polling ()
{
    if (poll_id > 0) {
        N_DEBUG (LOG_CAT "stopping session bus address file polling.");
        g_source_remove (poll_id);
        poll_id = 0;
    }
}

static gboolean
inotify_watch (GIOChannel *source, GIOCondition condition, gpointer userdata)
{
    char buf[BUF_LEN];
    int len, i = 0, fd = 0;
    gchar *address = NULL;
    gboolean success = FALSE;

    struct inotify_event *event = NULL;

    (void) userdata;

    if (condition & G_IO_IN) {

        fd = g_io_channel_unix_get_fd (source);
        len = read (fd, buf, BUF_LEN);

        while (i < len) {
            event = (struct inotify_event *) &buf[i];

            if (event->mask & IN_DELETE_SELF) {

                N_DEBUG (LOG_CAT "session bus address file was removed.");
                close_file_watcher ();
                setup_file_polling ();

                return FALSE;
            }

            if ((event->mask & IN_MODIFY)) {

                success = FALSE;

                if ((address = load_session_bus_address (SESSION_BUS_ADDRESS_FILENAME)) != NULL) {
                    if (session_new_bus (address)) {
                        session_reconnect ();
                        success = TRUE;
                    }
                }

                if (!success) {
                    close_file_watcher ();
                    setup_file_polling ();
                    return FALSE;
                }
            }

            i += EVENT_SIZE + event->len;
        }

    }

    return TRUE;
}

static gboolean
setup_file_watcher ()
{
    if ((inotify_fd = inotify_init ()) < 0)
        goto failed;

    if ((inotify_wd = inotify_add_watch (inotify_fd,
         SESSION_BUS_ADDRESS_FILENAME, IN_MODIFY | IN_ATTRIB | IN_DELETE_SELF)) < 0)
        goto failed;

    if ((inotify_channel = g_io_channel_unix_new (inotify_fd)) == NULL)
        goto failed;

    if ((inotify_source = g_io_add_watch (inotify_channel,
         G_IO_IN | G_IO_PRI, inotify_watch, NULL)) == 0)
        goto failed;

    return TRUE;

failed:
    close_file_watcher ();
    return FALSE;
}

static void
close_file_watcher ()
{
    if (inotify_source > 0) {
        g_source_remove (inotify_source);
        inotify_source = 0;
    }

    if (inotify_channel) {
        g_io_channel_unref (inotify_channel);
        inotify_channel = NULL;
    }

    if (inotify_wd >= 0) {
        inotify_rm_watch (inotify_fd, inotify_wd);
        inotify_wd = 0;
    }

    if (inotify_fd >= 0) {
        close (inotify_fd);
        inotify_fd = 0;
    }
}

static gchar*
parse_address (const char *buf)
{
    gchar **lines   = NULL;
    gchar **tokens  = NULL;
    gchar **i       = NULL;
    gchar  *address = NULL;

    if (!buf)
        return NULL;

    lines = g_strsplit (buf, "\n", 2);

    for (i = lines; i; ++i) {
        tokens = g_strsplit (*i, "=", 2);

        if (tokens[0] && tokens[1]) {
            if (g_str_has_suffix (tokens[0], "DBUS_SESSION_BUS_ADDRESS")) {
                address = g_strdup (tokens[1]);
                g_strfreev (tokens);
                g_strfreev (lines);
                return address;
            }
        }

        g_strfreev (tokens);
    }

    g_strfreev (lines);

    return NULL;
}

static gchar*
load_session_bus_address (const char *filename)
{
    FILE   *fp         = NULL;
    size_t  file_size  = 0;
    size_t  bytes_read = 0;
    char   *buf        = NULL;
    gchar  *address    = NULL;

    if (!filename)
        return NULL;

    if ((fp = fopen (filename, "r")) == NULL) {
        N_DEBUG (LOG_CAT "no address file %s", filename);
        return NULL;
    }

    fseek (fp, 0L, SEEK_END);
    file_size = ftell (fp);
    fseek (fp, 0L, SEEK_SET);

    if (file_size > 0) {
        if ((buf = (char*) g_try_malloc0 (sizeof (char) * file_size)) == NULL) {
            N_WARNING (LOG_CAT "failed to allocate memory to read session bus "
                               "address file.");
            fclose (fp);
            return NULL;
        }

        bytes_read = fread (buf, sizeof (char), file_size, fp);
        if (bytes_read != file_size) {
            N_WARNING (LOG_CAT "failed to read the session bus address file.");
            g_free (buf);
            fclose (fp);
            return NULL;
        }

        if ((address = parse_address (buf)) != NULL) {
            N_DEBUG (LOG_CAT "parsed DBus session bus address: %s", address);
        }
        else {
            N_WARNING (LOG_CAT "failed to parse DBus session bus address.");
        }

        g_free (buf);
    }

    fclose (fp);

    return address;
}

static gboolean
session_new_bus (const char *address)
{
    DBusError error;

    dbus_error_init (&error);

    if (session_bus != NULL) {
        N_DEBUG (LOG_CAT "received session bus address '%s'", address);
        dbus_connection_unref (session_bus);
        session_bus = NULL;
    }
    else
        N_DEBUG (LOG_CAT "received new session bus address '%s'", address);

    if ((session_bus = dbus_connection_open (address, &error)) == NULL ||
        !dbus_bus_register (session_bus, &error))
    {
        if (dbus_error_is_set(&error)) {
            N_WARNING (LOG_CAT "failed to connect to session bus %s (%s)",
                address, error.message);

            dbus_error_free (&error);
        }
        else
            N_WARNING (LOG_CAT "failed to connect to session bus %s", address);

        return FALSE;
    }

    session_reconnect ();

    return TRUE;
}

static void
session_reconnect ()
{
    profile_plugin_reconnect (g_core, session_bus);
}

static gboolean
session_bus_connect ()
{
    DBusError error;
    dbus_error_init(&error);

    if (!session_bus) {
        session_bus = dbus_bus_get(DBUS_BUS_SESSION, &error);
        if (dbus_error_is_set(&error)) {
            N_DEBUG (LOG_CAT "Could not connect to DBus session bus.");
            return FALSE;
        }
    }

    N_DEBUG (LOG_CAT "Connected to DBus session bus.");
    session_reconnect ();

    return TRUE;
}

int
session_initialize (NCore *core)
{
    gchar *address = NULL;

    g_core = core;

    /* before polling for address filename or anything try to
     * open session bus connection. */
    if (session_bus_connect())
        return TRUE;

    /* if the session bus address file exists already, then try to load
       the address from the session file. */

    if (!g_file_test (SESSION_BUS_ADDRESS_FILENAME, G_FILE_TEST_EXISTS)) {
        setup_file_polling ();
        return TRUE;
    }

    if ((address = load_session_bus_address (SESSION_BUS_ADDRESS_FILENAME)) != NULL) {
        session_new_bus    (address);
        g_free             (address);
        setup_file_watcher ();
    }
    else {
        setup_file_polling ();
    }

    return TRUE;
}

void
session_shutdown ()
{
    stop_file_polling  ();
    close_file_watcher ();
}

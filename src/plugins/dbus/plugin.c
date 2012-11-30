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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <glib.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <stdint.h>

#include <ngf/log.h>
#include <ngf/value.h>
#include <ngf/proplist.h>
#include <ngf/plugin.h>
#include <ngf/request.h>
#include <ngf/inputinterface.h>

N_PLUGIN_NAME        ("dbus")
N_PLUGIN_VERSION     ("0.1")
N_PLUGIN_DESCRIPTION ("D-Bus interface")

#define LOG_CAT "dbus: "

#define NGF_DBUS_PROXY_NAME   "com.nokia.NonGraphicFeedback1"
#define NGF_DBUS_NAME         "com.nokia.NonGraphicFeedback1.Backend"
#define NGF_DBUS_PATH         "/com/nokia/NonGraphicFeedback1"
#define NGF_DBUS_IFACE        "com.nokia.NonGraphicFeedback1"

#define NGF_DBUS_STATUS       "Status"
#define NGF_DBUS_METHOD_PLAY  "Play"
#define NGF_DBUS_METHOD_STOP  "Stop"
#define NGF_DBUS_METHOD_PAUSE "Pause"

#define NGF_DBUS_PROPERTY     "dbus.event.id"

#define RINGTONE_STOP_TIMEOUT 200

static gboolean          msg_parse_variant       (DBusMessageIter *iter,
                                                  NProplist *proplist,
                                                  const char *key);
static gboolean          msg_parse_dict          (DBusMessageIter *iter,
                                                  NProplist *proplist);
static gboolean          msg_get_properties      (DBusMessageIter *iter,
                                                  NProplist **properties);
static DBusHandlerResult dbusif_message_function (DBusConnection *connection,
                                                  DBusMessage *msg,
                                                  void *userdata);

static NRequest*         dbusif_lookup_request   (NInputInterface *iface,
                                                  uint32_t event_id);
static int               dbusif_initialize       (NInputInterface *iface);
static void              dbusif_shutdown         (NInputInterface *iface);
static void              dbusif_send_error       (NInputInterface *iface,
                                                  NRequest *request,
                                                  const char *err_msg);
static void              dbusif_send_reply       (NInputInterface *iface,
                                                  NRequest *request,
                                                  int code);

typedef struct _DBusInterfaceData
{
    DBusConnection  *connection;
    NInputInterface *iface;
    uint32_t event_id;
} DBusInterfaceData;

static DBusInterfaceData *g_data = NULL;

static gboolean
msg_parse_variant (DBusMessageIter *iter, NProplist *proplist, const char *key)
{
    DBusMessageIter variant;

    const char *str_value = NULL;
    dbus_uint32_t uint_value;
    dbus_int32_t int_value;
    dbus_bool_t boolean_value;

    if (!key)
        return FALSE;

    if (dbus_message_iter_get_arg_type (iter) != DBUS_TYPE_VARIANT)
        return FALSE;

    dbus_message_iter_recurse (iter, &variant);

    switch (dbus_message_iter_get_arg_type (&variant)) {
        case DBUS_TYPE_STRING:
            dbus_message_iter_get_basic (&variant, &str_value);
            n_proplist_set_string (proplist, key, str_value);
            return TRUE;

        case DBUS_TYPE_UINT32:
            dbus_message_iter_get_basic (&variant, &uint_value);
            n_proplist_set_uint (proplist, key, uint_value);
            return TRUE;

        case DBUS_TYPE_INT32:
            dbus_message_iter_get_basic (&variant, &int_value);
            n_proplist_set_int (proplist, key, int_value);
            return TRUE;

        case DBUS_TYPE_BOOLEAN:
            dbus_message_iter_get_basic (&variant, &boolean_value);
            n_proplist_set_bool (proplist, key, boolean_value ? TRUE : FALSE);
            return TRUE;

        default:
            break;
    }

    return FALSE;
}

static gboolean
msg_parse_dict (DBusMessageIter *iter, NProplist *proplist)
{
    const char      *key = NULL;
    DBusMessageIter  dict;

    /* Recurse to the dict entry */

    if (dbus_message_iter_get_arg_type (iter) != DBUS_TYPE_DICT_ENTRY)
        return FALSE;

    dbus_message_iter_recurse (iter, &dict);

    /* Get the key for the dict entry */

    if (dbus_message_iter_get_arg_type (&dict) != DBUS_TYPE_STRING)
        return FALSE;

    dbus_message_iter_get_basic (&dict, &key);
    dbus_message_iter_next (&dict);

    /* Parse the variant contents */
    if (!msg_parse_variant (&dict, proplist, key))
        return FALSE;

    return TRUE;
}

static gboolean
msg_get_properties (DBusMessageIter *iter, NProplist **properties)
{
    NProplist       *p = NULL;
    DBusMessageIter  array;

    if (dbus_message_iter_get_arg_type (iter) != DBUS_TYPE_ARRAY)
        return FALSE;

    p = n_proplist_new ();

    dbus_message_iter_recurse (iter, &array);
    while (dbus_message_iter_get_arg_type (&array) != DBUS_TYPE_INVALID) {
        (void) msg_parse_dict (&array, p);
        dbus_message_iter_next (&array);
    }

    *properties = p;

    return TRUE;
}

static void
dbusif_ack (DBusConnection *connection, DBusMessage *msg, uint32_t event_id)
{
    DBusMessage *reply = NULL;

    reply = dbus_message_new_method_return (msg);
    if (reply) {
        dbus_message_append_args (reply, DBUS_TYPE_UINT32, &event_id, DBUS_TYPE_INVALID);
        dbus_connection_send (connection, reply, NULL);
        dbus_message_unref (reply);
    }
}

static void
dbusif_reply_error (DBusConnection *connection, DBusMessage *msg, const char *error_message)
{
    DBusMessage *reply = NULL;
    const char *error;

    error = error_message ? error_message : "Unknown error.";
    N_DEBUG (LOG_CAT "reply error: %s", error);
    reply = dbus_message_new_error (msg, DBUS_ERROR_FAILED, error);
    if (reply) {
        dbus_connection_send (connection, reply, NULL);
        dbus_message_unref (reply);
    }
}

static DBusHandlerResult
dbusif_play_handler (DBusConnection *connection, DBusMessage *msg,
                     NInputInterface *iface, uint32_t event_id)
{
    const char      *event      = NULL;
    NProplist       *properties = NULL;
    NRequest        *request    = NULL;
    DBusMessageIter  iter;

    dbus_message_iter_init (msg, &iter);
    if (dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_STRING)
        goto fail;

    dbus_message_iter_get_basic (&iter, &event);
    dbus_message_iter_next (&iter);

    if (!msg_get_properties (&iter, &properties))
        goto fail;

    N_INFO (LOG_CAT ">> play received for event '%s' with id '%u'", event, event_id);

    // Reply internal event_id immediately
    dbusif_ack (connection, msg, event_id);

    n_proplist_set_uint (properties, NGF_DBUS_PROPERTY, event_id);
    request = n_request_new_with_event_and_properties (event, properties);
    n_input_interface_play_request (iface, request);
    n_proplist_free (properties);

    return DBUS_HANDLER_RESULT_HANDLED;

fail:
    dbusif_reply_error (connection, msg, "Malformed method call.");
    return DBUS_HANDLER_RESULT_HANDLED;
}

static NRequest*
dbusif_lookup_request (NInputInterface *iface, uint32_t event_id)
{
    g_assert (iface != NULL);

    NCore     *core            = NULL;
    NRequest  *request         = NULL;
    GList     *active_requests = NULL;
    GList     *iter            = NULL;
    guint      match_id        = 0;
    NProplist *properties      = NULL;

    if (event_id == 0)
        return NULL;

    core = n_input_interface_get_core (iface);
    active_requests = n_core_get_requests (core);

    for (iter = g_list_first (active_requests); iter; iter = g_list_next (iter)) {
        request = (NRequest*) iter->data;
        properties = (NProplist*) n_request_get_properties (request);
        if (!properties)
            continue;

        match_id = n_proplist_get_uint (properties, NGF_DBUS_PROPERTY);
        if (match_id == event_id)
            return request;
    }

    return NULL;
}

static void
dbusif_stop_all (NInputInterface *iface)
{
    g_assert (iface != NULL);

    NCore     *core            = NULL;
    NRequest  *request         = NULL;
    GList     *active_requests = NULL;
    GList     *iter            = NULL;

    core = n_input_interface_get_core (iface);
    active_requests = n_core_get_requests (core);

    for (iter = g_list_first (active_requests); iter; iter = g_list_next (iter)) {
        request = (NRequest*) iter->data;
        n_input_interface_stop_request (iface, request, 0);
    }
}

static DBusHandlerResult
dbusif_stop_handler (DBusConnection *connection, DBusMessage *msg,
                     NInputInterface *iface)
{
    dbus_uint32_t    event_id  = 0;
    NRequest        *request   = NULL;
    const char      *name      = NULL;
    const char      *error     = NULL;

    if (!dbus_message_get_args (msg, NULL,
                                DBUS_TYPE_UINT32, &event_id,
                                DBUS_TYPE_INVALID))
    {
        error = "Malformed method call.";
        goto fail;
    }

    N_INFO (LOG_CAT ">> stop received for id '%u'", event_id);

    request = dbusif_lookup_request (iface, event_id);

    if (!request) {
        error = "No event with given id found.";
        goto fail;
    }

    name = n_request_get_name (request);
    if (name && g_str_equal (name, "ringtone")) {
        N_DEBUG (LOG_CAT "mute ringtone for delayed stop");
        n_input_interface_pause_request (iface, request);

        N_DEBUG (LOG_CAT "setup stop timeout for ringtone in %d ms",
            RINGTONE_STOP_TIMEOUT);

        n_input_interface_stop_request (iface, request, RINGTONE_STOP_TIMEOUT);
    }
    else {
        n_input_interface_stop_request (iface, request, 0);
    }

    dbusif_ack (connection, msg, event_id);

    return DBUS_HANDLER_RESULT_HANDLED;

fail:
    dbusif_reply_error (connection, msg, error);
    return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
dbusif_pause_handler (DBusConnection *connection, DBusMessage *msg,
                      NInputInterface *iface)
{
    dbus_uint32_t    event_id  = 0;
    dbus_bool_t      pause     = FALSE;
    NRequest        *request   = NULL;
    const char      *error     = NULL;

    (void) iface;

    if (!dbus_message_get_args (msg, NULL,
                                DBUS_TYPE_UINT32, &event_id,
                                DBUS_TYPE_BOOLEAN, &pause,
                                DBUS_TYPE_INVALID))
    {
        error = "Malformed method call.";
        goto fail;
    }

    N_INFO (LOG_CAT ">> %s received for id '%u'", pause ? "pause" : "resume",
        event_id);

    request = dbusif_lookup_request (iface, event_id);

    if (!request) {
        error = "No event with given id found.";
        goto fail;
    }

    if (pause)
        (void) n_input_interface_pause_request (iface, request);
    else
        (void) n_input_interface_play_request (iface, request);

    dbusif_ack (connection, msg, event_id);

    return DBUS_HANDLER_RESULT_HANDLED;

fail:
    dbusif_reply_error (connection, msg, error);
    return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
dbusif_message_function (DBusConnection *connection, DBusMessage *msg,
                         void *userdata)
{
    NInputInterface *iface  = (NInputInterface*) userdata;
    const char      *member = dbus_message_get_member (msg);
    DBusError error = DBUS_ERROR_INIT;
    gchar *component = NULL;
    gchar *s1 = NULL;
    gchar *s2 = NULL;

    if (dbus_message_is_signal (msg, "org.freedesktop.DBus", "NameOwnerChanged")) {
        if (!dbus_message_get_args
            (msg, &error,
            DBUS_TYPE_STRING, &component,
            DBUS_TYPE_STRING, &s1,
            DBUS_TYPE_STRING, &s2,
            DBUS_TYPE_INVALID)) {
            if (dbus_error_is_set (&error)) {
                N_WARNING (LOG_CAT "D-Bus error: %s", error.message);
                dbus_error_free (&error);
            }
        } else {
            if (component && g_str_equal (component, "org.freedesktop.ohm")) {
                N_INFO (LOG_CAT "Ohmd restarted, stopping all requests");
                dbusif_stop_all (iface);
            }
        }

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    if (member == NULL)
        return DBUS_HANDLER_RESULT_HANDLED;

    if (!dbus_message_has_interface (msg, NGF_DBUS_IFACE))
        return DBUS_HANDLER_RESULT_HANDLED;

    if (g_str_equal (member, NGF_DBUS_METHOD_PLAY))
        return dbusif_play_handler (connection, msg, iface, ++g_data->event_id);

    else if (g_str_equal (member, NGF_DBUS_METHOD_STOP))
        return dbusif_stop_handler (connection, msg, iface);

    else if (g_str_equal (member, NGF_DBUS_METHOD_PAUSE))
        return dbusif_pause_handler (connection, msg, iface);

    return DBUS_HANDLER_RESULT_HANDLED;
}

static int
dbusif_initialize (NInputInterface *iface)
{
    static struct DBusObjectPathVTable method = {
        .message_function = dbusif_message_function
    };

    DBusError error;
    int       ret;

    g_data = g_new0 (DBusInterfaceData, 1);
    g_data->iface = iface;

    dbus_error_init (&error);
    g_data->connection = dbus_bus_get (DBUS_BUS_SYSTEM, &error);
    if (!g_data->connection) {
        N_ERROR (LOG_CAT "failed to get system bus: %s", error.message);
        dbus_error_free (&error);
        return FALSE;
    }

    dbus_connection_setup_with_g_main (g_data->connection, NULL);

    ret = dbus_bus_request_name (g_data->connection, NGF_DBUS_NAME,
        DBUS_NAME_FLAG_REPLACE_EXISTING, &error);

    if (ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        if (dbus_error_is_set (&error)) {
            N_ERROR (LOG_CAT "failed to get unique name: %s", error.message);
            dbus_error_free (&error);
        }

        return FALSE;
    }

    if (!dbus_connection_register_object_path (g_data->connection,
        NGF_DBUS_PATH, &method, iface))
        return FALSE;

    /* Monitor for ohmd restarts */
    dbus_bus_add_match (g_data->connection,
    "type='signal',sender='org.freedesktop.DBus',member='NameOwnerChanged',arg0='org.freedesktop.ohm'",
       NULL);
    dbus_connection_add_filter (g_data->connection, dbusif_message_function, iface, NULL);

    return TRUE;
}

static void
dbusif_shutdown (NInputInterface *iface)
{
    (void) iface;

    if (g_data->connection) {
        dbus_connection_unref (g_data->connection);
        g_data->connection = NULL;
    }

    if (g_data) {
        g_free (g_data);
        g_data = NULL;
    }
}

static void
dbusif_send_error (NInputInterface *iface, NRequest *request,
                   const char *err_msg)
{
    N_DEBUG (LOG_CAT "error occurred for request '%s': %s",
        n_request_get_name (request), err_msg);

    dbusif_send_reply (iface, request, 0);
}

static void
dbusif_send_reply (NInputInterface *iface, NRequest *request, int code)
{
    DBusMessage     *msg   = NULL;
    const NProplist *props = NULL;
    guint            event_id = 0;
    guint            status = 0;

    (void) iface;

    props  = n_request_get_properties (request);
    event_id = n_proplist_get_uint ((NProplist*) props, NGF_DBUS_PROPERTY);
    status = code;

    if (event_id == 0)
        return;

    N_DEBUG (LOG_CAT "sending reply for request '%s' (event.id=%d) with code %d",
        n_request_get_name (request), event_id, code);

    if ((msg = dbus_message_new_signal (NGF_DBUS_PATH,
                                        NGF_DBUS_IFACE,
                                        NGF_DBUS_STATUS)) == NULL) {
        N_WARNING (LOG_CAT "failed to construct signal.");
        return;
    }

    dbus_message_append_args (msg,
        DBUS_TYPE_UINT32, &event_id,
        DBUS_TYPE_UINT32, &status,
        DBUS_TYPE_INVALID);

    dbus_connection_send (g_data->connection, msg, NULL);
    dbus_message_unref (msg);
}

int
n_plugin__load (NPlugin *plugin)
{
    static const NInputInterfaceDecl iface = {
        .name       = "dbus",
        .initialize = dbusif_initialize,
        .shutdown   = dbusif_shutdown,
        .send_error = dbusif_send_error,
        .send_reply = dbusif_send_reply
    };

    /* register the DBus interface as the NInputInterface */
    n_plugin_register_input (plugin, &iface);

    return 1;
}

void
n_plugin__unload (NPlugin *plugin)
{
    (void) plugin;
}

/*
 * ngfd - Non-graphic feedback daemon
 *
 * Copyright (C) 2013 Jolla Ltd <robin.burchell@jollamobile.com>
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

#include <ngf/plugin.h>
#include <canberra.h>

#include <string.h>

#define STREAM_PREFIX_KEY "sound.stream."
#define CANBERRA_KEY "plugin.canberra.data"
#define LOG_CAT "canberra: "
#define SOUND_FILENAME_KEY "canberra.filename"

typedef struct _CanberraData
{
    NRequest       *request;
    NSinkInterface *iface;
    const gchar *filename;
} CanberraData;

N_PLUGIN_NAME        ("canberra")
N_PLUGIN_VERSION     ("0.1")
N_PLUGIN_DESCRIPTION ("libcanberra plugin")

ca_context *c_context;

static int canberra_connect ()
{
    int error;

    if (c_context)
        return TRUE;

    ca_context_create (&c_context);
    error = ca_context_open (c_context);
    if (error) {
        N_WARNING (LOG_CAT "can't connect to canberra! %s", ca_strerror (error));
        ca_context_destroy (c_context);
        c_context = 0;
        return FALSE;
    }

    return TRUE;
}

static int
canberra_sink_initialize (NSinkInterface *iface)
{
    (void) iface;
    N_DEBUG (LOG_CAT "sink initialize");
    canberra_connect ();
    return TRUE;
}

static void
canberra_sink_shutdown (NSinkInterface *iface)
{
    (void) iface;
    N_DEBUG (LOG_CAT "sink shutdown");
}

static int
canberra_sink_can_handle (NSinkInterface *iface, NRequest *request)
{
    (void) iface;

    NProplist *props = NULL;

    props = (NProplist*) n_request_get_properties (request);
    if (n_proplist_has_key (props, SOUND_FILENAME_KEY)) {
        N_DEBUG (LOG_CAT "request has a sound.filename, we can handle this.");
        return TRUE;
    }

    return FALSE;
}

static int
canberra_sink_prepare (NSinkInterface *iface, NRequest *request)
{
    N_DEBUG (LOG_CAT "sink prepare");

    CanberraData *data = g_slice_new0 (CanberraData);
    NProplist *props = props = (NProplist*) n_request_get_properties (request);

    data->request    = request;
    data->iface      = iface;
    data->filename = n_proplist_get_string (props, SOUND_FILENAME_KEY);

    n_request_store_data (request, CANBERRA_KEY, data);
    n_sink_interface_synchronize (iface, request);

    return TRUE;
}

static void
proplist_to_structure_cb (const char *key, const NValue *value, gpointer userdata)
{
    ca_proplist *target     = (ca_proplist*) userdata;
    const char   *prop_key   = NULL;
    const char   *prop_value = NULL;

    if (!g_str_has_prefix (key, STREAM_PREFIX_KEY))
        return;

    prop_key = key + strlen (STREAM_PREFIX_KEY);
    if (*prop_key == '\0')
        return;

    prop_value = n_value_get_string ((NValue*) value);
    ca_proplist_sets (target, prop_key, prop_value);
}

static int
canberra_sink_play (NSinkInterface *iface, NRequest *request)
{
    N_DEBUG (LOG_CAT "sink play");

    (void) iface;

    if (canberra_connect () == FALSE)
        return FALSE;


    static GHashTable *cached_samples = NULL;
    if (!cached_samples) {
        cached_samples = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    }

    CanberraData *data = (CanberraData*) n_request_get_data (request, CANBERRA_KEY);
    g_assert (data != NULL);

    NProplist *props = props = (NProplist*) n_request_get_properties (request);
    ca_proplist *ca_props = 0;
    int error;
    ca_proplist_create (&ca_props);

    /* TODO: don't hardcode */
    ca_proplist_sets (ca_props, CA_PROP_CANBERRA_XDG_THEME_NAME, "jolla-ambient");
    ca_proplist_sets (ca_props, CA_PROP_EVENT_ID, data->filename);

    /* convert all properties within the request that begin with
       "sound.stream." prefix. */
    n_proplist_foreach (props, proplist_to_structure_cb, ca_props);

    if (g_hash_table_lookup_extended (cached_samples, data->filename, NULL, NULL) == FALSE) {
        N_DEBUG (LOG_CAT "caching sample %s", data->filename);
        error = ca_context_cache_full (c_context, ca_props);
        if (error) {
            N_WARNING (LOG_CAT "canberra couldn't cache sample %s", data->filename);
            return FALSE;
        }

        g_hash_table_add (cached_samples, strdup(data->filename));
    } else {
        N_DEBUG (LOG_CAT "sample %s is cached", data->filename);
    }

    error = ca_context_play_full (c_context, 0, ca_props, NULL, NULL);
    ca_proplist_destroy (ca_props);

    if (error) {
        N_WARNING (LOG_CAT "sink play had a warning: %s", ca_strerror (error));

        if (error == CA_ERROR_NOTAVAILABLE ||
            error == CA_ERROR_DISCONNECTED ||
            error == CA_ERROR_STATE ||
            error == CA_ERROR_DESTROYED) {
            ca_context_destroy (c_context);
            c_context = 0;
        }

        return FALSE;
    }

     n_sink_interface_complete (data->iface, request);
    return TRUE;
}

static int
canberra_sink_pause (NSinkInterface *iface, NRequest *request)
{
    N_DEBUG (LOG_CAT "sink pause");

    (void) iface;
    (void) request;

    return TRUE;
}

static void
canberra_sink_stop (NSinkInterface *iface, NRequest *request)
{
    N_DEBUG (LOG_CAT "sink stop");

    (void) iface;

    CanberraData *data = (CanberraData*) n_request_get_data (request, CANBERRA_KEY);
    g_assert (data != NULL);

    g_slice_free (CanberraData, data);
}

N_PLUGIN_LOAD (plugin)
{
    N_DEBUG (LOG_CAT "plugin load");

    static const NSinkInterfaceDecl decl = {
        .name       = "canberra",
        .initialize = canberra_sink_initialize,
        .shutdown   = canberra_sink_shutdown,
        .can_handle = canberra_sink_can_handle,
        .prepare    = canberra_sink_prepare,
        .play       = canberra_sink_play,
        .pause      = canberra_sink_pause,
        .stop       = canberra_sink_stop
    };

    n_plugin_register_sink (plugin, &decl);

    return TRUE;
}

N_PLUGIN_UNLOAD (plugin)
{
    (void) plugin;

    N_DEBUG (LOG_CAT "plugin unload");
}

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

/*
 * USAGE:
 *
 * Resource plugin looks for media.* properties in request proplist.
 * 1) if no media.* is found in proplist, default for ENABLED for all
 * sinks.
 * 2) if media.foo is found in proplist, default for DISABLED for all
 * sinks, and enable only those specifically enabled in proplist.
 *
 * *) media.audio, media.vibra, media.leds, media.backlight
 *
 * After enabled/disabled classification is done, drop all disabled
 * sinks from request.
 *
 * resource.ini plugin configuration can be used to define which sinks
 * correspond which resource_key, ie. to define gst sink as media.audio
 * resource:
 *
 * [resource]
 * media.audio = gst
 *
 */

#include <string.h>
#include <ngf/plugin.h>
#include <ngf/sinkinterface.h>

#define LOG_CAT "resource: "

N_PLUGIN_NAME        ("resource")
N_PLUGIN_VERSION     ("0.2")
N_PLUGIN_DESCRIPTION ("Resource rules")

enum resource_key_index {
    RES_AUDIO   = 0,
    RES_VIBRA   = 1,
    RES_LEDS    = 2,
    RES_BLIGHT  = 3,
    RES_COUNT
};

static const char *resource_keys[RES_COUNT] = {
    [RES_AUDIO] = "media.audio",
    [RES_VIBRA] = "media.vibra",
    [RES_LEDS]  = "media.leds",
    [RES_BLIGHT] = "media.backlight"
};

#define ARRAY_SIZE(x) (sizeof ((x)) / sizeof ((x)[0]))

static gboolean        resource_map_enabled = FALSE;
static NSinkInterface *sink_map[RES_COUNT];

static NSinkInterface*
lookup_sink_from_key (NCore *core, const char *key)
{
    NSinkInterface **sink = NULL;

    if (!key)
        return NULL;

    for (sink = n_core_get_sinks (core); *sink; ++sink) {
        if (g_str_equal (n_sink_interface_get_name (*sink), key))
            return *sink;
    }

    return NULL;
}

static void
init_done_cb (NHook *hook, void *data, void *userdata)
{
    (void) hook;
    (void) data;

    const char *key = NULL;
    unsigned int i = 0;
    gboolean has_one = FALSE;

    NPlugin   *plugin = (NPlugin*) userdata;
    NCore     *core   = n_plugin_get_core   (plugin);
    NProplist *params = (NProplist*) n_plugin_get_params (plugin);

    if (!params || n_proplist_size (params) == 0) {
        N_WARNING (LOG_CAT "filtering sinks by resources disabled, no mapping "
                           "defined from flag to sink.");
        return;
    }

    for (i = 0; i < ARRAY_SIZE (resource_keys); ++i) {
        key = n_proplist_get_string (params, resource_keys[i]);
        sink_map[i] = lookup_sink_from_key (core, key);

        if (sink_map[i])
            has_one = TRUE;
    }

    if (has_one)
        resource_map_enabled = TRUE;
}

static void
filter_sinks_cb (NHook *hook, void *data, void *userdata)
{
    NProplist                *props  = NULL;
    NCoreHookFilterSinksData *filter = (NCoreHookFilterSinksData*) data;

    (void) hook;
    (void) userdata;

    unsigned int index;
    int force_enabled = 0;
    int enabled[RES_COUNT];

    if (!resource_map_enabled) {
        N_DEBUG (LOG_CAT "filtering sinks by resource is disabled.");
        return;
    }

    N_DEBUG (LOG_CAT "filter sinks for request '%s'",
        n_request_get_name (filter->request));

    memset (enabled, 0, sizeof(enabled));

    props = (NProplist*) n_request_get_properties (filter->request);

    for (index = 0; index < ARRAY_SIZE(enabled); index++) {
        if (n_proplist_has_key (props, resource_keys[index])) {
            force_enabled = 1;
            enabled[index] = n_proplist_get_bool (props, resource_keys[index]);
        }
    }

    for (index = 0; index < ARRAY_SIZE(enabled); index++) {

        if (!sink_map[index])
            continue;

        N_DEBUG (LOG_CAT "resource %s%s for '%s' with sink '%s'",
            force_enabled ? "forced " : "",
            force_enabled && !enabled[index] ? "disabled" : "enabled",
            resource_keys[index],
            n_sink_interface_get_name (sink_map[index]));

        if (force_enabled && !enabled[index])
            filter->sinks = g_list_remove (filter->sinks, sink_map[index]);
    }
}

N_PLUGIN_LOAD (plugin)
{
    NCore *core = NULL;

    /* connect to the init done and filter sinks hook. */

    core = n_plugin_get_core (plugin);

    (void) n_core_connect (core, N_CORE_HOOK_INIT_DONE, 0,
        init_done_cb, plugin);

    (void) n_core_connect (core, N_CORE_HOOK_FILTER_SINKS, 0,
        filter_sinks_cb, plugin);

    return TRUE;
}

N_PLUGIN_UNLOAD (plugin)
{
    (void) plugin;
}

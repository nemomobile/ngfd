/*
 * ngfd - Non-graphic feedback daemon
 *
 * Copyright (C) 2010 Nokia Corporation.
 * Contact: Xun Chen <xun.chen@nokia.com>
 * Copyright (C) 2014 Jolla Ltd.
 * Contact: Simonas Leleiva <simonas.leleiva@jollamobile.com>
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
#include <hardware_legacy/vibrator.h>

#define AV_KEY "plugin.droid-vibrator.data"
#define LOG_CAT  "droid-vibrator: "

typedef struct _DroidVibratorData
{
    NRequest       *request;
    NSinkInterface *iface;
    guint           timeout_id;
} DroidVibratorData;

N_PLUGIN_NAME        ("droid-vibrator")
N_PLUGIN_VERSION     ("0.1")
N_PLUGIN_DESCRIPTION ("Haptic feedback using Droid Vibrator HAL via libhybris")

static int
droid_vibrator_sink_initialize (NSinkInterface *iface)
{
    (void) iface;
    N_DEBUG (LOG_CAT "sink initialize");
    return TRUE;
}

static void
droid_vibrator_sink_shutdown (NSinkInterface *iface)
{
    (void) iface;
    N_DEBUG (LOG_CAT "sink shutdown");
}

static int
droid_vibrator_sink_can_handle (NSinkInterface *iface, NRequest *request)
{
    (void) iface;
    (void) request;

    N_DEBUG (LOG_CAT "sink can_handle");
    return TRUE;
}

static int
droid_vibrator_sink_prepare (NSinkInterface *iface, NRequest *request)
{
    N_DEBUG (LOG_CAT "sink prepare");

    DroidVibratorData *data = g_slice_new0 (DroidVibratorData);

    data->request    = request;
    data->iface      = iface;
    data->timeout_id = 0;

    n_request_store_data (request, AV_KEY, data);
    n_sink_interface_synchronize (iface, request);

    return TRUE;
}

static gboolean
timeout_cb (gpointer userdata)
{
    N_DEBUG (LOG_CAT "sink play timeout");

    DroidVibratorData *data = (DroidVibratorData*) userdata;
    g_assert (data != NULL);

    data->timeout_id = 0;
    n_sink_interface_complete (data->iface, data->request);

    return FALSE;
}

static int
droid_vibrator_sink_play (NSinkInterface *iface, NRequest *request)
{
    N_DEBUG (LOG_CAT "sink play");

    (void) iface;
    // TODO: refer to ffmemless.ini and build own config with durations for
    // each event
    int duration_ms = 33;

    DroidVibratorData *data = (DroidVibratorData*) n_request_get_data (request, AV_KEY);
    g_assert (data != NULL);

    // (thanks kjokinie) TODO: During call you want to block any vibration
    // effects (doesn't feel nice against ear)

    // (thanks kjokinie) TODO: respect profile settings to not play when
    // vibration is off. (there is also a special case for touch screen effects,
    // which have special bit set in ffmemless effects configuration files, and
    // follow "profile.current.touchscreen.vibration.level" profile setting)

    vibrator_on(duration_ms);

    // underlying Droid API cannot tell when playback finishes, so we always
    // assume it worked and finished (no big knock-on effects if we don't time
    // it well: tests show subsequent rapid haptics feedback worked fine)
    data->timeout_id = g_timeout_add (duration_ms, timeout_cb, data);

    return TRUE;
}

static int
droid_vibrator_sink_pause (NSinkInterface *iface, NRequest *request)
{
    N_DEBUG (LOG_CAT "sink pause");

    (void) iface;
    (void) request;

    return TRUE;
}

static void
droid_vibrator_sink_stop (NSinkInterface *iface, NRequest *request)
{
    N_DEBUG (LOG_CAT "sink stop");

    (void) iface;

    DroidVibratorData *data = (DroidVibratorData*) n_request_get_data (request, AV_KEY);
    g_assert (data != NULL);

    if (data->timeout_id > 0) {
        g_source_remove (data->timeout_id);
        data->timeout_id = 0;
    }

    g_slice_free (DroidVibratorData, data);
}

N_PLUGIN_LOAD (plugin)
{
    N_DEBUG (LOG_CAT "plugin load");

    static const NSinkInterfaceDecl decl = {
        .name       = "droid-vibrator",
        .initialize = droid_vibrator_sink_initialize,
        .shutdown   = droid_vibrator_sink_shutdown,
        .can_handle = droid_vibrator_sink_can_handle,
        .prepare    = droid_vibrator_sink_prepare,
        .play       = droid_vibrator_sink_play,
        .pause      = droid_vibrator_sink_pause,
        .stop       = droid_vibrator_sink_stop
    };

    n_plugin_register_sink (plugin, &decl);

    return TRUE;
}

N_PLUGIN_UNLOAD (plugin)
{
    (void) plugin;

    N_DEBUG (LOG_CAT "plugin unload");
}

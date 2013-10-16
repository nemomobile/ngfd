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

#include <ngf/plugin.h>

#include <stdlib.h>
#include <glib.h>
#include <gst/gst.h>
#include <gst/controller/gstcontroller.h>
#include <gst/controller/gstinterpolationcontrolsource.h>
#include <gio/gio.h>

#define GST_KEY               "plugin.gst.data"
#define LOG_CAT               "gst: "
#define MAX_TIMEOUT_KEY       "core.max_timeout"
#define STREAM_PREFIX_KEY     "sound.stream."
#define SOUND_FILENAME_KEY    "sound.filename"
#define SOUND_REPEAT_KEY      "sound.repeat"
#define SOUND_VOLUME_KEY      "sound.volume"
#define FADE_ONLY_CUSTOM_KEY  "sound.fade-only-custom"
#define FADE_OUT_KEY          "sound.fade-out"
#define FADE_IN_KEY           "sound.fade-in"
#define SYSTEM_SOUND_PATH     "/usr/share/sounds/"

typedef struct _FadeEffect
{
    gboolean enabled;   /* effect enabled */
    gdouble elapsed;    /* effect update time */
    gdouble position;   /* begin position (in s) */
    gdouble length;     /* length of the fade (in s) */
    gdouble start;      /* starting volume */
    gdouble end;        /* ending volume */
} FadeEffect;

typedef struct _StreamData
{
    NRequest *request;
    NSinkInterface *iface;
    GstElement *pipeline;
    GstElement *volume;
    gboolean volume_limit;
    guint volume_cap;
    gboolean volume_fixed;
    guint volume_min;
    guint volume_max;
    guint volume_set;
    GstStructure *properties;
    const gchar *filename;
    gboolean repeat_enabled;
    guint restart_source_id;
    gboolean first_play;
    GstController *controller;
    gdouble last_volume;
    gdouble time_spent;
    gboolean paused;
    guint bus_watch_id;

    FadeEffect *fade_out;
    FadeEffect *fade_in;
} StreamData;

N_PLUGIN_NAME        ("gst")
N_PLUGIN_VERSION     ("0.1")
N_PLUGIN_DESCRIPTION ("GStreamer plugin")

static gboolean is_custom_sound_filename (const char *filename);
static gchar* strip_prefix (const gchar *str, const gchar *prefix);
static gboolean parse_volume_limit (const char *str, guint *min, guint *max);
static gboolean parse_fixed_volume (const char *str, guint *volume);
static gboolean get_current_volume (StreamData *stream, gdouble *out_volume);
static gboolean get_current_position (StreamData *stream, gdouble *out_position);
static void set_stream_properties (GstElement *sink, const GstStructure *properties);
static int set_structure_string (GstStructure *s, const char *key, const char *value);
static void proplist_to_structure_cb (const char *key, const NValue *value, gpointer userdata);
static GstStructure* create_stream_properties (NProplist *props);
static gboolean restart_stream_cb (gpointer userdata);
static gboolean bus_cb (GstBus *bus, GstMessage *msg, gpointer userdata);
static void new_decoded_pad_cb (GstElement *element, GstPad *pad, gboolean is_last, gpointer userdata);
static int make_pipeline (StreamData *stream);
static void free_pipeline (StreamData *stream);
static int convert_number (const char *str, gint *result);
static FadeEffect* parse_volume_fade (const char *str);
static void set_fade_effect (GstInterpolationControlSource *source, FadeEffect *effect);
static void update_fade_effect (FadeEffect *effect, gdouble elapsed, gdouble volume);
static void free_fade_effect (FadeEffect *effect);

static gboolean system_sounds_enabled = TRUE;
static guint system_sounds_level = 0;

static gboolean
is_custom_sound_filename (const char *filename)
{
    if (filename && g_str_has_prefix (filename, SYSTEM_SOUND_PATH))
        return FALSE;

    return TRUE;
}

static gchar*
strip_prefix (const gchar *str, const gchar *prefix)
{
    if (!g_str_has_prefix (str, prefix))
        return NULL;

    size_t prefix_length = strlen (prefix);
    return g_strdup (str + prefix_length);
}

static gboolean
parse_volume_limit (const char *str, guint *min, guint *max)
{
    gchar *stripped = NULL;

    if (!str)
        return FALSE;
    
    *min = 0;
    *max = 0;
    
    if (g_str_has_prefix (str, "max:")) {
        stripped = strip_prefix (str, "max:");
        *max = atoi (stripped);
        g_free (stripped);
        return TRUE;
    }
    
    if (g_str_has_prefix (str, "min:")) {
        stripped = strip_prefix (str, "min:");
        *min = atoi (stripped);
        g_free (stripped);
        return TRUE;
    }
    
    return FALSE;
}

static gboolean
parse_fixed_volume (const char *str, guint *volume)
{
    gchar *stripped = NULL;

    if (!str || !g_str_has_prefix (str, "fixed:"))
        return FALSE;

    stripped = strip_prefix (str, "fixed:");
    *volume = atoi (stripped);
    g_free (stripped);
    return TRUE;
}

static gboolean
get_current_volume (StreamData *stream, gdouble *out_volume)
{
    gdouble v;

    g_object_get (G_OBJECT (stream->volume),
        "volume", &v, NULL);

    *out_volume = v;

    return TRUE;
}

static gboolean
get_current_position (StreamData *stream, gdouble *out_position)
{
    GstFormat fmt = GST_FORMAT_TIME;
    gint64 timestamp;

    if (!gst_element_query_position (stream->pipeline, &fmt, &timestamp)) {
        N_WARNING (LOG_CAT "unable to query data position");
        return FALSE;
    }

    if (!(GST_CLOCK_TIME_IS_VALID (timestamp) && fmt == GST_FORMAT_TIME)) {
        N_WARNING (LOG_CAT "queried position or format is not valid");
        return FALSE;
    }

    *out_position = (gdouble) timestamp / GST_SECOND;
    return TRUE;
}

static void
set_stream_properties (GstElement *sink, const GstStructure *properties)
{
    if (!sink | !properties)
        return;

    if (g_object_class_find_property (G_OBJECT_GET_CLASS (sink), "stream-properties") != NULL) {
        g_object_set (G_OBJECT (sink), "stream-properties", properties, NULL);
    }
}

static int
set_structure_string (GstStructure *s, const char *key, const char *value)
{
    g_assert (s != NULL);
    g_assert (key != NULL);

    GValue v = {0,{{0}}};

    if (!value)
        return FALSE;

    g_value_init (&v, G_TYPE_STRING);
    g_value_set_string (&v, value);
    gst_structure_set_value (s, key, &v);
    g_value_unset (&v);

    return TRUE;
}

static void
proplist_to_structure_cb (const char *key, const NValue *value, gpointer userdata)
{
    GstStructure *target     = (GstStructure*) userdata;
    const char   *prop_key   = NULL;
    const char   *prop_value = NULL;

    if (!g_str_has_prefix (key, STREAM_PREFIX_KEY))
        return;

    prop_key = key + strlen (STREAM_PREFIX_KEY);
    if (*prop_key == '\0')
        return;

    prop_value = n_value_get_string ((NValue*) value);
    (void) set_structure_string (target, prop_key, prop_value);
}

static GstStructure*
create_stream_properties (NProplist *props)
{
    g_assert (props != NULL);

    GstStructure *s      = gst_structure_empty_new ("props");
    const char   *source = NULL;
    const char   *role   = NULL;

    /* set the stream filename based on the sound file we're
       about to play. */

    source = n_proplist_get_string (props, SOUND_FILENAME_KEY);
    g_assert (source != NULL);

    set_structure_string (s, "media.filename", source);

    /* set media.role from configuration file if defined. Default to "media" */
    role = n_proplist_get_string (props, STREAM_PREFIX_KEY "media.role");
    if (role)
        set_structure_string (s, "media.role", role);
    else
        set_structure_string (s, "media.role", "media");

    /* if system sound level is off and the flag is set, then we need to
       use different stream restore role. */

    role = n_proplist_get_string (props ,"system-sounds-role");
    if (!system_sounds_enabled && role) {
        N_DEBUG (LOG_CAT "system sounds are off and replace role is set, using '%s'", role);
        n_proplist_set_string (props, STREAM_PREFIX_KEY "module-stream-restore.id", role);
    }

    /* convert all properties within the request that begin with
       "sound.stream." prefix. */

    n_proplist_foreach (props, proplist_to_structure_cb, s);

    return s;
}

static void
free_stream_properties (GstStructure *s)
{
    if (s)
        gst_structure_free (s);
}

static int
create_volume (StreamData *stream)
{
    GstInterpolationControlSource *source = NULL;

    if (stream->fade_in || stream->fade_out) {
        stream->controller = gst_controller_new (G_OBJECT (stream->volume),
            "volume", NULL);

        source = gst_interpolation_control_source_new ();
        gst_controller_set_control_source (stream->controller,
            "volume", GST_CONTROL_SOURCE (source));

        gst_interpolation_control_source_set_interpolation_mode (
            source, GST_INTERPOLATE_LINEAR);

        set_fade_effect (source, stream->fade_in);
        set_fade_effect (source, stream->fade_out);

        gst_object_unref (source);

        return TRUE;
    }

    if (stream->volume_limit) {
        if (system_sounds_level < stream->volume_min) {
            g_object_set (G_OBJECT (stream->volume), "volume", stream->volume_min / 100.0, NULL);
        }
        
        if (stream->volume_max && system_sounds_level > stream->volume_max) {
            g_object_set (G_OBJECT (stream->volume), "volume", stream->volume_max / 100.0, NULL);
        }

        return TRUE;
    }
    
    if (stream->volume_fixed)
        g_object_set (G_OBJECT (stream->volume), "volume", stream->volume_set / 100.0, NULL);

    return FALSE;
}

static void
free_volume (StreamData *stream)
{
    if (stream->controller) {
        g_object_unref (G_OBJECT (stream->controller));
        stream->controller = NULL;
    }
}

static gboolean
restart_stream_cb (gpointer userdata)
{
    StreamData *stream = (StreamData*) userdata;
    gdouble position = 0.0;

    stream->restart_source_id = 0;
    stream->first_play = FALSE;

    /* query the current position and volume */

    (void) get_current_position (stream, &position);
    stream->time_spent += position;

    (void) get_current_volume (stream, &stream->last_volume);

    /* stop the previous stream and free it */

    free_pipeline (stream);
 
    /* update the stream effects */

    N_DEBUG (LOG_CAT "fade effect (last volume=%.2f)", stream->last_volume);

    update_fade_effect (stream->fade_in, stream->time_spent, stream->last_volume);
    update_fade_effect (stream->fade_out, stream->time_spent, stream->last_volume);

    /* recreate the stream */

    N_DEBUG (LOG_CAT "re-creating pipeline.");
    if (!make_pipeline (stream)) {
        n_sink_interface_fail (stream->iface, stream->request);
        return FALSE;
    }

    /* put the stream to the playing state immediately */

    N_DEBUG (LOG_CAT "setting pipeline to playing");
    gst_element_set_state (stream->pipeline, GST_STATE_PLAYING);

    return FALSE;
}

static gboolean
bus_cb (GstBus *bus, GstMessage *msg, gpointer userdata)
{
    StreamData *stream = (StreamData*) userdata;

    (void) bus;

    switch (GST_MESSAGE_TYPE (msg)) {
        case GST_MESSAGE_ERROR: {
            GError *error = NULL;
            gst_message_parse_error (msg, &error, NULL);
            N_WARNING (LOG_CAT "error: %s", error->message);
            g_error_free (error);
            n_sink_interface_fail (stream->iface, stream->request);
            return FALSE;
        }

        case GST_MESSAGE_STATE_CHANGED: {
            if (GST_ELEMENT (GST_MESSAGE_SRC (msg)) != stream->pipeline)
                break;

            GstState old_state, new_state, pending_state;
            gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);

            if (old_state == GST_STATE_READY && new_state == GST_STATE_PAUSED) {
                N_DEBUG (LOG_CAT "synchronize");
                n_sink_interface_synchronize (stream->iface, stream->request);
            }

            break;
        }

        case GST_MESSAGE_EOS: {
            if (GST_ELEMENT (GST_MESSAGE_SRC (msg)) != stream->pipeline)
                break;

            if (stream->repeat_enabled) {
                N_DEBUG (LOG_CAT "rewinding pipeline.");
                n_sink_interface_resynchronize (stream->iface, stream->request);
                stream->restart_source_id = g_idle_add (restart_stream_cb, stream);
                return FALSE;
            }

            N_DEBUG (LOG_CAT "eos");
            n_sink_interface_complete (stream->iface, stream->request);
            return FALSE;
        }

        default:
            break;
    }

    return TRUE;
}

static void
new_decoded_pad_cb (GstElement *element, GstPad *pad, gboolean is_last,
                    gpointer userdata)
{
    GstElement   *sink_element = (GstElement*) userdata;
    GstStructure *structure    = NULL;
    GstCaps      *caps         = NULL;
    GstPad       *sink_pad     = NULL;

    (void) element;
    (void) is_last;

    caps = gst_pad_get_caps (pad);
    if (gst_caps_is_empty (caps) || gst_caps_is_any (caps)) {
        gst_caps_unref (caps);
        return;
    }

    structure = gst_caps_get_structure (caps, 0);
    if (g_str_has_prefix (gst_structure_get_name (structure), "audio")) {
        sink_pad = gst_element_get_pad (sink_element, "sink");
        if (!gst_pad_is_linked (sink_pad))
            gst_pad_link (pad, sink_pad);
        gst_object_unref (sink_pad);
    }

    gst_caps_unref (caps);
}

static int
make_pipeline (StreamData *stream)
{
    GstElement *pipeline = NULL, *source = NULL, *decoder = NULL,
        *audioconv = NULL, *volume = NULL, *sink = NULL;
    GstBus *bus = NULL;

    pipeline = gst_pipeline_new (NULL);
    source = gst_element_factory_make ("filesrc", NULL);
    decoder = gst_element_factory_make ("decodebin2", NULL);
    audioconv = gst_element_factory_make ("audioconvert", NULL);
    volume = gst_element_factory_make ("volume", NULL);
    sink = gst_element_factory_make ("pulsesink", NULL);

    if (!pipeline || !source || !decoder || !audioconv || !volume || !sink) {
        N_WARNING (LOG_CAT "failed to create required elements.");
        goto failed;
    }

    gst_bin_add_many (GST_BIN (pipeline), source, decoder, audioconv, volume, sink, NULL);
    
    if (!gst_element_link (source, decoder)) {
        N_WARNING (LOG_CAT "failed to link source to decoder");
        goto failed_pipeline;
    }

    if (!gst_element_link_many (audioconv, volume, sink, NULL)) {
        N_WARNING (LOG_CAT "failed to link converter, volume or sink");
        goto failed_pipeline;
    }

    g_signal_connect (G_OBJECT (decoder), "new-decoded-pad",
        G_CALLBACK (new_decoded_pad_cb), audioconv);

    g_object_set (G_OBJECT (source), "location", stream->filename, NULL);

    bus = gst_element_get_bus (pipeline);
    stream->bus_watch_id = gst_bus_add_watch (bus, bus_cb, stream);
    gst_object_unref (bus);

    set_stream_properties (sink, stream->properties);

    stream->pipeline = pipeline;
    stream->volume = volume;

    (void) create_volume (stream);

    return TRUE;

failed:
    free_volume (stream);

    if (sink)
        gst_object_unref (sink);
    if (volume)
        gst_object_unref (volume);
    if (audioconv)
        gst_object_unref (audioconv);
    if (decoder)
        gst_object_unref (decoder);
    if (source)
        gst_object_unref (source);

failed_pipeline:
    if (pipeline)
        gst_object_unref (pipeline);

    return FALSE;
}

static void
free_pipeline (StreamData *stream)
{
    if (stream->pipeline) {
        N_DEBUG (LOG_CAT "freeing pipeline");
        gst_element_set_state (stream->pipeline, GST_STATE_NULL);
        gst_object_unref (stream->pipeline);
        stream->pipeline = NULL;
    }

    if (stream->bus_watch_id > 0) {
        g_source_remove (stream->bus_watch_id);
        stream->bus_watch_id = 0;
    }

    free_volume (stream);
}

static void
system_sound_level_changed (NContext *context,
                            const char *key,
                            const NValue *old_value,
                            const NValue *new_value,
                            void *userdata)
{
    (void) context;
    (void) key;
    (void) old_value;
    (void) userdata;

    int v = 0;

    if (new_value) {
        v = n_value_get_int (new_value);
        system_sounds_level = v;
        if (v <= 0 && system_sounds_enabled) {
            N_DEBUG (LOG_CAT "system sounds are disabled.");
            system_sounds_enabled = FALSE;
        }
        else if (!system_sounds_enabled) {
            N_DEBUG (LOG_CAT "system sounds are enabled.");
            system_sounds_enabled = TRUE;
        }
    }
}

static void
init_done_cb (NHook *hook, void *data, void *userdata)
{
    (void) hook;
    (void) data;

    NContext *context = (NContext*) userdata;
    NValue *v = NULL;

    /* query the initial system sound level value */
    v = (NValue*) n_context_get_value (context, "profile.current.system.sound.level");
    if (v)
        system_sounds_enabled = n_value_get_int (v) > 0 ? TRUE : FALSE;

    if (!n_context_subscribe_value_change (context,
            "profile.current.system.sound.level",
            system_sound_level_changed, NULL))
    {
        N_WARNING (LOG_CAT "failed to subscribe to system sound "
                           "volume change");
    }
}

static int
gst_sink_initialize (NSinkInterface *iface)
{
    (void) iface;

    N_DEBUG (LOG_CAT "initializing GStreamer");

    gst_init_check (NULL, NULL, NULL);
    gst_controller_init (NULL, NULL);

    return TRUE;
}

static void
gst_sink_shutdown (NSinkInterface *iface)
{
    (void) iface;
}

static int
gst_sink_can_handle (NSinkInterface *iface, NRequest *request)
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
convert_number (const char *str, gint *result)
{
    long n;
    char *end;
    int errcode;

    errno = 0;
    n = strtol (str, &end, 10);
    errcode = errno;
    
    if ((n == 0 && str == end)
        || (n == 0 && errcode == EINVAL)
        || ((n == LONG_MAX || n == LONG_MIN) && errcode == ERANGE)
        || (n > G_MAXINT || n < G_MININT))
    {
        *result = (gint) 0;
        return 0;
    }

    *result = (gint) n;

    return 1;
}

static FadeEffect*
parse_volume_fade (const char *str)
{
#define VALID_NUMBER(in_f) \
    (valid = (valid == 0) ? 0 : (in_f))

    /* fade key has four values defined: position, length, start value, end value */

    FadeEffect *effect = NULL;
    gchar **split = NULL;
    gint position, length, start, end;
    int valid = 1;

    if (str == NULL)
        return NULL;

    split = g_strsplit (str, ",", 4);

    if (split[0] && split[1] && split[2] && split[3]) {

        VALID_NUMBER (convert_number (split[0], &position));
        VALID_NUMBER (convert_number (split[1], &length));
        VALID_NUMBER (convert_number (split[2], &start));
        VALID_NUMBER (convert_number (split[3], &end));

        if (valid) {
            effect = g_slice_new (FadeEffect);
            effect->enabled  = TRUE;
            effect->elapsed  = 0;
            effect->position = position;
            effect->length   = length;
            effect->start    = start / 100.0;
            effect->end      = end / 100.0;
        }
    }

    g_strfreev (split);

    if (effect) {
        N_DEBUG (LOG_CAT "fade effect parsed (enabled=%d elapsed=%.2f position=%.2f "
                         "length=%.2f start=%.2f stop=%.2f)", effect->enabled ? 1 : 0,
            effect->elapsed, effect->position, effect->length, effect->start, effect->end);
    }
    else {
        N_DEBUG (LOG_CAT "invalid fade effect, unable to parse: '%s'", str);
    }

    return effect;

#undef VALID_NUMBER
}

static void
set_fade_effect (GstInterpolationControlSource *source, FadeEffect *effect)
{
    GValue v = {0,{{0}}};
    gdouble start_time, new_length;

    if (source == NULL || effect == NULL || !effect->enabled)
        return;

    /* effect not started yet */

    if (effect->elapsed < effect->position) {
        start_time = effect->position - effect->elapsed;
        new_length = effect->length;
    }

    /* effect in progress */

    else if (effect->elapsed >= effect->position
             && effect->elapsed < (effect->position + effect->length)) {
        start_time = 0;
        new_length = effect->length - (effect->elapsed - effect->position);
    }

    /* done and finished */

    else {
        N_DEBUG (LOG_CAT "fade effect disabled (elapsed=%.2f position=%.2f "
                         "length=%.2f start=%.2f stop=%.2f)", effect->elapsed,
            effect->position, effect->length, effect->start, effect->end);
        effect->enabled = FALSE;
        return;
    }

    g_value_init (&v, G_TYPE_DOUBLE);

    g_value_set_double (&v, effect->start);
    gst_interpolation_control_source_set (source,
        start_time * GST_SECOND, &v);

    g_value_set_double (&v, effect->end);
    gst_interpolation_control_source_set (source,
        (start_time + new_length) * GST_SECOND, &v);

    g_value_unset (&v);

    N_DEBUG (LOG_CAT "fade effect (%.2f -> %.2f) to start from %.2f and end at %.2f seconds",
        effect->start, effect->end, start_time, start_time + new_length);
}

static void
update_fade_effect (FadeEffect *effect, gdouble elapsed, gdouble volume)
{
    if (effect == NULL || !effect->enabled)
        return;

    effect->elapsed = elapsed;
    effect->start = volume;

    N_DEBUG (LOG_CAT "fade effect updated (enabled=%d elapsed=%.2f position=%.2f "
                     "length=%.2f start=%.2f stop=%.2f)", effect->enabled ? 1 : 0,
        effect->elapsed, effect->position, effect->length, effect->start, effect->end);

    return;
}

static void
free_fade_effect (FadeEffect *effect)
{
    if (effect) {
        g_slice_free (FadeEffect, effect);
    }
}

static int
gst_sink_prepare (NSinkInterface *iface, NRequest *request)
{
    StreamData *stream = NULL;
    NProplist *props = NULL;
    gint timeout_ms;
    gboolean custom_sound, fade_only_custom;

    props = (NProplist*) n_request_get_properties (request);

    stream = g_slice_new0 (StreamData);
    stream->request = request;
    stream->iface = iface;
    stream->filename = n_proplist_get_string (props, SOUND_FILENAME_KEY);
    stream->repeat_enabled = n_proplist_get_bool (props, SOUND_REPEAT_KEY);
    stream->properties = create_stream_properties (props);
    stream->first_play = TRUE;

    stream->volume_limit = parse_volume_limit (n_proplist_get_string (props, SOUND_VOLUME_KEY),
        &stream->volume_min, &stream->volume_max);
    
    stream->volume_fixed = parse_fixed_volume (n_proplist_get_string (props, SOUND_VOLUME_KEY),
        &stream->volume_set);

    fade_only_custom = n_proplist_get_bool (props, FADE_ONLY_CUSTOM_KEY);
    custom_sound = is_custom_sound_filename (stream->filename);

    if (!fade_only_custom || (fade_only_custom && custom_sound)) {
        /* parse the volume fading keys and setup fades for the stream
           if available */

        stream->fade_out = parse_volume_fade (
            n_proplist_get_string (props, FADE_OUT_KEY));
        stream->fade_in = parse_volume_fade (
            n_proplist_get_string (props, FADE_IN_KEY));

        timeout_ms = n_proplist_get_int (props, MAX_TIMEOUT_KEY);
        timeout_ms = timeout_ms < 0 ? 0 : timeout_ms;

        n_request_set_timeout (request, (guint) timeout_ms);
    }

    /* store data ... */

    n_request_store_data (request, GST_KEY, stream);

    if (!make_pipeline (stream))
        return FALSE;

    N_DEBUG (LOG_CAT "setting pipeline to paused");
    gst_element_set_state (stream->pipeline, GST_STATE_PAUSED);

    return TRUE;
}

static int
gst_sink_play (NSinkInterface *iface, NRequest *request)
{
    (void) iface;

    StreamData *stream = NULL;

    stream = (StreamData*) n_request_get_data (request, GST_KEY);
    g_assert (stream != NULL);

    if (stream->pipeline) {
        N_DEBUG (LOG_CAT "setting pipeline to playing");
        gst_element_set_state (stream->pipeline, GST_STATE_PLAYING);
        stream->paused = FALSE;
    }

    return TRUE;
}

static int
gst_sink_pause (NSinkInterface *iface, NRequest *request)
{
    (void) iface;
    (void) request;

    StreamData *stream = NULL;

    stream = (StreamData*) n_request_get_data (request, GST_KEY);
    g_assert (stream != NULL);

    if (stream->pipeline && !stream->paused) {
        N_DEBUG (LOG_CAT "pausing pipeline.");
        gst_element_set_state (stream->pipeline, GST_STATE_PAUSED);
        stream->paused = TRUE;
    }

    return TRUE;
}

static void
gst_sink_stop (NSinkInterface *iface, NRequest *request)
{
    (void) iface;

    StreamData *stream = NULL;

    stream = (StreamData*) n_request_get_data (request, GST_KEY);
    g_assert (stream != NULL);

    if (stream->restart_source_id > 0) {
        g_source_remove (stream->restart_source_id);
        stream->restart_source_id = 0;
    }

    free_pipeline (stream);
    free_stream_properties (stream->properties);

    free_fade_effect (stream->fade_out);
    stream->fade_out = NULL;

    free_fade_effect (stream->fade_in);
    stream->fade_in = NULL;

    g_slice_free (StreamData, stream);
}

N_PLUGIN_LOAD (plugin)
{
    NCore    *core    = NULL;
    NContext *context = NULL;

    static const NSinkInterfaceDecl decl = {
        .name       = "gst",
        .initialize = gst_sink_initialize,
        .shutdown   = gst_sink_shutdown,
        .can_handle = gst_sink_can_handle,
        .prepare    = gst_sink_prepare,
        .play       = gst_sink_play,
        .pause      = gst_sink_pause,
        .stop       = gst_sink_stop
    };

    n_plugin_register_sink (plugin, &decl);

    core = n_plugin_get_core (plugin);
    context = n_core_get_context (core);

    if (!n_core_connect (core, N_CORE_HOOK_INIT_DONE, 0,
                         init_done_cb, context))
    {
        N_WARNING (LOG_CAT "failed to setup init done hook.");
    }

    return TRUE;
}

N_PLUGIN_UNLOAD (plugin)
{
    NCore    *core    = NULL;
    NContext *context = NULL;

    core = n_plugin_get_core (plugin);
    context = n_core_get_context (core);

    n_context_unsubscribe_value_change (context,
        "profile.current.system.sound.level",
        system_sound_level_changed);

    n_core_disconnect (core, N_CORE_HOOK_INIT_DONE,
        init_done_cb, context);
}

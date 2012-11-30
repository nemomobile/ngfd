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

#include "core-player.h"
#include <string.h>

#define LOG_CAT         "core: "
#define FALLBACK_SUFFIX ".fallback"
#define MAX_TIMEOUT_KEY "core.max_timeout"
#define POLICY_TIMEOUT_KEY "play.timeout"

static gboolean n_core_max_timeout_reached_cb         (gpointer userdata);
static void     n_core_setup_max_timeout              (NRequest *request);
static void     n_core_clear_max_timeout              (NRequest *request);
static void     n_core_fire_new_request_hook          (NRequest *request);
static void     n_core_fire_transform_properties_hook (NRequest *request);
static GList*   n_core_fire_filter_sinks_hook         (NRequest *request, GList *sinks);
static GList*   n_core_query_capable_sinks            (NRequest *request);
static void     n_core_merge_request_properties       (NRequest *request, NEvent *event);

static void     n_core_send_reply               (NRequest *request, NCorePlayerState status);
static void     n_core_send_error               (NRequest *request, const char *err_msg);
static int      n_core_sink_in_list             (GList *sinks, NSinkInterface *sink);
static int      n_core_sink_priority_cmp        (gconstpointer in_a, gconstpointer in_b);
static gboolean n_core_sink_synchronize_done_cb (gpointer userdata);
static gboolean n_core_request_done_cb          (gpointer userdata);
static void     n_core_stop_sinks               (GList *sinks, NRequest *request);
static int      n_core_prepare_sinks            (GList *sinks, NRequest *request);



static gboolean
n_core_max_timeout_reached_cb (gpointer userdata)
{
    NRequest *request = (NRequest*) userdata;

    N_DEBUG (LOG_CAT "maximum timeout reached, stopping request.");
    request->max_timeout_id = 0;
    n_core_stop_request (request->core, request, 0);

    return FALSE;
}

static void
n_core_setup_max_timeout (NRequest *request)
{
    g_assert (request != NULL);

    if (request->max_timeout_id > 0)
        return;

    if (request->timeout_ms > 0) {
        N_DEBUG (LOG_CAT "maximum timeout set to %d", request->timeout_ms);
        request->max_timeout_id = g_timeout_add (request->timeout_ms,
            n_core_max_timeout_reached_cb, request);
    }
}

static void
n_core_clear_max_timeout (NRequest *request)
{
    g_assert (request != NULL);

    if (request->max_timeout_id > 0) {
        N_DEBUG (LOG_CAT "maximum timeout callback removed.");
        g_source_remove (request->max_timeout_id);
        request->max_timeout_id = 0;
    }
}

static void
n_core_fire_new_request_hook (NRequest *request)
{
    g_assert (request != NULL);
    g_assert (request->core != NULL);

    NCoreHookNewRequestData new_request;

    new_request.request = request;
    n_core_fire_hook (request->core, N_CORE_HOOK_NEW_REQUEST, &new_request);
}

static void
n_core_fire_transform_properties_hook (NRequest *request)
{
    g_assert (request != NULL);
    g_assert (request->core != NULL);

    NCoreHookTransformPropertiesData transform_data;

    transform_data.request = request;
    n_core_fire_hook (request->core, N_CORE_HOOK_TRANSFORM_PROPERTIES, &transform_data);
}

static GList*
n_core_fire_filter_sinks_hook (NRequest *request, GList *sinks)
{
    g_assert (request != NULL);
    g_assert (request->core != NULL);

    NCoreHookFilterSinksData filter_sinks_data;

    filter_sinks_data.request = request;
    filter_sinks_data.sinks   = sinks;
    n_core_fire_hook (request->core, N_CORE_HOOK_FILTER_SINKS, &filter_sinks_data);

    return filter_sinks_data.sinks;
}

static GList*
n_core_query_capable_sinks (NRequest *request)
{
    g_assert (request != NULL);
    g_assert (request->core != NULL);

    NCore           *core  = request->core;
    GList           *sinks = NULL;
    NSinkInterface **iter  = NULL;

    for (iter = core->sinks; *iter; ++iter) {
        if ((*iter)->funcs.can_handle && !(*iter)->funcs.can_handle (*iter, request))
            continue;

        sinks = g_list_append (sinks, *iter);
    }

    return sinks;
}

static void
n_core_merge_request_properties (NRequest *request, NEvent *event)
{
    g_assert (request != NULL);
    g_assert (event != NULL);

    NProplist *copy = NULL;

    copy = n_proplist_copy (event->properties);
    n_proplist_merge (copy, request->properties);

    n_proplist_free (request->properties);
    request->properties = copy;
}

static void
n_core_send_reply (NRequest *request, NCorePlayerState status)
{
    g_assert (request != NULL);
    g_assert (request->input_iface != NULL);

    if (request->input_iface->funcs.send_reply) {
        request->input_iface->funcs.send_reply (request->input_iface,
            request, status);
    }
}

static void
n_core_send_error (NRequest *request, const char *err_msg)
{
    g_assert (request != NULL);
    g_assert (request->input_iface != NULL);

    if (request->input_iface->funcs.send_error) {
        request->input_iface->funcs.send_error (request->input_iface,
            request, err_msg);
    }
}

static int
n_core_sink_in_list (GList *sinks, NSinkInterface *sink)
{
    if (!sinks || !sink)
        return FALSE;

    return g_list_find (sinks, sink) != NULL ? TRUE : FALSE;
}

static int
n_core_sink_priority_cmp (gconstpointer in_a, gconstpointer in_b)
{
    NSinkInterface *a = (NSinkInterface*) in_a;
    NSinkInterface *b = (NSinkInterface*) in_b;

    if (a->priority > b->priority)
        return -1;
    else if (b->priority > a->priority)
        return 1;

    return 0;
}

static gboolean
n_core_sink_synchronize_done_cb (gpointer userdata)
{
    NRequest       *request   = (NRequest*) userdata;
    NCore          *core      = request->core;
    GList          *iter      = NULL;
    NSinkInterface *sink      = NULL;

    /* setup the maximum timeout callback. */
    n_core_setup_max_timeout (request);

    /* all sinks have been synchronized for the request. call play for every
       prepared sink. */

    request->play_source_id = 0;
    for (iter = g_list_first (request->sinks_prepared); iter; iter = g_list_next (iter)) {
        sink = (NSinkInterface*) iter->data;

        if (!sink->funcs.play (sink, request)) {
            N_WARNING (LOG_CAT "sink '%s' failed play request '%s'",
                sink->name, request->name);

            n_core_fail_sink (core, sink, request);
            return FALSE;
        }

        if (!sink->funcs.prepare) {
            if (n_core_sink_in_list (request->stop_list, sink))
                request->stop_list = g_list_append (request->stop_list, sink);
        }

        request->sinks_playing = g_list_append (request->sinks_playing,
            sink);
    }

    g_list_free (request->sinks_prepared);
    request->sinks_prepared = NULL;

    return FALSE;
}

static void
n_core_stop_sinks (GList *sinks, NRequest *request)
{
    GList          *iter = NULL;
    NSinkInterface *sink = NULL;

    for (iter = g_list_first (sinks); iter; iter = g_list_next (iter)) {
        sink = (NSinkInterface*) iter->data;
	 if (sink && sink->funcs.stop)
	     sink->funcs.stop (sink, request);
    }
}

static int
n_core_prepare_sinks (GList *sinks, NRequest *request)
{
    g_assert (request != NULL);

    NCore          *core = request->core;
    GList          *iter = NULL;
    NSinkInterface *sink = NULL;

    for (iter = g_list_first (sinks); iter; iter = g_list_next (iter)) {
        sink = (NSinkInterface*) iter->data;

        if (!sink->funcs.prepare) {
            N_DEBUG (LOG_CAT "sink has no prepare, synchronizing immediately");
            n_core_synchronize_sink (core, sink, request);
            continue;
        }

        if (!sink->funcs.prepare (sink, request)) {
            N_WARNING (LOG_CAT "sink '%s' failed to prepare request '%s'",
                sink->name, request->name);

            n_core_fail_sink (core, sink, request);
            return FALSE;
        }

        if (!n_core_sink_in_list (request->stop_list, sink))
            request->stop_list = g_list_append (request->stop_list, sink);
    }

    return TRUE;
}

static void
n_translate_fallback (const char *key, const NValue *value, gpointer userdata)
{
    NProplist *props = (NProplist*) userdata;
    gchar *new_key = NULL;

    if (g_str_has_suffix (key, FALLBACK_SUFFIX)) {
        new_key = g_strdup (key);
        new_key[strlen (key) - strlen (FALLBACK_SUFFIX)] = 0;
        n_proplist_set (props, new_key, n_value_copy (value));
        g_free (new_key);
    }
}

static void
n_find_fallback_cb (const char *key, const NValue *value, gpointer userdata)
{
    (void) value;

    gboolean *has_fallbacks = (gboolean*) userdata;
    if (g_str_has_suffix (key, FALLBACK_SUFFIX))
        *has_fallbacks = TRUE;
}

static gboolean
n_core_request_done_cb (gpointer userdata)
{
    NRequest  *request       = (NRequest*) userdata;
    NCore     *core          = request->core;
    NProplist *new_props     = NULL;
    gboolean   has_fallbacks = FALSE;

    /* ensure that maximum timeout is removed. */
    n_core_clear_max_timeout (request);

    /* all sinks have been either completed or the request failed. we will run
       a stop on each sink and then clear out the request. */

    request->stop_source_id = 0;
    core->requests = g_list_remove (core->requests, request);

    N_DEBUG (LOG_CAT "stopping all sinks for request '%s'", request->name);
    n_core_stop_sinks (request->stop_list, request);

    g_list_free (request->stop_list);
    g_list_free (request->sinks_resync);
    g_list_free (request->sinks_playing);
    g_list_free (request->sinks_prepared);
    g_list_free (request->sinks_preparing);
    g_list_free (request->all_sinks);

    if (request->has_failed && request->is_fallback) {
        /* if the fallback failed, bail out. */
        n_core_send_error (request, "request failed!");
        goto done;
    }
    else if (!request->has_failed || request->is_fallback) {
        /* we completed the original one or fallback, complete the event. */
        n_core_send_reply (request, N_CORE_EVENT_COMPLETED);
        goto done;
    }

    if (request->no_event) {
        /* there was no event at all or we did not find one */
        n_core_send_error (request, "fallback failed or no fallback.");
        goto done;
    }

    /* try fallbacks */

    n_proplist_foreach (request->properties, n_find_fallback_cb, &has_fallbacks);
    if (!has_fallbacks) {
        /* no fallbacks for the request, error out */
        n_core_send_error (request, "no fallbacks!");
        goto done;
    }

    /* translate the request properties using the stored original
       properties and translating the fallback keys. */

    N_DEBUG (LOG_CAT "request has failed, restarting with fallback.");

    new_props = n_proplist_copy (request->original_properties);

    NRequest *new_request = n_request_new ();
    new_request->name        = g_strdup (request->name);
    new_request->input_iface = request->input_iface;
    new_request->properties  = new_props;
    new_request->is_fallback = TRUE;

    n_request_free (request);

    n_core_play_request (core, new_request);

    return FALSE;

done:
    /* free the actual request */
    N_DEBUG (LOG_CAT "request '%s' done", request->name);
    n_request_free (request);

    return FALSE;
}

int
n_core_play_request (NCore *core, NRequest *request)
{
    NProplist *new_props     = NULL;

    g_assert (core != NULL);
    g_assert (request != NULL);

    GList  *all_sinks = NULL;

    /* store the original request properties and default timeout */

    request->original_properties = n_proplist_copy (request->properties);
    request->timeout_ms = n_proplist_get_uint (request->properties, POLICY_TIMEOUT_KEY);
    request->core = core;

    /* evaluate the request and context to resolve the correct event for
       this specific request. if no event, then there is no default event
       defined and we are done here. */

    request->event = n_core_evaluate_request (core, request);
    if (!request->event) {
        N_WARNING (LOG_CAT "unable to resolve event for request '%s'",
            request->name);
        request->no_event = TRUE;
        goto fail_request;
    }

    N_DEBUG (LOG_CAT "request '%s' resolved to event '%s'", request->name,
        request->event->name);

    /* fire the hook before merge */

    n_core_fire_new_request_hook (request);

    /* merge and transform */

    n_core_merge_request_properties (request, request->event);

    /* check if fallbacks need to be used */
    if (request->is_fallback) {
        new_props = n_proplist_copy (request->properties);
        n_proplist_foreach (request->properties,
            n_translate_fallback, new_props);
        n_proplist_free (request->properties);
        request->properties  = new_props;
    }

    n_core_fire_transform_properties_hook (request);

    /* query and filter capable sinks */

    all_sinks = n_core_query_capable_sinks (request);
    all_sinks = n_core_fire_filter_sinks_hook (request, all_sinks);

    /* if no sinks left, then nothing to do. */

    if (!all_sinks) {
        N_WARNING (LOG_CAT "no sinks that can handle the request '%s'",
            request->name);
        goto fail_request;
    }

    /* sort the sinks based on their priority. priority is set automatically for
       each sink if "core.sink_order" key is set. */

    all_sinks = g_list_sort (all_sinks, n_core_sink_priority_cmp);

    /* setup the sinks for the play data */

    request->all_sinks       = all_sinks;
    request->sinks_preparing = g_list_copy (all_sinks);
    request->master_sink     = (NSinkInterface*) ((g_list_first (all_sinks))->data);

    /* prepare all sinks that can handle the event. if there is no preparation
       function defined within the sink, then it is synchronized immediately. */

    core->requests = g_list_append (core->requests, request);
    n_core_prepare_sinks (all_sinks, request);

    n_core_send_reply (request, N_CORE_EVENT_PLAYING);

    return TRUE;

fail_request:
    request->has_failed     = TRUE;
    request->stop_source_id = g_idle_add (n_core_request_done_cb, request);

    return TRUE;
}

int
n_core_pause_request (NCore *core, NRequest *request)
{
    g_assert (core != NULL);
    g_assert (request != NULL);

    GList          *iter = NULL;
    NSinkInterface *sink = NULL;
    int all_paused = 1;

    if (request->is_paused) {
        N_DEBUG (LOG_CAT "request '%s' is already paused, no action.",
            request->name);
        return TRUE;
    }

    for (iter = g_list_first (request->all_sinks); iter; iter = g_list_next (iter)) {
        sink = (NSinkInterface*) iter->data;

        if (sink->funcs.pause && !sink->funcs.pause (sink, request)) {
            N_WARNING (LOG_CAT "sink '%s' failed to pause request '%s'",
                sink->name, request->name);
            all_paused = 0;
        }
    }

    if (all_paused)
        n_core_send_reply (request, N_CORE_EVENT_PAUSED);

    request->is_paused = TRUE;
    return TRUE;
}

int
n_core_resume_request (NCore *core, NRequest *request)
{
    g_assert (core != NULL);
    g_assert (request != NULL);

    GList          *iter = NULL;
    NSinkInterface *sink = NULL;
    int all_resumed = 1;

    if (!request->is_paused) {
        N_DEBUG (LOG_CAT "request '%s' is not paused, no action.",
            request->name);
        return TRUE;
    }

    for (iter = g_list_first (request->all_sinks); iter; iter = g_list_next (iter)) {
        sink = (NSinkInterface*) iter->data;

        if (sink->funcs.play && !sink->funcs.play (sink, request)) {
            N_WARNING (LOG_CAT "sink '%s' failed to resume (play) request '%s'",
                sink->name, request->name);
            all_resumed = 0;
        }
    }

    if (all_resumed)
        n_core_send_reply (request, N_CORE_EVENT_PLAYING);

    request->is_paused = FALSE;
    return TRUE;
}

void
n_core_stop_request (NCore *core, NRequest *request, guint timeout)
{
    g_assert (core != NULL);
    g_assert (request != NULL);

    if (request->stop_source_id > 0) {
        N_DEBUG (LOG_CAT "already stopping request '%s'", request->name);
        return;
    }

    if (request->play_source_id > 0) {
        g_source_remove (request->play_source_id);
        request->play_source_id = 0;
    }

    if (timeout > 0)
        request->stop_source_id = g_timeout_add (timeout, n_core_request_done_cb, request);
    else
        request->stop_source_id = g_idle_add (n_core_request_done_cb, request);

    n_core_clear_max_timeout (request);
}

void
n_core_set_resync_on_master (NCore *core, NSinkInterface *sink,
                             NRequest *request)
{
    g_assert (core != NULL);
    g_assert (sink != NULL);
    g_assert (request != NULL);

    if (request->master_sink == sink) {
        N_WARNING (LOG_CAT "no need to add master sink '%s' to resync list.",
            sink->name);
        return;
    }

    if (n_core_sink_in_list (request->sinks_resync, sink))
        return;

    request->sinks_resync = g_list_append (request->sinks_resync,
        sink);

    N_DEBUG (LOG_CAT "sink '%s' set to resynchronize on master sink '%s'",
        sink->name, request->master_sink->name);
}

void
n_core_resynchronize_sinks (NCore *core, NSinkInterface *sink,
                            NRequest *request)
{
    g_assert (core != NULL);
    g_assert (sink != NULL);
    g_assert (request != NULL);

    GList *resync_list = NULL;

    if (request->master_sink != sink) {
        N_WARNING (LOG_CAT "sink '%s' not master sink, not resyncing.",
            sink->name);
        return;
    }

    if (request->play_source_id > 0) {
        N_WARNING (LOG_CAT "already resyncing.");
        return;
    }

    /* add the master sink to prepared list, since it only needs play
       to continue. */

    request->sinks_playing  = g_list_remove (request->sinks_playing,
        request->master_sink);
    request->sinks_prepared = g_list_append (request->sinks_prepared,
        request->master_sink);

    /* if resync list is empty, we'll just trigger play on the master
       sink again. */

    if (!request->sinks_resync) {
        N_DEBUG (LOG_CAT "no sinks in resync list, triggering play for sink '%s'",
            sink->name);
        request->play_source_id = g_idle_add (n_core_sink_synchronize_done_cb,
            request);
        return;
    }

    /* first, we need to copy and clear the resync list. otherwise when the
       list is prepared, duplicates are added. */

    resync_list = g_list_copy (request->sinks_resync);
    g_list_free (request->sinks_resync);
    request->sinks_resync = NULL;

    /* stop all sinks in the resync list. */

    n_core_stop_sinks (resync_list, request);

    /* prepare all sinks in the resync list and re-trigger the playback
       for them. */

    request->sinks_preparing = g_list_copy (resync_list);
    (void) n_core_prepare_sinks (resync_list, request);

    /* clear the list copy. */

    g_list_free (resync_list);
}

void
n_core_synchronize_sink (NCore *core, NSinkInterface *sink, NRequest *request)
{
    g_assert (core != NULL);
    g_assert (sink != NULL);
    g_assert (request != NULL);

    if (request->stop_source_id > 0) {
        N_DEBUG (LOG_CAT "sink '%s' was synchronized, but request is in the process"
                         "of stopping.", sink->name);
        return;
    }

    if (!request->sinks_preparing) {
        N_WARNING (LOG_CAT "sink '%s' synchronized, but no sinks in the list.",
            sink->name);
        return;
    }

    if (!n_core_sink_in_list (request->sinks_preparing, sink)) {
        N_WARNING (LOG_CAT "sink '%s' not in preparing list.",
            sink->name);
        return;
    }

    N_DEBUG (LOG_CAT "sink '%s' synchronized for request '%s'",
        sink->name, request->name);

    request->sinks_preparing = g_list_remove (request->sinks_preparing, sink);
    request->sinks_prepared  = g_list_append (request->sinks_prepared, sink);

    if (!request->sinks_preparing) {
        N_DEBUG (LOG_CAT "all sinks have been synchronized");
        request->play_source_id = g_idle_add (n_core_sink_synchronize_done_cb,
            request);
    }
}

void
n_core_complete_sink (NCore *core, NSinkInterface *sink, NRequest *request)
{
    g_assert (core != NULL);
    g_assert (sink != NULL);
    g_assert (request != NULL);

    if (!request->sinks_playing)
        return;

    if (request->stop_source_id > 0){
        N_DEBUG (LOG_CAT "already completing request '%s'", request->name);
        return;
    }  

    N_DEBUG (LOG_CAT "sink '%s' completed request '%s'",
        sink->name, request->name);

    request->sinks_playing = g_list_remove (request->sinks_playing, sink);
    if (!request->sinks_playing) {
        N_DEBUG (LOG_CAT "all sinks have been completed");
        request->stop_source_id = g_idle_add (n_core_request_done_cb,
            request);
    }
}

void
n_core_fail_sink (NCore *core, NSinkInterface *sink, NRequest *request)
{
    g_assert (core != NULL);
    g_assert (sink != NULL);
    g_assert (request != NULL);

    N_WARNING (LOG_CAT "sink '%s' failed request '%s'",
        sink->name, request->name);

    if (request->stop_source_id > 0)
        return;

    /* sink failed, so request failed */

    request->has_failed     = TRUE;
    request->stop_source_id = g_idle_add (n_core_request_done_cb, request);
}

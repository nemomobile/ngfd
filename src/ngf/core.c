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

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <sys/types.h>
#include <dirent.h>

#include <ngf/log.h>
#include "core-internal.h"
#include "event-internal.h"
#include "request-internal.h"
#include "context-internal.h"

#define PATH_LEN 4096
#define LOG_CAT  "core: "

#define DEFAULT_CONF_PATH     "/usr/share/ngfd"
#define DEFAULT_PLUGIN_PATH   "/usr/lib/ngf"
#define DEFAULT_CONF_FILENAME "ngfd.ini"
#define PLUGIN_CONF_PATH      "plugins.d"
#define EVENT_CONF_PATH       "events.d"

typedef struct _NEventMatchResult
{
    NRequest *request;
    NContext *context;
    gboolean  has_match;
    gboolean  skip_rest;
} NEventMatchResult;

static gchar*     n_core_get_path               (const char *key, const char *default_path);
static NProplist* n_core_load_params            (NCore *core, const char *plugin_name);
static NPlugin*   n_core_load_plugin            (NCore *core, const char *plugin_name);
static void       n_core_unload_plugin          (NCore *core, NPlugin *plugin);
static void       n_core_free_event_list_cb     (gpointer in_key, gpointer in_data, gpointer userdata);
static gint       n_core_sort_event_cb          (gconstpointer a, gconstpointer b);
static void       n_core_dump_value_cb          (const char *key, const NValue *value, gpointer userdata);
static void       n_core_parse_events_from_file (NCore *core, const char *filename);
static int        n_core_parse_events           (NCore *core);
static void       n_core_parse_keytypes         (NCore *core, GKeyFile *keyfile);
static void       n_core_parse_sink_order       (NCore *core, GKeyFile *keyfile);
static int        n_core_parse_configuration    (NCore *core);
static void       n_core_match_event_rule_cb    (const char *key, const NValue *value, gpointer userdata);



static gchar*
n_core_get_path (const char *key, const char *default_path)
{
    g_assert (default_path != NULL);

    const char *env_path = NULL;
    const char *source   = NULL;
    char        path[PATH_LEN];

    source = default_path;
    if (key && (env_path = getenv (key)) != NULL)
        source = env_path;

    strncpy (path, source, PATH_LEN);
    path[PATH_LEN - 1] = '\0';
    return g_strdup (path);
}

static NProplist*
n_core_load_params (NCore *core, const char *plugin_name)
{
    g_assert (core != NULL);
    g_assert (plugin_name != NULL);

    NProplist  *proplist  = NULL;
    GKeyFile   *keyfile   = NULL;
    gchar      *filename  = NULL;
    gchar      *full_path = NULL;
    gchar     **keys      = NULL;
    gchar     **iter      = NULL;
    GError     *error     = NULL;
    gchar      *value     = NULL;

    filename  = g_strdup_printf ("%s.ini", plugin_name);
    full_path = g_build_filename (core->conf_path, PLUGIN_CONF_PATH, filename, NULL);
    keyfile   = g_key_file_new ();

    if (!g_key_file_load_from_file (keyfile, full_path, G_KEY_FILE_NONE, &error)) {
        if (error->code & G_KEY_FILE_ERROR_NOT_FOUND) {
            N_WARNING (LOG_CAT "problem with configuration file '%s': %s",
                filename, error->message);
        }

        goto done;
    }

    keys = g_key_file_get_keys (keyfile, plugin_name, NULL, NULL);
    if (!keys) {
        N_WARNING (LOG_CAT "no group '%s' within configuration file '%s'",
            plugin_name, filename);
        goto done;
    }

    proplist = n_proplist_new ();
    for (iter = keys; *iter; ++iter) {
        if ((value = g_key_file_get_string (keyfile, plugin_name, *iter, NULL)) == NULL)
            continue;

        N_DEBUG (LOG_CAT "+ plugin parameter: %s = %s", *iter, value);
        n_proplist_set_string (proplist, *iter, value);
        g_free (value);
    }

    g_strfreev (keys);

done:
    if (error)
        g_error_free (error);

    if (keyfile)
        g_key_file_free (keyfile);

    g_free          (full_path);
    g_free          (filename);

    return proplist;
}

static NPlugin*
n_core_load_plugin (NCore *core, const char *plugin_name)
{
    g_assert (core != NULL);
    g_assert (plugin_name != NULL);

    NPlugin *plugin    = NULL;
    gchar   *filename  = NULL;
    gchar   *full_path = NULL;

    filename  = g_strdup_printf ("libngfd_%s.so", plugin_name);
    full_path = g_build_filename (core->plugin_path, filename, NULL);

    if (!(plugin = n_plugin_load (full_path)))
        goto done;

    plugin->core   = core;
    plugin->params = n_core_load_params (core, plugin_name);

    if (!plugin->load (plugin))
        goto done;

    N_DEBUG (LOG_CAT "loaded plugin '%s'", plugin_name);

    g_free (full_path);
    g_free (filename);

    return plugin;

done:
    N_ERROR (LOG_CAT "unable to load plugin '%s'", plugin_name);

    if (plugin)
        n_plugin_unload (plugin);

    g_free (full_path);
    g_free (filename);

    return NULL;
}

static void
n_core_unload_plugin (NCore *core, NPlugin *plugin)
{
    g_assert (core != NULL);
    g_assert (plugin != NULL);

    N_DEBUG (LOG_CAT "unloading plugin '%s'", plugin->get_name ());
    plugin->unload (plugin);
    n_plugin_unload (plugin);
}

NCore*
n_core_new (int *argc, char **argv)
{
    NCore *core = NULL;

    (void) argc;
    (void) argv;

    core = g_new0 (NCore, 1);

    /* query the default paths */

    core->conf_path   = n_core_get_path ("NGF_CONF_PATH", DEFAULT_CONF_PATH);
    core->plugin_path = n_core_get_path ("NGF_PLUGIN_PATH", DEFAULT_PLUGIN_PATH);
    core->context     = n_context_new ();

    core->event_table = g_hash_table_new_full (g_str_hash, g_str_equal,
        g_free, NULL);

    core->key_types = g_hash_table_new_full (g_str_hash, g_str_equal,
        g_free, NULL);

    return core;
}

static void
n_core_free_event_list_cb (gpointer in_key, gpointer in_data, gpointer userdata)
{
    (void) in_key;
    (void) userdata;

    GList      *event_list = (GList*) in_data;
    GList      *iter       = NULL;
    NEvent     *event      = NULL;

    for (iter = g_list_first (event_list); iter; iter = g_list_next (iter)) {
        event = (NEvent*) iter->data;
        n_event_free (event);
    }

    g_list_free (event_list);
}

void
n_core_free (NCore *core)
{
    g_assert (core != NULL);

    if (!core->shutdown_done)
        n_core_shutdown (core);

    if (core->sink_order) {
        g_list_foreach (core->sink_order, (GFunc) g_free, NULL);
        g_list_free (core->sink_order);
    }

    g_hash_table_destroy (core->key_types);

    g_list_free          (core->event_list);
    g_hash_table_foreach (core->event_table, n_core_free_event_list_cb, NULL);
    g_hash_table_destroy (core->event_table);

    n_context_free (core->context);
    g_free (core->plugin_path);
    g_free (core->conf_path);
    g_free (core);
}

static void
n_core_set_sink_priorities (NSinkInterface **sink_list, GList *sink_order)
{
    NSinkInterface **sink = NULL;
    GList           *list = NULL;
    GList           *iter = NULL;
    const char      *name = NULL;
    int              prio = 0;

    list = g_list_copy (sink_order);
    list = g_list_reverse (list);

    for (iter = g_list_first (list); iter; iter = g_list_next (iter)) {
        name = (const char*) iter->data;

        for (sink = sink_list; *sink; ++sink) {
            if (g_str_equal ((*sink)->name, name)) {
                N_DEBUG (LOG_CAT "sink '%s' priority set to %d",
                    (*sink)->name, prio);

                (*sink)->priority = prio;
                ++prio;
                break;
            }
        }
    }

    g_list_free (list);
}

int
n_core_initialize (NCore *core)
{
    g_assert (core != NULL);
    g_assert (core->conf_path != NULL);
    g_assert (core->plugin_path != NULL);

    NSinkInterface  **sink   = NULL;
    NInputInterface **input  = NULL;
    NPlugin          *plugin = NULL;
    GList            *p      = NULL;

    /* setup hooks */

    n_hook_init (&core->hooks[N_CORE_HOOK_INIT_DONE]);
    n_hook_init (&core->hooks[N_CORE_HOOK_TRANSFORM_PROPERTIES]);
    n_hook_init (&core->hooks[N_CORE_HOOK_FILTER_SINKS]);

    /* load the default configuration. */

    if (!n_core_parse_configuration (core))
        goto failed_init;

    /* check for required plugins. */

    if (!core->required_plugins && !core->optional_plugins) {
        N_ERROR (LOG_CAT "no plugins to load defined in configuration");
        goto failed_init;
    }

    /* load events from the given event path. */

    if (!n_core_parse_events (core))
        goto failed_init;

    /* load all plugins */

    /* first mandatory plugins */
    for (p = g_list_first (core->required_plugins); p; p = g_list_next (p)) {
        if (!(plugin = n_core_load_plugin (core, (const char*) p->data)))
            goto failed_init;

        core->plugins = g_list_append (core->plugins, plugin);
    }

    /* then optional plugins */
    for (p = g_list_first (core->optional_plugins); p; p = g_list_next (p)) {
        if ((plugin = n_core_load_plugin (core, (const char*) p->data)))
            core->plugins = g_list_append (core->plugins, plugin);

        if (!plugin)
            N_INFO (LOG_CAT "optional plugin %s not loaded.", p->data);
    }

    /* setup the sink priorities based on the sink-order */

    n_core_set_sink_priorities (core->sinks, core->sink_order);

    /* initialize all sinks. if no sinks, we're done. */

    if (!core->sinks) {
        N_ERROR (LOG_CAT "no plugin has registered sink interface");
        goto failed_init;
    }

    for (sink = core->sinks; *sink; ++sink) {
        if ((*sink)->funcs.initialize && !(*sink)->funcs.initialize (*sink)) {
            N_ERROR (LOG_CAT "sink '%s' failed to initialize", (*sink)->name);
            goto failed_init;
        }
    }

    /* initialize all inputs. */

    if (!core->inputs) {
        N_ERROR (LOG_CAT "no plugin has registered input interface");
        goto failed_init;
    }

    for (input = core->inputs; *input; ++input) {
        if ((*input)->funcs.initialize && !(*input)->funcs.initialize (*input)) {
            N_ERROR (LOG_CAT "input '%s' failed to initialize", (*input)->name);
            goto failed_init;
        }
    }

    /* fire the init done hook. */

    n_core_fire_hook (core, N_CORE_HOOK_INIT_DONE, NULL);

    return TRUE;

failed_init:
    return FALSE;
}

static void
unload_plugin_cb (gpointer data, gpointer userdata)
{
    NCore   *core   = (NCore*) userdata;
    NPlugin *plugin = (NPlugin*) data;
    n_core_unload_plugin (core, plugin);
}

void
n_core_shutdown (NCore *core)
{
    g_assert (core != NULL);

    NInputInterface **input = NULL;
    NSinkInterface  **sink  = NULL;

    /* shutdown all inputs */

    if (core->inputs) {
        for (input = core->inputs; *input; ++input) {
            if ((*input)->funcs.shutdown)
                (*input)->funcs.shutdown (*input);
        }
    }

    /* shutdown all sinks */

    if (core->sinks) {
        for (sink = core->sinks; *sink; ++sink) {
            if ((*sink)->funcs.shutdown)
                (*sink)->funcs.shutdown (*sink);
        }
    }

    if (core->plugins) {
        g_list_foreach (core->plugins, unload_plugin_cb, core);
        g_list_free (core->plugins);
        core->plugins = NULL;
    }

    if (core->required_plugins) {
        g_list_foreach (core->required_plugins, (GFunc) g_free, NULL);
        g_list_free (core->required_plugins);
        core->required_plugins = NULL;
    }

    if (core->optional_plugins) {
        g_list_foreach (core->optional_plugins, (GFunc) g_free, NULL);
        g_list_free (core->optional_plugins);
        core->optional_plugins = NULL;
    }

    core->shutdown_done = TRUE;
}

void
n_core_register_sink (NCore *core, const NSinkInterfaceDecl *iface)
{
    g_assert (core != NULL);
    g_assert (iface->name != NULL);
    g_assert (iface->play != NULL);
    g_assert (iface->stop != NULL);

    NSinkInterface *sink = NULL;
    sink = g_new0 (NSinkInterface, 1);
    sink->name  = iface->name;
    sink->core  = core;
    sink->funcs = *iface;

    core->num_sinks++;
    core->sinks = (NSinkInterface**) g_realloc (core->sinks,
        sizeof (NSinkInterface*) * (core->num_sinks + 1));

    core->sinks[core->num_sinks-1] = sink;
    core->sinks[core->num_sinks]   = NULL;

    N_DEBUG (LOG_CAT "sink interface '%s' registered", sink->name);
}

void
n_core_register_input (NCore *core, const NInputInterfaceDecl *iface)
{
    NInputInterface *input = NULL;

    g_assert (core != NULL);
    g_assert (iface->name != NULL);

    input = g_new0 (NInputInterface, 1);
    input->name  = iface->name;
    input->core  = core;
    input->funcs = *iface;

    core->num_inputs++;
    core->inputs = (NInputInterface**) g_realloc (core->inputs,
        sizeof (NInputInterface*) * (core->num_inputs + 1));

    core->inputs[core->num_inputs-1] = input;
    core->inputs[core->num_inputs]   = NULL;

    N_DEBUG (LOG_CAT "input interface '%s' registered", input->name);
}

static gint
n_core_sort_event_cb (gconstpointer a, gconstpointer b)
{
    const NEvent *ea = (const NEvent*) a;
    const NEvent *eb = (const NEvent*) b;

    guint numa = ea->rules ? n_proplist_size (ea->rules) : 0;
    guint numb = eb->rules ? n_proplist_size (eb->rules) : 0;

    return (numa > numb) ? -1 : ((numa < numb) ? 1 : 0);
}

static void
n_core_dump_value_cb (const char *key, const NValue *value, gpointer userdata)
{
    (void) userdata;

    gchar *value_str = n_value_to_string ((NValue*) value);
    N_DEBUG (LOG_CAT "+ %s = %s", key, value_str);
    g_free (value_str);
}

void
n_core_add_event (NCore *core, NEvent *event)
{
    g_assert (core != NULL);
    g_assert (event != NULL);

    GList  *event_list = NULL;
    GList  *iter       = NULL;
    NEvent *found      = NULL;

    /* get the event list for the specific event name. */

    event_list = g_hash_table_lookup (core->event_table, event->name);

    /* iterate through the event list and try to find an event that has the
       same rules. */

    for (iter = g_list_first (event_list); iter; iter = g_list_next (iter)) {
        found = (NEvent*) iter->data;

        if (n_proplist_match_exact (found->rules, event->rules)) {
            /* match found. merge the properties to the pre-existing event
               and free the new one. */

            N_DEBUG (LOG_CAT "merging event '%s'", event->name);
            n_proplist_foreach (event->rules, n_core_dump_value_cb, NULL);

            n_proplist_merge (found->properties, event->properties);
            n_event_free (event);

            return;
        }
    }

    /* completely new event, add it to the list and sort it. */

    N_DEBUG (LOG_CAT "new event '%s'", event->name);
    if (n_proplist_size (event->rules) > 0)
        n_proplist_foreach (event->rules, n_core_dump_value_cb, NULL);
    else
        N_DEBUG (LOG_CAT "+ default");

    N_DEBUG (LOG_CAT "properties");
    n_proplist_foreach (event->properties, n_core_dump_value_cb, NULL);

    event_list = g_list_append (event_list, event);
    event_list = g_list_sort (event_list, n_core_sort_event_cb);
    g_hash_table_replace (core->event_table, g_strdup (event->name), event_list);

    core->event_list = g_list_append (core->event_list, event);
}

static void
n_core_parse_events_from_file (NCore *core, const char *filename)
{
    g_assert (core != NULL);
    g_assert (filename != NULL);

    GKeyFile  *keyfile    = NULL;
    GError    *error      = NULL;
    gchar    **group_list = NULL;
    gchar    **group      = NULL;
    NEvent    *event      = NULL;

    keyfile = g_key_file_new ();
    if (!g_key_file_load_from_file (keyfile, filename, G_KEY_FILE_NONE, &error)) {
        N_WARNING (LOG_CAT "failed to load event file: %s", error->message);
        g_error_free    (error);
        g_key_file_free (keyfile);
        return;
    }

    /* each unique group is considered as an event, even if split within
       separate files. */

    N_DEBUG (LOG_CAT "processing event file '%s'", filename);

    group_list = g_key_file_get_groups (keyfile, NULL);
    for (group = group_list; *group; ++group) {
        event = n_event_new_from_group (core, keyfile, *group);
        if (event)
            n_core_add_event (core, event);
    }

    g_strfreev      (group_list);
    g_key_file_free (keyfile);
}

static int
n_core_parse_events (NCore *core)
{
    gchar         *path       = NULL;
    DIR           *parent_dir = NULL;
    struct dirent *walk       = NULL;
    gchar         *filename   = NULL;

    /* find all the events within the given path */

    path = g_build_filename (core->conf_path, EVENT_CONF_PATH, NULL);
    parent_dir = opendir (path);
    if (!parent_dir) {
        N_ERROR (LOG_CAT "failed to open event path '%s'", path);
        g_free (path);
        return FALSE;
    }

    while ((walk = readdir (parent_dir)) != NULL) {
        if (walk->d_type & DT_REG) {
            filename = g_build_filename (path, walk->d_name, NULL);
            n_core_parse_events_from_file (core, filename);
            g_free (filename);
        }
    }

    closedir (parent_dir);
    g_free   (path);

    return TRUE;
}

static void
n_core_parse_keytypes (NCore *core, GKeyFile *keyfile)
{
    g_assert (core != NULL);
    g_assert (keyfile != NULL);

    gchar **conf_keys = NULL;
    gchar **key       = NULL;
    gchar  *value     = NULL;
    int     key_type  = 0;

    /* load all the event configuration key entries. */

    conf_keys = g_key_file_get_keys (keyfile, "keytypes", NULL, NULL);
    if (!conf_keys)
        return;

    for (key = conf_keys; *key; ++key) {
        value = g_key_file_get_string (keyfile, "keytypes", *key, NULL);
        if (!value) {
            N_WARNING (LOG_CAT "no datatype defined for key '%s'", *key);
            continue;
        }

        if (strncmp (value, "INTEGER", 7) == 0)
            key_type = N_VALUE_TYPE_INT;
        else if (strncmp (value, "STRING", 6) == 0)
            key_type = N_VALUE_TYPE_STRING;
        else if (strncmp (value, "BOOLEAN", 7) == 0)
            key_type = N_VALUE_TYPE_BOOL;

        if (!key_type) {
            N_WARNING (LOG_CAT "unrecognized datatype '%s' for key '%s'",
                value, *key);
            g_free (value);
            continue;
        }

        N_DEBUG (LOG_CAT "new key type '%s' = %s", *key, value);
        g_hash_table_replace (core->key_types, g_strdup (*key), (gpointer) key_type);
        g_free (value);
    }

    g_strfreev (conf_keys);
}

static void
n_core_parse_sink_order (NCore *core, GKeyFile *keyfile)
{
    g_assert (core != NULL);
    g_assert (keyfile != NULL);

    gchar **sink_list = NULL;
    gchar **item      = NULL;
    gsize   num_items = 0;

    sink_list = g_key_file_get_string_list (keyfile, "general", "sink-order",
        &num_items, NULL);

    if (!sink_list) {
        N_WARNING (LOG_CAT "no sink-order, re-synchronization may be undefined.");
        return;
    }

    for (item = sink_list; *item; ++item)
        core->sink_order = g_list_append (core->sink_order, g_strdup (*item));

    g_strfreev (sink_list);
}

static void
parse_plugins (gchar **plugins, GList **list)
{
    g_assert (plugins);

    gchar    **item       = NULL;

    for (item = plugins; *item; ++item)
        *list = g_list_append (*list, g_strdup (*item));
}

static int
n_core_parse_configuration (NCore *core)
{
    g_assert (core != NULL);
    g_assert (core->conf_path != NULL);

    GKeyFile  *keyfile    = NULL;
    GError    *error      = NULL;
    gchar     *filename   = NULL;
    gchar    **plugins    = NULL;

    filename = g_build_filename (core->conf_path, DEFAULT_CONF_FILENAME, NULL);
    keyfile  = g_key_file_new ();

    if (!g_key_file_load_from_file (keyfile, filename, G_KEY_FILE_NONE, &error)) {
        N_WARNING (LOG_CAT "failed to load configuration file file: %s", error->message);
        g_error_free    (error);
        g_key_file_free (keyfile);
        g_free          (filename);
        return FALSE;
    }

    N_DEBUG (LOG_CAT "parsing configuration file '%s'", filename);

    /* parse the required plugins. */
    if ((plugins = g_key_file_get_string_list (keyfile, "general", "plugins", NULL, NULL)) != NULL) {
        parse_plugins (plugins, &core->required_plugins);
        g_strfreev (plugins);
    }

    /* parse the optional plugins. */
    if ((plugins = g_key_file_get_string_list (keyfile, "general", "plugins-optional", NULL, NULL)) != NULL) {
        parse_plugins (plugins, &core->optional_plugins);
        g_strfreev (plugins);
    }

    /* load all the event configuration key entries. */

    n_core_parse_keytypes (core, keyfile);

    /* load the sink order. the first sink has the highest priority. */

    n_core_parse_sink_order (core, keyfile);

    g_key_file_free (keyfile);
    g_free          (filename);

    return TRUE;
}

static const char*
n_core_match_and_strip_prefix (const char *value, const char *prefix)
{
    g_assert (value != NULL);
    g_assert (prefix != NULL);

    if (g_str_has_prefix (value, prefix))
        return (const char*) (value + strlen (prefix));

    return NULL;
}

static void
n_core_match_event_rule_cb (const char *key, const NValue *value,
                            gpointer userdata)
{
    NEventMatchResult *result      = (NEventMatchResult*) userdata;
    NRequest          *request     = result->request;
    NValue            *match_value = NULL;
    const char        *context_key = NULL;
    const char        *str         = NULL;

    if (result->skip_rest)
        return;

    /* assume positive result */

    result->has_match = TRUE;

    /* if the key has a context@ prefix, then we will lookup the value from
       the current context. */

    context_key = n_core_match_and_strip_prefix (key, "context@");
    match_value = context_key ?
        (NValue*) n_context_get_value (result->context, context_key) :
        (NValue*) n_proplist_get (request->properties, key);

    /* if match value has a *, then any value for request will do. */

    str = n_value_get_string ((NValue*) value);
    if (str && g_str_equal (str, "*"))
        return;

    /* the moment we find a key and value that does not match, we're done
       here. */

    if (!match_value || !n_value_equals (value, match_value)) {
        result->has_match  = FALSE;
        result->skip_rest  = TRUE;
    }
}

NEvent*
n_core_evaluate_request (NCore *core, NRequest *request)
{
    g_assert (core != NULL);
    g_assert (request != NULL);

    NEvent *event      = NULL;
    NEvent *found      = NULL;
    GList  *event_list = NULL;
    GList  *iter       = NULL;

    NEventMatchResult result;

    N_DEBUG (LOG_CAT "evaluating events for request '%s'",
        request->name);

    /* find the list of events that have the same name. */

    event_list = (GList*) g_hash_table_lookup (core->event_table, request->name);
    if (!event_list)
        return NULL;

    /* for each event, match the properties. */

    for (iter = g_list_first (event_list); iter; iter = g_list_next (iter)) {
        event = (NEvent*) iter->data;

        /* default event with no properties, accept. */

        if (n_proplist_size (event->rules) == 0) {
            found = event;
            break;
        }

        result.request    = request;
        result.context    = core->context;
        result.has_match  = FALSE;
        result.skip_rest  = FALSE;

        n_proplist_foreach (event->rules, n_core_match_event_rule_cb, &result);

        if (result.has_match) {
            found = event;
            break;
        }
    }

    if (found) {
        N_DEBUG (LOG_CAT "evaluated to '%s'", found->name);
        n_proplist_foreach (event->rules, n_core_dump_value_cb, NULL);
    }

    return found;
}

NContext*
n_core_get_context (NCore *core)
{
    return (core != NULL) ? core->context : NULL;
}

GList*
n_core_get_requests (NCore *core)
{
    if (!core)
        return NULL;

    return core->requests;
}

NSinkInterface**
n_core_get_sinks (NCore *core)
{
    if (!core)
        return NULL;

    return core->sinks;
}

GList*
n_core_get_events (NCore *core)
{
    if (!core)
        return NULL;

    return core->event_list;
}

int
n_core_connect (NCore *core, NCoreHook hook, int priority,
                NHookCallback callback, void *userdata)
{
    if (!core || !callback)
        return FALSE;

    if (hook >= N_CORE_HOOK_LAST)
        return FALSE;

    N_DEBUG (LOG_CAT "0x%X connected to hook '%s'", (unsigned int) callback,
        n_core_hook_to_string (hook));

    n_hook_connect (&core->hooks[hook], priority, callback, userdata);

    return TRUE;
}

void
n_core_disconnect (NCore *core, NCoreHook hook, NHookCallback callback,
                   void *userdata)
{
    if (!core || !callback)
        return;

    if (hook >= N_CORE_HOOK_LAST)
        return;

    n_hook_disconnect (&core->hooks[hook], callback, userdata);
}

void
n_core_fire_hook (NCore *core, NCoreHook hook, void *data)
{
    if (!core || hook >= N_CORE_HOOK_LAST)
        return;

    N_DEBUG (LOG_CAT "firing hook '%s'", n_core_hook_to_string (hook));
    n_hook_fire (&core->hooks[hook], data);
}

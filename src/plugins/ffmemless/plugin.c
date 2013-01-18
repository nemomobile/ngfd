/*
 * ngfd - Non-graphic feedback daemon, plugin for FF memless force feedback
 *
 * Copyright (C) 2013 Jolla Oy.
 * Contact: Kalle Jokiniemi <kalle.jokiniemi@jollamobile.com>
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

#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ngf/plugin.h>
#include <linux/input.h>

#include "ffmemless.h"

#define LOG_CAT "ffmemless: "
#define FFM_PLUGIN_NAME		"ffmemless"

#define FFM_SYSTEM_CONFIG_FILE	"system_effects_file"
#define FFM_DEVFILE_KEY		"device_file_path"
#define FFM_EFFECTLIST_KEY	"supported_effects"
#define FFM_EFFECT_KEY		"ffmemless.effect"
#define FFM_EFFECT_PREFIX	"NGF_"
#define FFM_MAX_PARAM_LEN	80
#define FFM_DEFAULT_EFFECT	"NGF_DEFAULT"

#define NGF_DEFAULT_TYPE	FF_RUMBLE
#define NGF_DEFAULT_DURATION	240
#define NGF_DEFAULT_DELAY	0
#define NGF_DEFAULT_RMAGNITUDE	27000
#define NGF_DEFAULT_PMAGNITUDE	14000

N_PLUGIN_NAME(FFM_PLUGIN_NAME)
N_PLUGIN_DESCRIPTION("Vibra plugin using ff-memless kernel backend")
N_PLUGIN_VERSION("0.9")

static struct ffm_data {
	int 		dev_file;
	const NProplist *ngfd_props;
	NProplist *sys_props;
	GHashTable	*effects;
} ffm;

static int ffm_setup_device(const NProplist *props, int *dev_fd)
{
	const char *device_file = n_proplist_get_string(props, FFM_DEVFILE_KEY);

	if (device_file == NULL) {
		N_DEBUG (LOG_CAT "No %s provided, using automatic detection",
					FFM_DEVFILE_KEY);
		*dev_fd = ffmemless_evdev_file_search();
	} else {
		N_DEBUG (LOG_CAT "%s found with value \"%s\"",
					FFM_DEVFILE_KEY, device_file);
		*dev_fd = ffmemless_evdev_file_open(device_file);
		/* do a fall back to automatic search, in case open fails */
		if (*dev_fd == -1) {
			N_DEBUG (LOG_CAT "%s is not a valid event device",
					device_file);
			N_DEBUG (LOG_CAT "Falling back to automatic detection");
			*dev_fd = ffmemless_evdev_file_search();
		}
	}
	if (*dev_fd == -1) {
		N_DEBUG (LOG_CAT "Failed to open ff-memless event device");
		return -1;
	} else {
		N_DEBUG (LOG_CAT "Successfully opened ff-memless event device");
		return 0;
	}
}
static void ffm_close_device(int fd)
{
	ffmemless_evdev_file_close(fd);
}

/* Fetches string value from props with full key combined from prefix and key */
static const char *ffm_get_str_value(const NProplist *props, const char *prefix,
					const char *key)
{
	char full_key[FFM_MAX_PARAM_LEN];
	sprintf(full_key, "%s%s", prefix, key);
	return n_proplist_get_string(props, full_key);
}

/* Fetches a integer value from props and makes sure it is in [min,max] range */
static int ffm_get_int_value(const NProplist *props, const char *prefix,
					const char *key, int min, int max)
{
	const char *value;
	int result;

	value = ffm_get_str_value(props, prefix, key);
	if (value == NULL)
		result = 0;
	else
		result = atoi(value);

	if (result > max) {
		N_DEBUG (LOG_CAT "%s%s too high, rounding to %d",
						prefix, key, max);
		result = max;
	} else if (result < min) {
		N_DEBUG (LOG_CAT "%s%s too small, rounding to %d",
						prefix, key, min);
		result = min;
	}
	return result;
}

/* Read and create proplist from a custom file */
static NProplist* ffm_read_props(const char *file_name)
{
	NProplist  *proplist  = NULL;
	GKeyFile   *keyfile   = NULL;
	gchar     **keys      = NULL;
	gchar     **iter      = NULL;
	GError     *error     = NULL;
	gchar      *value     = NULL;

	keyfile   = g_key_file_new ();

	N_DEBUG (LOG_CAT "Loading properties from file \"%s\"", file_name);

	if (!g_key_file_load_from_file (keyfile, file_name, G_KEY_FILE_NONE,
								&error)) {
		N_WARNING (LOG_CAT "problem with configuration file"
				" '%s': %s", file_name, error->message);
		goto done;
	}

	keys = g_key_file_get_keys (keyfile, FFM_PLUGIN_NAME, NULL, NULL);
	if (!keys) {
		N_WARNING (LOG_CAT "no group '%s' within configuration file "
				"'%s'", FFM_PLUGIN_NAME, file_name);
		goto done;
	}

	proplist = n_proplist_new ();

	for (iter = keys; *iter; ++iter) {
		if ((value = g_key_file_get_string (keyfile,
					FFM_PLUGIN_NAME, *iter, NULL)) == NULL)
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

	return proplist;
}

/*
 * Create a Hash table of effects from a string of semicolon separated keys.
 * Values of keys will be initialized to -1.
 */
static GHashTable *ffm_new_effect_list(const char *effect_data)
{
	GHashTable *list = NULL;
	gchar **effect_names;
	int i = 0;
	gint16 *id;

	if (!effect_data) {
		N_WARNING (LOG_CAT "NULL effect_data pointer");
		return NULL;
	}

	N_DEBUG (LOG_CAT "creating effect list for %s", effect_data);

	effect_names = g_strsplit((const gchar*) effect_data, ";", 0);
	if (!effect_names[0]) {
		N_WARNING (LOG_CAT "Empty effect_data string");
		goto ffm_effect_list_done;
	}

	list = g_hash_table_new_full(g_str_hash,  g_str_equal,
					g_free, g_free);

	for (i = 0; effect_names[i] != NULL; i++) {
		/* Add effect key to effect list with initial value -1 */
		id = g_new(gint16, 1);
		*id = -1;
		g_hash_table_insert(list, strdup(effect_names[i]), id);
	}

ffm_effect_list_done:
	g_strfreev(effect_names);

	return list;
}

/* Load the default fall-back effect and insert it to effects table */
static int ffm_setup_default_effect(GHashTable *effects, int dev_fd)
{
	struct ff_effect ff;
	gint16 *id;

	memset(&ff, 0, sizeof(struct ff_effect));
	id = (gint16 *)g_hash_table_lookup(effects, FFM_DEFAULT_EFFECT);
	if (!id) {
		id = g_new(gint16, 1);
		*id = -1;
		ff.id = -1;
		g_hash_table_insert(effects, g_strdup(FFM_DEFAULT_EFFECT),
				id);
	} else {
		ff.id = *id;
	}

	ff.type = FF_RUMBLE;
	ff.replay.length = NGF_DEFAULT_DURATION;
	ff.u.rumble.strong_magnitude = NGF_DEFAULT_RMAGNITUDE;
	ff.u.rumble.weak_magnitude = NGF_DEFAULT_RMAGNITUDE;
	if (ffmemless_upload_effect(&ff, dev_fd)) {
		N_DEBUG (LOG_CAT "%s effect load failed", FFM_DEFAULT_EFFECT);
		return -1;
	}
	*id = ff.id;
	N_DEBUG (LOG_CAT "Added effect %s, id %d", FFM_DEFAULT_EFFECT, *id);
	return 0;
}

/*
 * Setup parameters for given effects (if any parameters exist in props), and
 * load new or update existing effects to kernel.
 */
static int ffm_setup_effects(const NProplist *props, GHashTable *effects)
{
	const char *value;
	char *key;
	struct ff_effect ff;
	gint16 *id;
	GHashTableIter iter;

	if(!effects || !props) {
		N_WARNING (LOG_CAT "ffm_setup_effects: invalid parameters");
		return -1;
	}

	if (!g_hash_table_size(effects)) {
		N_WARNING (LOG_CAT "No effects defined");
		return -1;
	}

	g_hash_table_iter_init(&iter, effects);

	/* Create and load all configured effects */
	while (g_hash_table_iter_next(&iter, (gpointer) &key, (gpointer) &id)) {
		memset(&ff, 0, sizeof(struct ff_effect));
		N_DEBUG (LOG_CAT "got key %s, id %d", key, *id);

		value = ffm_get_str_value(props, key, "_TYPE");
		if (!value) {
			/*
			 * _TYPE is mandatory. If it is not defined, old
			 * parameters are kept. Even if there would be other
			 * parameters defined. This is because the logic is
			 * to always overwrite all parameters for given effect.
			 */
			N_DEBUG (LOG_CAT "No %s_TYPE defined, skipping", key);
			continue;
		} else if (!strcmp(value, "rumble")) {
			ff.type = FF_RUMBLE;
		} else if (!strcmp(value, "periodic")) {
			ff.type = FF_PERIODIC;
		} else {
			N_WARNING (LOG_CAT "unknown effect type %s", value);
			continue;
		}

		ff.id = *id;

		N_DEBUG (LOG_CAT "Creating / updating effect %s", key);

		ff.replay.length = ffm_get_int_value(props, key,
						"_DURATION", 0, UINT16_MAX);
		if (!ff.replay.length) {
			N_WARNING (LOG_CAT "%s%s not defined, using %dms",
				key, "_DURATION",
				NGF_DEFAULT_DURATION);
			ff.replay.length = NGF_DEFAULT_DURATION;
		}

		ff.replay.delay = ffm_get_int_value(props, key,
						"_DELAY", 0, UINT16_MAX);

		value = ffm_get_str_value(props, key, "_DIRECTION");
		if (!g_strcmp0(value, "reverse"))
				ff.direction = 0xC000;	/* reverse */
		else
			ff.direction = 0x4000;	/* forward */

		/* Fill effect type dependent parameters */
		if (ff.type == FF_RUMBLE) {
			N_DEBUG (LOG_CAT "rumble effect");
			ff.type = FF_RUMBLE;

			ff.u.rumble.strong_magnitude = ffm_get_int_value(props,
				key, "_MAGNITUDE", 0, UINT16_MAX);
			if (!ff.u.rumble.strong_magnitude) {
				N_WARNING (LOG_CAT "%s_MAGNITUDE not given,"
						" using %d", key,
						NGF_DEFAULT_RMAGNITUDE);
				ff.u.rumble.strong_magnitude =
						NGF_DEFAULT_RMAGNITUDE;
			}
			/* Usually no separate weak motor, use same value */
			ff.u.rumble.weak_magnitude =
						ff.u.rumble.strong_magnitude;

		} else if (ff.type == FF_PERIODIC) {
			N_DEBUG (LOG_CAT "periodic effect");
			ff.type = FF_PERIODIC;

			value = ffm_get_str_value(props, key,
								"_WAVEFORM");
			if (!g_strcmp0(value, "square"))
				ff.u.periodic.waveform = FF_SQUARE;
			else if (!g_strcmp0(value, "triangle"))
				ff.u.periodic.waveform = FF_TRIANGLE;
			else
				ff.u.periodic.waveform = FF_SINE;

			ff.u.periodic.period = ffm_get_int_value(props,
				key, "_PERIOD", 0, UINT16_MAX);

			ff.u.periodic.magnitude = ffm_get_int_value(props,
				key, "_MAGNITUDE", 0, INT16_MAX);
			if (!ff.u.periodic.magnitude) {
				N_WARNING (LOG_CAT "%s_MAGNITUDE not given,"
						" using %d", key,
						NGF_DEFAULT_PMAGNITUDE);
				ff.u.periodic.magnitude =
						NGF_DEFAULT_PMAGNITUDE;
			}

			ff.u.periodic.offset = ffm_get_int_value(props,
				key, "_OFFSET", 0, INT16_MAX);

			ff.u.periodic.phase = ffm_get_int_value(props,
				key, "_PHASE", 0, UINT16_MAX);

			ff.u.periodic.envelope.attack_length = ffm_get_int_value
				(props, key, "_ATTACK",
				0, UINT16_MAX);

			ff.u.periodic.envelope.attack_level = ffm_get_int_value(
				props, key, "_ALEVEL",
				0, UINT16_MAX);

			ff.u.periodic.envelope.fade_length = ffm_get_int_value(
				props, key, "_FADE",
				0, UINT16_MAX);

			ff.u.periodic.envelope.fade_level = ffm_get_int_value(
				props, key, "_FLEVEL",
				0, UINT16_MAX);
		} else {
			N_WARNING (LOG_CAT "unknown effect type");
			continue;
		}

		/* Finally load the effect */
		if (ffmemless_upload_effect(&ff, ffm.dev_file)) {
			N_DEBUG (LOG_CAT "%s effect loading failed",
							key);
			goto ffm_eff_error1;
		}
		/* If the id was -1, kernel has updated it with valid value */
		*id = ff.id;
		N_DEBUG (LOG_CAT "Created effect %s with id %d", key, *id);
		N_DEBUG (LOG_CAT "Parameters:\n"
			"type = 0x%x\n"
			"length = %dms\n"
			"delay = %dms\n"
			"strong rumble magn = 0x%x\n"
			"weak rumble magn = 0x%x\n"
			"per_waveform = 0x%x\n"
			"period = %dms\n"
			"periodic magnitude = 0x%x\n"
			"offset = 0x%x\n"
			"phase = %d\n"
			"att = %ums\n"
			"att_lev = 0x%x\n"
			"fade = %ums\n"
			"fade_lev = 0x%x\n",
			ff.type,
			ff.replay.length,
			ff.replay.delay,
			ff.u.rumble.strong_magnitude,
			ff.u.rumble.weak_magnitude,
			ff.u.periodic.waveform,
			ff.u.periodic.period,
			ff.u.periodic.magnitude,
			ff.u.periodic.offset,
			ff.u.periodic.phase,
			ff.u.periodic.envelope.attack_length,
			ff.u.periodic.envelope.attack_level,
			ff.u.periodic.envelope.fade_length,
			ff.u.periodic.envelope.fade_level);
	}

	return 0;

ffm_eff_error1:
	g_hash_table_destroy(ffm.effects);
	return -1;
}

static int ffm_play(NRequest* request, int play)
{
	const NProplist *props = n_request_get_properties (request);
	gint16 *id;
	const gchar *key;

	/* Check whether we are supposed to even play anything */
	key = n_proplist_get_string(props, FFM_EFFECT_KEY);
	if (!key) {
		N_DEBUG (LOG_CAT "got NULL key");
		return FALSE;
	}

	id = g_hash_table_lookup(ffm.effects, key);
	/* Fall back to default effect, if the key did not match our effects */
	if (id == NULL)
		id = g_hash_table_lookup(ffm.effects, FFM_DEFAULT_EFFECT);

	N_DEBUG (LOG_CAT "starting to play effect %s with id:%d", key, *id);
	if (ffmemless_play(*id, ffm.dev_file, play))
		return FALSE;

	N_DEBUG (LOG_CAT "Succesfully started playback");
	return TRUE;
}

static int ffm_sink_initialize(NSinkInterface *iface)
{
	(void) iface;
	(void) ffm;

	return TRUE;
}
static void ffm_sink_shutdown(NSinkInterface *iface)
{
	(void) iface;

}

static int ffm_sink_can_handle(NSinkInterface *iface, NRequest *request)
{
	const NProplist *props = n_request_get_properties (request);
	(void) iface;
	NCore    *core    = n_sink_interface_get_core     (iface);
	NContext *context = n_core_get_context            (core);
	NValue   *enabled = NULL;

	N_DEBUG (LOG_CAT "can handle %s?", n_request_get_name(request));

	enabled = (NValue*) n_context_get_value (context,
		"profile.current.vibrating.alert.enabled");

	if (!enabled || !n_value_get_bool (enabled)) {
		N_DEBUG (LOG_CAT "no, vibration not enabled in profile");
		return FALSE;
	}

	if (n_proplist_has_key (props, FFM_EFFECT_KEY)) {
		N_DEBUG (LOG_CAT "yes");
		return TRUE;
	}
	N_DEBUG (LOG_CAT "no, missing effect for this event");
	return FALSE;
}
static int ffm_sink_prepare(NSinkInterface *iface, NRequest *request)
{
	(void) iface;
	(void) request;

	n_sink_interface_synchronize (iface, request);
	N_DEBUG (LOG_CAT "prepare");
	return TRUE;
}
static int ffm_sink_play(NSinkInterface *iface, NRequest *request)
{
	(void) iface;
	N_DEBUG (LOG_CAT "play");

	return ffm_play(request, 1);
}
static int ffm_sink_pause(NSinkInterface *iface, NRequest *request)
{
	(void) iface;
	N_DEBUG (LOG_CAT "pause");

	/* no pause possible for vibra effects, just stop */
	return ffm_play(request, 0);
}
static void ffm_sink_stop(NSinkInterface *iface, NRequest *request)
{
	(void) iface;
	N_DEBUG (LOG_CAT "stop");

	ffm_play(request, 0);
}

N_PLUGIN_LOAD(plugin)
{
	const NProplist *props = n_plugin_get_params(plugin);

	N_DEBUG (LOG_CAT "plugin load");

	static const NSinkInterfaceDecl decl = {
		.name       = "ffmemless",
		.initialize = ffm_sink_initialize,
		.shutdown   = ffm_sink_shutdown,
		.can_handle = ffm_sink_can_handle,
		.prepare    = ffm_sink_prepare,
		.play       = ffm_sink_play,
		.pause      = ffm_sink_pause,
		.stop       = ffm_sink_stop
	};

	ffm.ngfd_props = props;
	ffm.sys_props = ffm_read_props(n_proplist_get_string(props,
						FFM_SYSTEM_CONFIG_FILE));

	n_proplist_dump(ffm.ngfd_props);
	if (ffm.sys_props)
		n_proplist_dump(ffm.sys_props);

	if (ffm_setup_device(ffm.ngfd_props, &ffm.dev_file)) {
		N_ERROR (LOG_CAT "Could not find a device file");
		goto ffm_load_error1;
	}

	ffm.effects = ffm_new_effect_list(n_proplist_get_string(ffm.ngfd_props,
							FFM_EFFECTLIST_KEY));

	if (ffm_setup_default_effect(ffm.effects, ffm.dev_file)) {
		N_ERROR (LOG_CAT "Could not load default fall-back effect");
		goto ffm_load_error2;
	}
	/* Setup effects defined in ngfd internal ini file, this must pass. */
	if (ffm_setup_effects(ffm.ngfd_props, ffm.effects)) {
		N_ERROR (LOG_CAT "Could not load ngfd effects");
		goto ffm_load_error2;
	}
	/*
	 * Setup effects defined in system level ini file, this is ok to fail.
	 * If there are effects with same name, they get overwritten.
	 */
	if (ffm_setup_effects(ffm.sys_props, ffm.effects)) {
		N_DEBUG (LOG_CAT "No system level effect settings");
	}
	n_plugin_register_sink (plugin, &decl);
	return TRUE;

ffm_load_error2:
	g_hash_table_destroy(ffm.effects);
	ffm_close_device(ffm.dev_file);
ffm_load_error1:
	n_proplist_free(ffm.sys_props);
	return FALSE;
}

N_PLUGIN_UNLOAD(plugin)
{
	(void) plugin;
	N_DEBUG (LOG_CAT "plugin unload");

	g_hash_table_destroy(ffm.effects);
	ffm_close_device(ffm.dev_file);
	n_proplist_free(ffm.sys_props);
}

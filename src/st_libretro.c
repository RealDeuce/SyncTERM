/* Libretro ABI and frontend-owned application lifetime. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "libretro.h"
#include "st_application.h"
#include "st_libretro_input.h"
#include "st_threads.h"
#include "st_time.h"
#include "string_compat.h"
#include "path.h"
#include "filesystem.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static retro_environment_t environment;
static retro_video_refresh_t video;
static retro_audio_sample_t sample;
static retro_audio_sample_batch_t batch;
static retro_input_poll_t input_poll;
static retro_input_state_t input_state;

/* The frontend owns lifecycle calls and callbacks. The application worker
 * owns Wren, settings, connections and streams. mutex protects borrowed
 * display/audio handles, stopping/finished and startup publication. release
 * revokes handles under that lock before the worker destroys their owners.
 * No frontend callback is invoked while holding mutex or from the worker. */
static struct {
	st_mutex_t mutex;
	st_condition_t changed;
	st_thread_t worker;
	bool initialized, started, ready, stopping, finished, shutdown_sent;
	st_console_display_t *display;
	st_audio_runtime_t *audio;
	char *directory, *content;
	bool keyboard_callback;
	bool keys[RETROK_LAST];
	uint16_t modifiers;
	uint32_t *pixels;
	size_t pixel_capacity;
	uint32_t width, height;
	int32_t mouse_x, mouse_y;
	uint32_t mouse_buttons;
	int16_t pcm[735 * AUDIO_CHANNELS];
	size_t pcm_position, pcm_frames;
} core;

static bool
stopping(void *context)
{
	bool result;
	(void)context;
	(void)st_mutex_lock(&core.mutex);
	result = core.stopping;
	(void)st_mutex_unlock(&core.mutex);
	return result;
}

static void
ready(void *context, st_console_display_t *display, st_audio_runtime_t *audio)
{
	(void)context;
	(void)st_mutex_lock(&core.mutex);
	core.display = display;
	core.audio = audio;
	core.ready = true;
	(void)st_condition_broadcast(&core.changed);
	(void)st_mutex_unlock(&core.mutex);
}

static void
release(void *context)
{
	(void)context;
	(void)st_mutex_lock(&core.mutex);
	core.display = NULL;
	core.audio = NULL;
	(void)st_mutex_unlock(&core.mutex);
}

static int
application_worker(void *context)
{
	const struct st_application_host host = {
		.libretro = true, .ready = ready, .release = release, .stopping = stopping,
		.directory = core.directory,
	};
	char program[] = "syncterm";
	char content_option[] = "-b";
	char *argv[6] = {program};
	int argc = 1, result;
	(void)context;
	if (core.content != NULL) {
		argv[argc++] = content_option;
		argv[argc++] = core.content;
	}
	argv[argc] = NULL;
	result = st_application_run(argc, argv, &host);
	(void)st_mutex_lock(&core.mutex);
	core.finished = true;
	(void)st_condition_broadcast(&core.changed);
	(void)st_mutex_unlock(&core.mutex);
	return result;
}

static void
stop_worker(void)
{
	const struct st_console_event close = {.type = ST_CONSOLE_EVENT_CLOSE};
	if (!core.started) return;
	(void)st_mutex_lock(&core.mutex);
	core.stopping = true;
	while (!core.finished) {
		struct timespec deadline;
		/* Reissue across modal/console switches and cancel synchronous audio
		 * drains before waiting for the application to finish its cleanup. */
		if (core.audio != NULL) st_audio_runtime_stop(core.audio);
		if (core.display != NULL) (void)st_console_display_post_event(core.display, &close);
		if (st_time_utc_deadline_after_ms(&deadline, 20) == 0)
			(void)st_condition_timedwait(&core.changed, &core.mutex, &deadline);
		else
			(void)st_condition_wait(&core.changed, &core.mutex);
	}
	(void)st_mutex_unlock(&core.mutex);
	(void)st_thread_join(core.worker, NULL);
	core.started = false;
	core.ready = false;
	core.pcm_frames = core.pcm_position = 0;
}

static bool
start_worker(void)
{
	core.stopping = core.finished = core.shutdown_sent = core.ready = false;
	core.mouse_x = core.mouse_y = 0;
	core.mouse_buttons = core.modifiers = 0;
	memset(core.keys, 0, sizeof(core.keys));
	if (st_thread_create(&core.worker, application_worker, NULL) != ST_THREAD_SUCCESS)
		return false;
	core.started = true;
	(void)st_mutex_lock(&core.mutex);
	while (!core.ready && !core.finished)
		(void)st_condition_wait(&core.changed, &core.mutex);
	bool success = core.ready && !core.finished;
	(void)st_mutex_unlock(&core.mutex);
	if (!success) stop_worker();
	return success;
}

static void RETRO_CALLCONV
keyboard(bool down, unsigned key, uint32_t character, uint16_t modifiers)
{
	struct st_console_event events[2];
	size_t count;
	if (!core.initialized || key >= RETROK_LAST) return;
	count = st_libretro_key_events(events, down, key, character, modifiers, core.keys[key]);
	core.keys[key] = down;
	core.modifiers = modifiers;
	(void)st_mutex_lock(&core.mutex);
	if (core.display != NULL && !core.stopping)
		for (size_t i = 0; i < count; ++i)
			(void)st_console_display_post_event(core.display, &events[i]);
	(void)st_mutex_unlock(&core.mutex);
}

static void
poll_keyboard(void)
{
	uint16_t modifiers = (uint16_t)(core.modifiers & (RETROKMOD_CAPSLOCK | RETROKMOD_NUMLOCK));
	const unsigned mods[] = {RETROK_LSHIFT, RETROK_RSHIFT, RETROK_LCTRL,
	    RETROK_RCTRL, RETROK_LALT, RETROK_RALT, RETROK_LMETA, RETROK_RMETA};
	const uint16_t bits[] = {RETROKMOD_SHIFT, RETROKMOD_SHIFT, RETROKMOD_CTRL,
	    RETROKMOD_CTRL, RETROKMOD_ALT, RETROKMOD_ALT, RETROKMOD_META, RETROKMOD_META};
	for (size_t i = 0; i < sizeof(mods) / sizeof(mods[0]); ++i)
		if (input_state(0, RETRO_DEVICE_KEYBOARD, 0, mods[i])) modifiers |= bits[i];
	for (unsigned key = 0; key < RETROK_LAST; ++key) {
		bool down = input_state(0, RETRO_DEVICE_KEYBOARD, 0, key) != 0;
		if (down == core.keys[key]) continue;
		if (down && key == RETROK_CAPSLOCK) modifiers ^= RETROKMOD_CAPSLOCK;
		if (down && key == RETROK_NUMLOCK) modifiers ^= RETROKMOD_NUMLOCK;
		keyboard(down, key, 0, modifiers);
	}
}

static void
poll_mouse(void)
{
	struct st_console_event event = {.type = ST_CONSOLE_EVENT_MOUSE};
	int64_t x = (int64_t)core.mouse_x + input_state(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X);
	int64_t y = (int64_t)core.mouse_y + input_state(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);
	uint32_t buttons = 0;
	int wheel = (input_state(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELUP) != 0)
	    - (input_state(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELDOWN) != 0);
	int wheel_x = (input_state(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_HORIZ_WHEELUP) != 0)
	    - (input_state(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_HORIZ_WHEELDOWN) != 0);
	if (input_state(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_PRESSED)) {
		x = (int32_t)(((int64_t)input_state(0, RETRO_DEVICE_POINTER, 0,
		    RETRO_DEVICE_ID_POINTER_X) + 32767) * core.width / 65534);
		y = (int32_t)(((int64_t)input_state(0, RETRO_DEVICE_POINTER, 0,
		    RETRO_DEVICE_ID_POINTER_Y) + 32767) * core.height / 65534);
		buttons |= ST_CONSOLE_MOUSE_BUTTON_LEFT;
	}
	if (input_state(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT)) buttons |= ST_CONSOLE_MOUSE_BUTTON_LEFT;
	if (input_state(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT)) buttons |= ST_CONSOLE_MOUSE_BUTTON_RIGHT;
	if (input_state(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_MIDDLE)) buttons |= ST_CONSOLE_MOUSE_BUTTON_MIDDLE;
	if (input_state(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_BUTTON_4)) buttons |= ST_CONSOLE_MOUSE_BUTTON_X1;
	if (input_state(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_BUTTON_5)) buttons |= ST_CONSOLE_MOUSE_BUTTON_X2;
	x = x < 0 ? 0 : (uint64_t)x >= core.width ? (int64_t)core.width - 1 : x;
	y = y < 0 ? 0 : (uint64_t)y >= core.height ? (int64_t)core.height - 1 : y;
	if (x == core.mouse_x && y == core.mouse_y && buttons == core.mouse_buttons && wheel == 0 && wheel_x == 0) return;
	event.data.mouse.buttons = buttons;
	event.data.mouse.wheel_y = wheel;
	event.data.mouse.wheel_x = wheel_x;
	if (core.modifiers & RETROKMOD_SHIFT) event.data.mouse.modifiers |= ST_CONSOLE_MODIFIER_SHIFT;
	if (core.modifiers & RETROKMOD_CTRL) event.data.mouse.modifiers |= ST_CONSOLE_MODIFIER_CONTROL;
	if (core.modifiers & RETROKMOD_ALT) event.data.mouse.modifiers |= ST_CONSOLE_MODIFIER_ALT;
	(void)st_mutex_lock(&core.mutex);
	if (core.display != NULL && !core.stopping
	    && st_console_display_libretro_map_pointer(core.display, (int32_t)x, (int32_t)y, &event) == 0) {
		if (x != core.mouse_x || y != core.mouse_y) {
			event.data.mouse.action = ST_CONSOLE_MOUSE_MOVE;
			(void)st_console_display_post_event(core.display, &event);
		}
		if (buttons != core.mouse_buttons) {
			event.data.mouse.action = ST_CONSOLE_MOUSE_BUTTON;
			(void)st_console_display_post_event(core.display, &event);
		}
		if (wheel != 0 || wheel_x != 0) {
			event.data.mouse.action = ST_CONSOLE_MOUSE_WHEEL;
			(void)st_console_display_post_event(core.display, &event);
		}
	}
	(void)st_mutex_unlock(&core.mutex);
	core.mouse_x = (int32_t)x; core.mouse_y = (int32_t)y; core.mouse_buttons = buttons;
}

RETRO_API void retro_set_environment(retro_environment_t cb)
{
	bool no_game = true;
	environment = cb;
	if (cb != NULL) (void)cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_game);
}
RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb) { video = cb; }
RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb) { sample = cb; }
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { batch = cb; }
RETRO_API void retro_set_input_poll(retro_input_poll_t cb) { input_poll = cb; }
RETRO_API void retro_set_input_state(retro_input_state_t cb) { input_state = cb; }
RETRO_API unsigned retro_api_version(void) { return RETRO_API_VERSION; }

RETRO_API void
retro_init(void)
{
	if (core.initialized) return;
	if (st_mutex_init(&core.mutex, ST_MUTEX_PLAIN) != ST_THREAD_SUCCESS) return;
	if (st_condition_init(&core.changed) != ST_THREAD_SUCCESS) {
		st_mutex_destroy(&core.mutex);
		return;
	}
	core.width = 640; core.height = 400;
	core.initialized = true;
}

RETRO_API void
retro_get_system_info(struct retro_system_info *info)
{
	*info = (struct retro_system_info){
		.library_name = "SyncTERM", .library_version = "2.0a",
		.valid_extensions = "lst", .need_fullpath = true,
	};
}

RETRO_API void
retro_get_system_av_info(struct retro_system_av_info *info)
{
	*info = (struct retro_system_av_info){
		.geometry = {core.width, core.height, core.width, core.height, 0.0f},
		.timing = {60.0, AUDIO_SAMPLE_RATE},
	};
}

RETRO_API bool
retro_load_game(const struct retro_game_info *info)
{
	enum retro_pixel_format format = RETRO_PIXEL_FORMAT_XRGB8888;
	struct retro_keyboard_callback callback = {.callback = keyboard};
	const char *directory = NULL;
	if (!core.initialized || environment == NULL) return false;
	retro_unload_game();
	if (!environment(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &format)) return false;
	core.keyboard_callback = environment(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, &callback);
	if (environment(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &directory) && directory != NULL
	    && directory[0] != '\0') {
		if (path_join_alloc(&core.directory, directory, "syncterm") != 0
		    || fs_create_directories(core.directory) != 0) goto failed;
	}
	if (info != NULL) {
		if (info->path == NULL || info->path[0] == '\0') goto failed;
		core.content = strdup(info->path);
		if (core.content == NULL) goto failed;
	}
	if (start_worker()) return true;
failed:
	retro_unload_game();
	return false;
}

RETRO_API void
retro_run(void)
{
	uint32_t width = 0, height = 0;
	bool frame = false, geometry = false, shutdown = false;
	if (!core.initialized || !core.started) return;
	if (input_poll != NULL) input_poll();
	if (!core.keyboard_callback && input_state != NULL) poll_keyboard();
	if (input_state != NULL) poll_mouse();
	(void)st_mutex_lock(&core.mutex);
	if (core.display != NULL && st_console_display_libretro_frame_size(core.display, &width, &height) == 0
	    && width != 0 && height != 0 && width <= INT32_MAX && height <= INT32_MAX
	    && (size_t)width <= SIZE_MAX / height / sizeof(*core.pixels)) {
		size_t count = (size_t)width * height;
		if (count > core.pixel_capacity) {
			uint32_t *replacement = realloc(core.pixels, count * sizeof(*replacement));
			if (replacement != NULL) { core.pixels = replacement; core.pixel_capacity = count; }
		}
		if (count <= core.pixel_capacity)
			frame = st_console_display_libretro_frame_copy(core.display, core.pixels, width, height) == 0;
		if (frame) {
			geometry = width != core.width || height != core.height;
			core.width = width; core.height = height;
		}
	}
	if (core.pcm_frames == 0) {
		if (core.audio == NULL || st_audio_runtime_pull(core.audio, core.pcm, 735) != 0)
			memset(core.pcm, 0, sizeof(core.pcm));
		core.pcm_frames = 735; core.pcm_position = 0;
	}
	if (core.finished && !core.shutdown_sent) { core.shutdown_sent = true; shutdown = true; }
	(void)st_mutex_unlock(&core.mutex);
	if (geometry && environment != NULL) {
		struct retro_system_av_info info;
		retro_get_system_av_info(&info);
		(void)environment(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &info);
	}
	if (frame && video != NULL) video(core.pixels, width, height, (size_t)width * sizeof(*core.pixels));
	if (batch != NULL) {
		size_t accepted = batch(core.pcm + core.pcm_position * AUDIO_CHANNELS, core.pcm_frames);
		if (accepted > core.pcm_frames) accepted = core.pcm_frames;
		core.pcm_position += accepted; core.pcm_frames -= accepted;
	} else {
		if (sample != NULL)
			for (size_t i = 0; i < core.pcm_frames; ++i) {
				size_t offset = (core.pcm_position + i) * AUDIO_CHANNELS;
				sample(core.pcm[offset], core.pcm[offset + 1]);
			}
		core.pcm_frames = 0;
	}
	if (shutdown && environment != NULL) (void)environment(RETRO_ENVIRONMENT_SHUTDOWN, NULL);
}

RETRO_API void
retro_unload_game(void)
{
	if (!core.initialized) return;
	stop_worker();
	free(core.directory); core.directory = NULL;
	free(core.content); core.content = NULL;
	free(core.pixels); core.pixels = NULL; core.pixel_capacity = 0;
}

RETRO_API void
retro_deinit(void)
{
	if (!core.initialized) return;
	retro_unload_game();
	st_condition_destroy(&core.changed);
	st_mutex_destroy(&core.mutex);
	core.initialized = false;
}

RETRO_API void retro_reset(void)
{
	if (core.initialized && core.started) {
		stop_worker();
		if (!start_worker() && environment != NULL)
			(void)environment(RETRO_ENVIRONMENT_SHUTDOWN, NULL);
	}
}
RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device) { (void)port; (void)device; }
RETRO_API size_t retro_serialize_size(void) { return 0; }
RETRO_API bool retro_serialize(void *data, size_t size) { (void)data; (void)size; return false; }
RETRO_API bool retro_unserialize(const void *data, size_t size) { (void)data; (void)size; return false; }
RETRO_API void retro_cheat_reset(void) {}
RETRO_API void retro_cheat_set(unsigned index, bool enabled, const char *code) { (void)index; (void)enabled; (void)code; }
RETRO_API bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t count) { (void)type; (void)info; (void)count; return false; }
RETRO_API unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }
RETRO_API void *retro_get_memory_data(unsigned id) { (void)id; return NULL; }
RETRO_API size_t retro_get_memory_size(unsigned id) { (void)id; return 0; }

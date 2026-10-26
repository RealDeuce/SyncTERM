/* Load the produced core through its public ABI, as a frontend would. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#define RETRO_IMPORT_SYMBOLS
#include "libretro.h"
#include "syncterm_config.h"
#include "filesystem.h"
#include "st_threads.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if SYNCTERM_PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "st_windows_utf.h"
#define CORE_SYMBOL FARPROC
#define FIND_SYMBOL(module, name) GetProcAddress(module, name)
#else
#include <dlfcn.h>
#define CORE_SYMBOL void *
#define FIND_SYMBOL(module, name) dlsym(module, name)
#endif

#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); return 1; } } while (0)
#define LOAD(name) do { \
	CORE_SYMBOL symbol = FIND_SYMBOL(module, #name); \
	CHECK(symbol != NULL); \
	_Static_assert(sizeof(symbol) == sizeof(api.name), "native function pointer ABI"); \
	memcpy(&api.name, &symbol, sizeof(api.name)); \
} while (0)

static struct {
	unsigned (*retro_api_version)(void);
	void (*retro_set_environment)(retro_environment_t);
	void (*retro_set_video_refresh)(retro_video_refresh_t);
	void (*retro_set_audio_sample)(retro_audio_sample_t);
	void (*retro_set_audio_sample_batch)(retro_audio_sample_batch_t);
	void (*retro_set_input_poll)(retro_input_poll_t);
	void (*retro_set_input_state)(retro_input_state_t);
	void (*retro_get_system_info)(struct retro_system_info *);
	void (*retro_get_system_av_info)(struct retro_system_av_info *);
	void (*retro_init)(void);
	void (*retro_deinit)(void);
	bool (*retro_load_game)(const struct retro_game_info *);
	void (*retro_unload_game)(void);
	void (*retro_reset)(void);
	void (*retro_run)(void);
} api;

static struct retro_keyboard_callback keyboard;
static st_thread_t frontend;
static bool callback_thread_error, accept_pixels = true, accept_keyboard = true;
static bool support_no_game, shutdown_requested;
static bool menu_visible;
static size_t video_frames, audio_frames, audio_batches, polls;
static uint64_t last_hash;
static bool press_escape;

static bool RETRO_CALLCONV
environment(unsigned command, void *data)
{
	if (!st_thread_equal(st_thread_current(), frontend)) callback_thread_error = true;
	switch (command) {
	case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
		support_no_game = *(bool *)data; return true;
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
		return accept_pixels && *(enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_XRGB8888;
	case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
		keyboard = *(struct retro_keyboard_callback *)data; return accept_keyboard;
	case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
		*(const char **)data = ST_LIBRETRO_TEST_DIRECTORY; return true;
	case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
		return ((const struct retro_system_av_info *)data)->geometry.base_width > 0;
	case RETRO_ENVIRONMENT_SHUTDOWN:
		shutdown_requested = true; return true;
	default: return false;
	}
}

static void RETRO_CALLCONV
video(const void *data, unsigned width, unsigned height, size_t pitch)
{
	const uint32_t *pixels = data;
	uint64_t hash = 0;
	size_t ink = 0;
	if (!st_thread_equal(st_thread_current(), frontend)) callback_thread_error = true;
	if (pixels == NULL || width == 0 || height == 0 || pitch != width * sizeof(*pixels)) {
		callback_thread_error = true; return;
	}
	for (size_t i = 0; i < (size_t)width * height; ++i) {
		hash = hash * 31 + pixels[i];
		if ((pixels[i] & UINT32_C(0xffffff)) != 0) ++ink;
	}
	/* Font setup can publish a blank screen with just a cursor. Input
	 * assertions need the menu, not a fixed number of startup frames. */
	if (ink > width) menu_visible = true;
	last_hash = hash;
	++video_frames;
}

static size_t RETRO_CALLCONV
audio(const int16_t *data, size_t count)
{
	if (!st_thread_equal(st_thread_current(), frontend) || data == NULL || count == 0)
		callback_thread_error = true;
	/* Force a short frontend write once; the next run must retain its tail. */
	size_t accepted = audio_batches++ == 0 ? count / 2 : count;
	audio_frames += accepted;
	return accepted;
}

static void RETRO_CALLCONV
sample(int16_t left, int16_t right)
{
	(void)left; (void)right;
	if (!st_thread_equal(st_thread_current(), frontend)) callback_thread_error = true;
	++audio_frames;
}

static void RETRO_CALLCONV
poll_input(void)
{
	++polls;
	if (press_escape && accept_keyboard && keyboard.callback != NULL) {
		keyboard.callback(true, RETROK_ESCAPE, 27, 0);
		keyboard.callback(false, RETROK_ESCAPE, 0, 0);
	}
}

static int16_t RETRO_CALLCONV
input(unsigned port, unsigned device, unsigned index, unsigned id)
{
	(void)port; (void)index;
	return press_escape && (polls & 1) != 0
	    && device == RETRO_DEVICE_KEYBOARD && id == RETROK_ESCAPE ? 1 : 0;
}

static void
run_frames(unsigned count)
{
	for (unsigned i = 0; i < count; ++i) {
		api.retro_run();
		(void)st_thread_sleep(&(struct timespec){0, 5000000}, NULL);
	}
}

static bool
wait_for_menu(void)
{
	for (unsigned i = 0; i < 1000 && !menu_visible && !shutdown_requested; ++i)
		run_frames(1);
	return menu_visible;
}

static bool
exit_application(void)
{
	/* Repeated key presses also cover input flushing during startup; the
	 * meaningful result is the application's frontend shutdown request. */
	press_escape = true;
	for (unsigned i = 0; i < 1000 && !shutdown_requested; ++i) run_frames(1);
	press_escape = false;
	return shutdown_requested;
}

int
main(int argc, char **argv)
{
	CHECK(argc == 2);
#if SYNCTERM_PLATFORM_WINDOWS
	wchar_t *wide = NULL;
	CHECK(st_windows_utf8_to_utf16(&wide, argv[1]) == 0);
	HMODULE module = LoadLibraryW(wide);
	free(wide);
#else
	void *module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
	if (module == NULL) fprintf(stderr, "%s\n", dlerror());
#endif
	CHECK(module != NULL);
	LOAD(retro_api_version); LOAD(retro_set_environment); LOAD(retro_set_video_refresh);
	LOAD(retro_set_audio_sample); LOAD(retro_set_audio_sample_batch); LOAD(retro_set_input_poll);
	LOAD(retro_set_input_state); LOAD(retro_get_system_info); LOAD(retro_get_system_av_info);
	LOAD(retro_init); LOAD(retro_deinit); LOAD(retro_load_game); LOAD(retro_unload_game);
	LOAD(retro_reset); LOAD(retro_run);
	const char *required[] = {"retro_serialize_size", "retro_serialize", "retro_unserialize",
	    "retro_cheat_reset", "retro_cheat_set", "retro_load_game_special", "retro_get_region",
	    "retro_get_memory_data", "retro_get_memory_size", "retro_set_controller_port_device"};
	for (size_t i = 0; i < sizeof(required) / sizeof(required[0]); ++i)
		CHECK(FIND_SYMBOL(module, required[i]) != NULL);
#if SYNCTERM_PLATFORM_WINDOWS
	CHECK(FIND_SYMBOL(module, "st_application_main") != NULL);
#endif
	CHECK(FIND_SYMBOL(module, "st_console_create") == NULL);
	CHECK(api.retro_api_version() == RETRO_API_VERSION);
	struct retro_system_info info;
	api.retro_get_system_info(&info);
	CHECK(strcmp(info.library_name, "SyncTERM") == 0 && info.need_fullpath);
	CHECK(fs_create_directories(ST_LIBRETRO_TEST_DIRECTORY "/syncterm") == 0);
	FILE *file = fopen(ST_LIBRETRO_TEST_DIRECTORY "/syncterm/syncterm.ini", "wb");
	CHECK(file != NULL);
	CHECK(fputs("[SyncTERM]\nConfirmClose=false\nOutputMode=4\n[WebLists]\n", file) >= 0);
	CHECK(fclose(file) == 0);
	file = fopen(ST_LIBRETRO_TEST_DIRECTORY "/test.lst", "wb");
	CHECK(file != NULL);
	CHECK(fputs("[First]\nConnectionType=Raw\nAddress=127.0.0.1\nPort=1\n"
	    "[Second]\nConnectionType=Raw\nAddress=127.0.0.1\nPort=1\n", file) >= 0);
	CHECK(fclose(file) == 0);
	frontend = st_thread_current();
	api.retro_set_environment(environment);
	CHECK(support_no_game);
	api.retro_set_video_refresh(video);
	api.retro_set_audio_sample_batch(audio);
	api.retro_set_input_poll(poll_input);
	api.retro_set_input_state(input);
	api.retro_init();
	accept_pixels = false;
	CHECK(!api.retro_load_game(NULL));
	accept_pixels = true;
	CHECK(!api.retro_load_game(&(struct retro_game_info){0}));
	struct retro_game_info game = {.path = ST_LIBRETRO_TEST_DIRECTORY "/test.lst"};
	CHECK(api.retro_load_game(&game));
	CHECK(wait_for_menu());
	run_frames(3);
	CHECK(video_frames > 0 && last_hash != 0 && polls > 0);
	CHECK(audio_frames > 0 && audio_batches > 1);
	struct retro_system_av_info av;
	api.retro_get_system_av_info(&av);
	CHECK(av.geometry.base_width != 0 && av.geometry.base_height != 0);
	CHECK(av.timing.fps == 60.0 && av.timing.sample_rate == 44100.0);
	api.retro_reset();
	CHECK(exit_application());
	api.retro_unload_game();
	/* Immediate unload races application setup. It must join without waiting
	 * for another retro_run(), including while the menu is taking ownership. */
	for (unsigned i = 0; i < 3; ++i) {
		CHECK(api.retro_load_game(NULL));
		api.retro_unload_game();
	}
	accept_keyboard = false;
	api.retro_set_audio_sample_batch(NULL);
	api.retro_set_audio_sample(sample);
	menu_visible = false;
	shutdown_requested = false;
	CHECK(api.retro_load_game(&game));
	CHECK(wait_for_menu());
	CHECK(exit_application());
	api.retro_unload_game();
	api.retro_deinit();
	api.retro_init();
	accept_keyboard = true;
	menu_visible = false;
	shutdown_requested = false;
	CHECK(api.retro_load_game(NULL));
	api.retro_deinit();
	CHECK(!callback_thread_error);
#if SYNCTERM_PLATFORM_WINDOWS
	CHECK(FreeLibrary(module));
#else
	CHECK(dlclose(module) == 0);
#endif
	return 0;
}

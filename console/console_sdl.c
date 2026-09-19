/* Runtime-loaded SDL2 bitmap presenter with a real-main-thread dispatcher. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "console_internal.h"

#include "console_config.h"
#include "dynamic_library.h"
#include "io_error.h"
#include "st_time.h"
#include "st_utf8.h"

#include <SDL.h>
#include <limits.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef void (SDLCALL *sdl_set_main_ready_fn)(void);
typedef int (SDLCALL *sdl_init_subsystem_fn)(Uint32);
typedef void (SDLCALL *sdl_quit_subsystem_fn)(Uint32);
typedef SDL_Window *(SDLCALL *sdl_create_window_fn)(const char *, int, int,
	int, int, Uint32);
typedef void (SDLCALL *sdl_destroy_window_fn)(SDL_Window *);
typedef Uint32 (SDLCALL *sdl_get_window_id_fn)(SDL_Window *);
typedef void (SDLCALL *sdl_get_window_size_fn)(SDL_Window *, int *, int *);
typedef int (SDLCALL *sdl_get_window_display_index_fn)(SDL_Window *);
typedef int (SDLCALL *sdl_get_display_bounds_fn)(int, SDL_Rect *);
typedef int (SDLCALL *sdl_get_display_usable_bounds_fn)(int, SDL_Rect *);
typedef int (SDLCALL *sdl_get_window_borders_size_fn)(SDL_Window *, int *,
	int *, int *, int *);
typedef void (SDLCALL *sdl_set_window_position_fn)(SDL_Window *, int, int);
typedef void (SDLCALL *sdl_set_window_title_fn)(SDL_Window *, const char *);
typedef void (SDLCALL *sdl_set_window_size_fn)(SDL_Window *, int, int);
typedef void (SDLCALL *sdl_set_window_minimum_size_fn)(SDL_Window *, int,
	int);
typedef void (SDLCALL *sdl_restore_window_fn)(SDL_Window *);
typedef int (SDLCALL *sdl_set_window_fullscreen_fn)(SDL_Window *, Uint32);
typedef SDL_Surface *(SDLCALL *sdl_create_rgb_surface_from_fn)(void *, int,
	int, int, int, Uint32, Uint32, Uint32, Uint32);
typedef void (SDLCALL *sdl_free_surface_fn)(SDL_Surface *);
typedef void (SDLCALL *sdl_set_window_icon_fn)(SDL_Window *, SDL_Surface *);
typedef SDL_bool (SDLCALL *sdl_set_hint_fn)(const char *, const char *);
typedef SDL_Renderer *(SDLCALL *sdl_create_renderer_fn)(SDL_Window *, int,
	Uint32);
typedef void (SDLCALL *sdl_destroy_renderer_fn)(SDL_Renderer *);
typedef int (SDLCALL *sdl_get_renderer_output_size_fn)(SDL_Renderer *, int *,
	int *);
typedef int (SDLCALL *sdl_set_render_draw_color_fn)(SDL_Renderer *, Uint8,
	Uint8, Uint8, Uint8);
typedef int (SDLCALL *sdl_render_clear_fn)(SDL_Renderer *);
typedef int (SDLCALL *sdl_render_copy_fn)(SDL_Renderer *, SDL_Texture *,
	const SDL_Rect *, const SDL_Rect *);
typedef void (SDLCALL *sdl_render_present_fn)(SDL_Renderer *);
typedef SDL_Texture *(SDLCALL *sdl_create_texture_fn)(SDL_Renderer *, Uint32,
	int, int, int);
typedef void (SDLCALL *sdl_destroy_texture_fn)(SDL_Texture *);
typedef int (SDLCALL *sdl_lock_texture_fn)(SDL_Texture *, const SDL_Rect *,
	void **, int *);
typedef void (SDLCALL *sdl_unlock_texture_fn)(SDL_Texture *);
typedef int (SDLCALL *sdl_poll_event_fn)(SDL_Event *);
typedef Uint32 (SDLCALL *sdl_get_ticks_fn)(void);
typedef int (SDLCALL *sdl_show_cursor_fn)(int);
typedef void (SDLCALL *sdl_start_text_input_fn)(void);
typedef void (SDLCALL *sdl_stop_text_input_fn)(void);
typedef int (SDLCALL *sdl_set_clipboard_text_fn)(const char *);
typedef char *(SDLCALL *sdl_get_clipboard_text_fn)(void);
typedef void (SDLCALL *sdl_free_memory_fn)(void *);
typedef int (SDLCALL *sdl_open_url_fn)(const char *);

struct sdl_api {
	void (SDLCALL *set_main_ready)(void);
	int (SDLCALL *init_subsystem)(Uint32);
	void (SDLCALL *quit_subsystem)(Uint32);
	SDL_Window *(SDLCALL *create_window)(const char *, int, int, int, int,
	    Uint32);
	void (SDLCALL *destroy_window)(SDL_Window *);
	Uint32 (SDLCALL *get_window_id)(SDL_Window *);
	void (SDLCALL *get_window_size)(SDL_Window *, int *, int *);
	int (SDLCALL *get_window_display_index)(SDL_Window *);
	int (SDLCALL *get_display_bounds)(int, SDL_Rect *);
	int (SDLCALL *get_display_usable_bounds)(int, SDL_Rect *);
	int (SDLCALL *get_window_borders_size)(SDL_Window *, int *, int *,
	    int *, int *);
	void (SDLCALL *set_window_position)(SDL_Window *, int, int);
	void (SDLCALL *set_window_title)(SDL_Window *, const char *);
	void (SDLCALL *set_window_size)(SDL_Window *, int, int);
	void (SDLCALL *set_window_minimum_size)(SDL_Window *, int, int);
	void (SDLCALL *restore_window)(SDL_Window *);
	int (SDLCALL *set_window_fullscreen)(SDL_Window *, Uint32);
	SDL_Surface *(SDLCALL *create_rgb_surface_from)(void *, int, int, int,
	    int, Uint32, Uint32, Uint32, Uint32);
	void (SDLCALL *free_surface)(SDL_Surface *);
	void (SDLCALL *set_window_icon)(SDL_Window *, SDL_Surface *);
	SDL_bool (SDLCALL *set_hint)(const char *, const char *);
	SDL_Renderer *(SDLCALL *create_renderer)(SDL_Window *, int, Uint32);
	void (SDLCALL *destroy_renderer)(SDL_Renderer *);
	int (SDLCALL *get_renderer_output_size)(SDL_Renderer *, int *, int *);
	int (SDLCALL *set_render_draw_color)(SDL_Renderer *, Uint8, Uint8,
	    Uint8, Uint8);
	int (SDLCALL *render_clear)(SDL_Renderer *);
	int (SDLCALL *render_copy)(SDL_Renderer *, SDL_Texture *,
	    const SDL_Rect *, const SDL_Rect *);
	void (SDLCALL *render_present)(SDL_Renderer *);
	SDL_Texture *(SDLCALL *create_texture)(SDL_Renderer *, Uint32, int,
	    int, int);
	void (SDLCALL *destroy_texture)(SDL_Texture *);
	int (SDLCALL *lock_texture)(SDL_Texture *, const SDL_Rect *, void **,
	    int *);
	void (SDLCALL *unlock_texture)(SDL_Texture *);
	int (SDLCALL *poll_event)(SDL_Event *);
	Uint32 (SDLCALL *get_ticks)(void);
	int (SDLCALL *show_cursor)(int);
	void (SDLCALL *start_text_input)(void);
	void (SDLCALL *stop_text_input)(void);
	int (SDLCALL *set_clipboard_text)(const char *);
	char *(SDLCALL *get_clipboard_text)(void);
	void (SDLCALL *free_memory)(void *);
	int (SDLCALL *open_url)(const char *);
};

struct sdl_state {
	/* One in-flight command per display, serialized by the console core. */
	st_condition_t completed;
	/* Only the backend thread changes or dereferences this pointer. */
	st_console_t *console;
	st_console_display_t *console_display;
	struct st_console_bitmap_geometry window_geometry;
	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_Texture *texture;
	struct st_console_bitmap_presenter *presenter;
	struct st_console_bitmap_frame frame;
	uint32_t window_id;
	uint32_t texture_width;
	uint32_t texture_height;
	uint32_t minimum_width;
	uint32_t minimum_height;
	uint32_t windowed_width;
	uint32_t windowed_height;
	uint32_t mouse_buttons;
	int mouse_x;
	int mouse_y;
	uint32_t key_modifiers;
	bool texture_valid;
	bool focused;
	bool pointer_visible;
	bool fullscreen;
	bool maximized;
	struct sdl_state *next;
};


enum sdl_command_type {
	SDL_COMMAND_CREATE,
	SDL_COMMAND_DESTROY,
	SDL_COMMAND_PRESENT,
	SDL_COMMAND_ACTIVATE,
	SDL_COMMAND_RESIZE,
	SDL_COMMAND_SET_TITLE,
	SDL_COMMAND_SET_CLIPBOARD,
	SDL_COMMAND_GET_CLIPBOARD,
	SDL_COMMAND_OPEN_URL,
	SDL_COMMAND_SET_POINTER,
	SDL_COMMAND_SET_FULLSCREEN,
};

struct sdl_command {
	enum sdl_command_type type;
	struct sdl_state *state;
	union {
		st_console_t *console;
		struct {
			uint32_t width;
			uint32_t height;
			uint32_t source_width;
			uint32_t source_height;
			uint32_t minimum_width;
			uint32_t minimum_height;
		} size;
		const char *text;
		char **text_result;
		bool enabled;
	} data;
	int result;
	bool done;
	struct sdl_command *next;
};

struct application_call {
	int argc;
	char **argv;
	int (*main_function)(int, char **);
};

struct sdl_manager {
	st_mutex_t mutex;
	st_condition_t changed;
	bool synchronization_ready;
	bool active;
	bool application_done;
	int application_result;
	struct sdl_command *commands_head;
	struct sdl_command *commands_tail;
	dynamic_library_t *library;
	struct sdl_api api;
	bool initialized;
	struct sdl_state *states;
};

static struct sdl_manager manager;
static st_once_flag_t manager_once = ST_ONCE_FLAG_INIT;

static void
initialize_manager(void)
{
	if (st_mutex_init(&manager.mutex, ST_MUTEX_PLAIN) != ST_THREAD_SUCCESS)
		return;
	if (st_condition_init(&manager.changed) != ST_THREAD_SUCCESS) {
		st_mutex_destroy(&manager.mutex);
		return;
	}
	manager.synchronization_ready = true;
}

#define LOAD_REQUIRED(api, library, member, name) \
	(((api)->member = (sdl_ ## member ## _fn) \
	    dynamic_library_symbol((library), (name))) != NULL)

static int
load_sdl(void)
{
	static const char *const names[] = {"SDL2", "SDL2-2.0"};
	int status;

	if (manager.initialized)
		return 0;
	status = dynamic_library_open(&manager.library, names, 2, 0,
	    DYNAMIC_LIBRARY_LOCAL);
	if (status != 0)
		return status;
	if (!LOAD_REQUIRED(&manager.api, manager.library, set_main_ready,
	    "SDL_SetMainReady")
	    || !LOAD_REQUIRED(&manager.api, manager.library, init_subsystem,
	    "SDL_InitSubSystem")
	    || !LOAD_REQUIRED(&manager.api, manager.library, quit_subsystem,
	    "SDL_QuitSubSystem")
	    || !LOAD_REQUIRED(&manager.api, manager.library, create_window,
	    "SDL_CreateWindow")
	    || !LOAD_REQUIRED(&manager.api, manager.library, destroy_window,
	    "SDL_DestroyWindow")
	    || !LOAD_REQUIRED(&manager.api, manager.library, get_window_id,
	    "SDL_GetWindowID")
	    || !LOAD_REQUIRED(&manager.api, manager.library, get_window_size,
	    "SDL_GetWindowSize")
	    || !LOAD_REQUIRED(&manager.api, manager.library,
	    get_window_display_index, "SDL_GetWindowDisplayIndex")
	    || !LOAD_REQUIRED(&manager.api, manager.library, get_display_bounds,
	    "SDL_GetDisplayBounds")
	    || !LOAD_REQUIRED(&manager.api, manager.library, set_window_position,
	    "SDL_SetWindowPosition")
	    || !LOAD_REQUIRED(&manager.api, manager.library, set_window_title,
	    "SDL_SetWindowTitle")
	    || !LOAD_REQUIRED(&manager.api, manager.library, set_window_size,
	    "SDL_SetWindowSize")
	    || !LOAD_REQUIRED(&manager.api, manager.library,
	    set_window_minimum_size, "SDL_SetWindowMinimumSize")
	    || !LOAD_REQUIRED(&manager.api, manager.library, restore_window,
	    "SDL_RestoreWindow")
	    || !LOAD_REQUIRED(&manager.api, manager.library, set_window_fullscreen,
	    "SDL_SetWindowFullscreen")
	    || !LOAD_REQUIRED(&manager.api, manager.library,
	    create_rgb_surface_from, "SDL_CreateRGBSurfaceFrom")
	    || !LOAD_REQUIRED(&manager.api, manager.library, free_surface,
	    "SDL_FreeSurface")
	    || !LOAD_REQUIRED(&manager.api, manager.library, set_window_icon,
	    "SDL_SetWindowIcon")
	    || !LOAD_REQUIRED(&manager.api, manager.library, set_hint,
	    "SDL_SetHint")
	    || !LOAD_REQUIRED(&manager.api, manager.library, create_renderer,
	    "SDL_CreateRenderer")
	    || !LOAD_REQUIRED(&manager.api, manager.library, destroy_renderer,
	    "SDL_DestroyRenderer")
	    || !LOAD_REQUIRED(&manager.api, manager.library,
	    get_renderer_output_size, "SDL_GetRendererOutputSize")
	    || !LOAD_REQUIRED(&manager.api, manager.library, set_render_draw_color,
	    "SDL_SetRenderDrawColor")
	    || !LOAD_REQUIRED(&manager.api, manager.library, render_clear,
	    "SDL_RenderClear")
	    || !LOAD_REQUIRED(&manager.api, manager.library, render_copy,
	    "SDL_RenderCopy")
	    || !LOAD_REQUIRED(&manager.api, manager.library, render_present,
	    "SDL_RenderPresent")
	    || !LOAD_REQUIRED(&manager.api, manager.library, create_texture,
	    "SDL_CreateTexture")
	    || !LOAD_REQUIRED(&manager.api, manager.library, destroy_texture,
	    "SDL_DestroyTexture")
	    || !LOAD_REQUIRED(&manager.api, manager.library, lock_texture,
	    "SDL_LockTexture")
	    || !LOAD_REQUIRED(&manager.api, manager.library, unlock_texture,
	    "SDL_UnlockTexture")
	    || !LOAD_REQUIRED(&manager.api, manager.library, poll_event,
	    "SDL_PollEvent")
	    || !LOAD_REQUIRED(&manager.api, manager.library, get_ticks,
	    "SDL_GetTicks")
	    || !LOAD_REQUIRED(&manager.api, manager.library, show_cursor,
	    "SDL_ShowCursor")
	    || !LOAD_REQUIRED(&manager.api, manager.library, start_text_input,
	    "SDL_StartTextInput")
	    || !LOAD_REQUIRED(&manager.api, manager.library, stop_text_input,
	    "SDL_StopTextInput")
	    || !LOAD_REQUIRED(&manager.api, manager.library, set_clipboard_text,
	    "SDL_SetClipboardText")
	    || !LOAD_REQUIRED(&manager.api, manager.library, get_clipboard_text,
	    "SDL_GetClipboardText")
	    || !LOAD_REQUIRED(&manager.api, manager.library, free_memory,
	    "SDL_free")) {
		(void)dynamic_library_close(manager.library);
		manager.library = NULL;
		memset(&manager.api, 0, sizeof(manager.api));
		return IO_ERROR_NOT_SUPPORTED;
	}
	manager.api.open_url = (sdl_open_url_fn)dynamic_library_symbol(
	    manager.library, "SDL_OpenURL");
	manager.api.get_display_usable_bounds =
	    (sdl_get_display_usable_bounds_fn)dynamic_library_symbol(
	    manager.library, "SDL_GetDisplayUsableBounds");
	manager.api.get_window_borders_size =
	    (sdl_get_window_borders_size_fn)dynamic_library_symbol(
	    manager.library, "SDL_GetWindowBordersSize");
	manager.api.set_main_ready();
	if (manager.api.init_subsystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
		(void)dynamic_library_close(manager.library);
		manager.library = NULL;
		memset(&manager.api, 0, sizeof(manager.api));
		return IO_ERROR_NOT_SUPPORTED;
	}
	manager.api.start_text_input();
	manager.initialized = true;
	return 0;
}

static int
make_texture(struct sdl_state *state, uint32_t width, uint32_t height,
	enum st_console_scaling_mode scaling_mode)
{
	SDL_Texture *texture;

	if (width == 0 || height == 0 || width > INT_MAX || height > INT_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	(void)manager.api.set_hint(SDL_HINT_RENDER_SCALE_QUALITY,
	    scaling_mode == ST_CONSOLE_SCALING_INTERNAL ? "0" : "2");
	texture = manager.api.create_texture(state->renderer,
	    SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, (int)width,
	    (int)height);
	if (texture == NULL)
		return IO_ERROR_IO;
	manager.api.destroy_texture(state->texture);
	state->texture = texture;
	state->texture_width = width;
	state->texture_height = height;
	state->texture_valid = false;
	return 0;
}

static int
window_units(struct sdl_state *state, uint32_t pixel_width,
	uint32_t pixel_height, int *width, int *height)
{
	int window_width;
	int window_height;
	int output_width;
	int output_height;
	uint64_t converted_width;
	uint64_t converted_height;

	manager.api.get_window_size(state->window, &window_width, &window_height);
	if (manager.api.get_renderer_output_size(state->renderer, &output_width,
	    &output_height) != 0 || window_width <= 0 || window_height <= 0
	    || output_width <= 0 || output_height <= 0)
		return IO_ERROR_IO;
	converted_width = ((uint64_t)pixel_width * (uint32_t)window_width
	    + (uint32_t)output_width - 1U) / (uint32_t)output_width;
	converted_height = ((uint64_t)pixel_height * (uint32_t)window_height
	    + (uint32_t)output_height - 1U) / (uint32_t)output_height;
	if (converted_width == 0 || converted_height == 0
	    || converted_width > INT_MAX || converted_height > INT_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	*width = (int)converted_width;
	*height = (int)converted_height;
	return 0;
}

static int
set_window_pixel_geometry(struct sdl_state *state, uint32_t width,
	uint32_t height, uint32_t minimum_width, uint32_t minimum_height,
	bool resize)
{
	int window_width;
	int window_height;
	int minimum_window_width;
	int minimum_window_height;
	int status = window_units(state, minimum_width, minimum_height,
	    &minimum_window_width, &minimum_window_height);

	if (status != 0)
		return status;
	manager.api.set_window_minimum_size(state->window, minimum_window_width,
	    minimum_window_height);
	if (resize) {
		status = window_units(state, width, height, &window_width,
		    &window_height);
		if (status != 0)
			return status;
		manager.api.set_window_size(state->window, window_width,
		    window_height);
	}
	return 0;
}

static int
set_fullscreen_state(struct sdl_state *state, bool enabled)
{
	if (enabled == state->fullscreen)
		return 0;
	if (enabled) {
		int width;
		int height;

		if (manager.api.get_renderer_output_size(state->renderer,
		    &width, &height) == 0 && width > 0 && height > 0) {
			state->windowed_width = (uint32_t)width;
			state->windowed_height = (uint32_t)height;
		}
	}
	if (manager.api.set_window_fullscreen(state->window,
	    enabled ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) != 0)
		return IO_ERROR_IO;
	state->fullscreen = enabled;
	if (!enabled)
		return set_window_pixel_geometry(state, state->windowed_width,
		    state->windowed_height, state->minimum_width,
		    state->minimum_height, true);
	return 0;
}

static void
display_pixel_bounds(struct sdl_state *state, uint32_t *width,
	uint32_t *height, int *display_index)
{
	SDL_Rect bounds;
	int index;
	int window_width;
	int window_height;
	int output_width;
	int output_height;
	int top = 0;
	int left = 0;
	int bottom = 0;
	int right = 0;
	uint64_t scaled_width;
	uint64_t scaled_height;

	*width = 0;
	*height = 0;
	index = manager.api.get_window_display_index(state->window);
	if (display_index != NULL)
		*display_index = index;
	if (index < 0
	    || ((manager.api.get_display_usable_bounds == NULL
	    || manager.api.get_display_usable_bounds(index, &bounds) != 0)
	    && manager.api.get_display_bounds(index, &bounds) != 0))
		return;
	manager.api.get_window_size(state->window, &window_width, &window_height);
	if (manager.api.get_renderer_output_size(state->renderer, &output_width,
	    &output_height) != 0 || window_width <= 0 || window_height <= 0
	    || output_width <= 0 || output_height <= 0)
		return;
	if (manager.api.get_window_borders_size != NULL)
		(void)manager.api.get_window_borders_size(state->window, &top,
		    &left, &bottom, &right);
	if (bounds.w <= left + right || bounds.h <= top + bottom)
		return;
	scaled_width = (uint64_t)(bounds.w - left - right)
	    * (uint32_t)output_width / (uint32_t)window_width;
	scaled_height = (uint64_t)(bounds.h - top - bottom)
	    * (uint32_t)output_height / (uint32_t)window_height;
	if (scaled_width == 0 || scaled_height == 0
	    || scaled_width > UINT32_MAX || scaled_height > UINT32_MAX)
		return;
	*width = (uint32_t)scaled_width;
	*height = (uint32_t)scaled_height;
}

static void
snap_window(struct sdl_state *state, bool larger)
{
	int current_width;
	int current_height;
	uint32_t maximum_width;
	uint32_t maximum_height;
	uint32_t width;
	uint32_t height;

	if (state->fullscreen)
		return;
	if (state->maximized) {
		manager.api.restore_window(state->window);
		state->maximized = false;
		current_width = (int)state->windowed_width;
		current_height = (int)state->windowed_height;
	}
	else if (manager.api.get_renderer_output_size(state->renderer,
	    &current_width, &current_height) != 0
	    || current_width <= 0 || current_height <= 0)
		return;
	display_pixel_bounds(state, &maximum_width, &maximum_height, NULL);
	if (st_console_snap_geometry_size(&state->window_geometry,
	    (uint32_t)current_width, (uint32_t)current_height, larger,
	    maximum_width, maximum_height,
	    &width, &height) != 0
	    || (width == (uint32_t)current_width
	    && height == (uint32_t)current_height))
		return;
	state->windowed_width = width;
	state->windowed_height = height;
	(void)set_window_pixel_geometry(state, width, height,
	    state->minimum_width, state->minimum_height, true);
}

static int
create_state(struct sdl_state *state, uint32_t width, uint32_t height,
	uint32_t minimum_width, uint32_t minimum_height)
{
	const struct st_console_icon *icon = state->console_display->config.icon;
	int status;

	status = load_sdl();
	if (status != 0)
		return status;
	if (width == 0 || height == 0 || width > INT_MAX || height > INT_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	state->window = manager.api.create_window("SyncTERM",
	    SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, (int)width,
	    (int)height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
	if (state->window == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	if (icon != NULL && icon->width <= INT_MAX && icon->height <= INT_MAX
	    && icon->stride <= INT_MAX) {
		SDL_Surface *surface = manager.api.create_rgb_surface_from(
		    (void *)icon->pixels, (int)icon->width, (int)icon->height, 32,
		    (int)icon->stride,
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
		    UINT32_C(0xff000000), UINT32_C(0x00ff0000),
		    UINT32_C(0x0000ff00), UINT32_C(0x000000ff));
#else
		    UINT32_C(0x000000ff), UINT32_C(0x0000ff00),
		    UINT32_C(0x00ff0000), UINT32_C(0xff000000));
#endif

		if (surface != NULL) {
			manager.api.set_window_icon(state->window, surface);
			manager.api.free_surface(surface);
		}
	}
	state->renderer = manager.api.create_renderer(state->window, -1,
	    SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (state->renderer == NULL)
		state->renderer = manager.api.create_renderer(state->window, -1,
		    SDL_RENDERER_SOFTWARE);
	if (state->renderer == NULL) {
		manager.api.destroy_window(state->window);
		state->window = NULL;
		return IO_ERROR_NOT_SUPPORTED;
	}
	state->window_id = manager.api.get_window_id(state->window);
	status = state->window_id == 0 ? IO_ERROR_IO : 0;
	if (status == 0)
		status = set_window_pixel_geometry(state, width, height,
		    minimum_width, minimum_height, true);
	if (status != 0) {
		manager.api.destroy_renderer(state->renderer);
		manager.api.destroy_window(state->window);
		state->renderer = NULL;
		state->window = NULL;
		return status;
	}
	state->focused = true;
	state->pointer_visible = true;
	state->minimum_width = minimum_width;
	state->minimum_height = minimum_height;
	state->windowed_width = width;
	state->windowed_height = height;
	state->next = manager.states;
	manager.states = state;
	return 0;
}

static void
destroy_state(struct sdl_state *state)
{
	struct sdl_state **link = &manager.states;

	while (*link != NULL && *link != state)
		link = &(*link)->next;
	if (*link == state)
		*link = state->next;
	manager.api.destroy_texture(state->texture);
	manager.api.destroy_renderer(state->renderer);
	manager.api.destroy_window(state->window);
	state->texture = NULL;
	state->renderer = NULL;
	state->window = NULL;
}

static int
upload_pixels(void *context, const struct st_rgb_buffer *pixels,
	const struct st_console_rect *rectangle)
{
	struct sdl_state *state = context;
	SDL_Rect target;
	void *locked = NULL;
	int pitch = 0;
	int status;

	if (state->texture == NULL || state->texture_width != pixels->width
	    || state->texture_height != pixels->height) {
		status = make_texture(state, pixels->width, pixels->height,
		    state->frame.scaling_mode);
		if (status != 0)
			return status;
	}
	if (rectangle->x > INT_MAX || rectangle->y > INT_MAX
	    || rectangle->width > INT_MAX || rectangle->height > INT_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	target = (SDL_Rect){(int)rectangle->x, (int)rectangle->y,
	    (int)rectangle->width, (int)rectangle->height};
	if (manager.api.lock_texture(state->texture, &target, &locked, &pitch)
	    != 0)
		return IO_ERROR_IO;
	for (uint32_t y = 0; y < rectangle->height; ++y) {
		const uint32_t *source = pixels->pixels
		    + (size_t)(rectangle->y + y) * pixels->width + rectangle->x;
		void *destination = (unsigned char *)locked
		    + (size_t)y * (size_t)pitch;

		memcpy(destination, source,
		    (size_t)rectangle->width * sizeof(source[0]));
	}
	manager.api.unlock_texture(state->texture);
	state->texture_valid = true;
	return 0;
}

static int
draw_state(struct sdl_state *state, const st_console_t *console, uint32_t now,
	bool forced)
{
	SDL_Rect destination;
	int output_width;
	int output_height;
	int status;

	if (manager.api.get_renderer_output_size(state->renderer, &output_width,
	    &output_height) != 0)
		return IO_ERROR_IO;
	if (output_width <= 0 || output_height <= 0)
		return 0;
	state->frame.scaling_mode = console == NULL
	    ? state->frame.scaling_mode : console->scaling_mode;
	status = st_console_bitmap_presenter_frame(state->presenter, console,
	    (uint32_t)output_width, (uint32_t)output_height, now, upload_pixels,
	    state, &state->frame);
	if (status != 0)
		return status;
	if (!state->texture_valid || (!state->frame.changed && !forced))
		return 0;
	destination = (SDL_Rect){(int)state->frame.destination.x,
	    (int)state->frame.destination.y, (int)state->frame.destination.width,
	    (int)state->frame.destination.height};
	if (manager.api.set_render_draw_color(state->renderer, 0, 0, 0, 255) != 0
	    || manager.api.render_clear(state->renderer) != 0
	    || manager.api.render_copy(state->renderer, state->texture, NULL,
	    &destination) != 0)
		return IO_ERROR_IO;
	manager.api.render_present(state->renderer);
	return 0;
}

static uint32_t
event_modifiers(SDL_Keymod value)
{
	uint32_t result = 0;

	if ((value & KMOD_SHIFT) != 0) result |= ST_CONSOLE_MODIFIER_SHIFT;
	if ((value & KMOD_CTRL) != 0) result |= ST_CONSOLE_MODIFIER_CONTROL;
	if ((value & KMOD_ALT) != 0) result |= ST_CONSOLE_MODIFIER_ALT;
	if ((value & KMOD_GUI) != 0) result |= ST_CONSOLE_MODIFIER_GUI;
	if ((value & KMOD_CAPS) != 0) result |= ST_CONSOLE_MODIFIER_CAPS_LOCK;
	if ((value & KMOD_NUM) != 0) result |= ST_CONSOLE_MODIFIER_NUM_LOCK;
	return result;
}

static uint16_t
evdev_key(SDL_Scancode key)
{
	if (key >= SDL_SCANCODE_A && key <= SDL_SCANCODE_Z) {
		static const uint16_t letters[] = {
			30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38, 50,
			49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44,
		};
		return letters[key - SDL_SCANCODE_A];
	}
	switch (key) {
	case SDL_SCANCODE_ESCAPE: return 1;
	case SDL_SCANCODE_1: return 2;
	case SDL_SCANCODE_2: return 3;
	case SDL_SCANCODE_3: return 4;
	case SDL_SCANCODE_4: return 5;
	case SDL_SCANCODE_5: return 6;
	case SDL_SCANCODE_6: return 7;
	case SDL_SCANCODE_7: return 8;
	case SDL_SCANCODE_8: return 9;
	case SDL_SCANCODE_9: return 10;
	case SDL_SCANCODE_0: return 11;
	case SDL_SCANCODE_MINUS: return 12;
	case SDL_SCANCODE_EQUALS: return 13;
	case SDL_SCANCODE_BACKSPACE: return 14;
	case SDL_SCANCODE_TAB: return 15;
	case SDL_SCANCODE_LEFTBRACKET: return 26;
	case SDL_SCANCODE_RIGHTBRACKET: return 27;
	case SDL_SCANCODE_RETURN: return 28;
	case SDL_SCANCODE_LCTRL: return 29;
	case SDL_SCANCODE_SEMICOLON: return 39;
	case SDL_SCANCODE_APOSTROPHE: return 40;
	case SDL_SCANCODE_GRAVE: return 41;
	case SDL_SCANCODE_LSHIFT: return 42;
	case SDL_SCANCODE_BACKSLASH: return 43;
	case SDL_SCANCODE_COMMA: return 51;
	case SDL_SCANCODE_PERIOD: return 52;
	case SDL_SCANCODE_SLASH: return 53;
	case SDL_SCANCODE_RSHIFT: return 54;
	case SDL_SCANCODE_KP_MULTIPLY: return 55;
	case SDL_SCANCODE_LALT: return 56;
	case SDL_SCANCODE_SPACE: return 57;
	case SDL_SCANCODE_CAPSLOCK: return 58;
	case SDL_SCANCODE_F1: return 59;
	case SDL_SCANCODE_F2: return 60;
	case SDL_SCANCODE_F3: return 61;
	case SDL_SCANCODE_F4: return 62;
	case SDL_SCANCODE_F5: return 63;
	case SDL_SCANCODE_F6: return 64;
	case SDL_SCANCODE_F7: return 65;
	case SDL_SCANCODE_F8: return 66;
	case SDL_SCANCODE_F9: return 67;
	case SDL_SCANCODE_F10: return 68;
	case SDL_SCANCODE_NUMLOCKCLEAR: return 69;
	case SDL_SCANCODE_SCROLLLOCK: return 70;
	case SDL_SCANCODE_KP_7: return 71;
	case SDL_SCANCODE_KP_8: return 72;
	case SDL_SCANCODE_KP_9: return 73;
	case SDL_SCANCODE_KP_MINUS: return 74;
	case SDL_SCANCODE_KP_4: return 75;
	case SDL_SCANCODE_KP_5: return 76;
	case SDL_SCANCODE_KP_6: return 77;
	case SDL_SCANCODE_KP_PLUS: return 78;
	case SDL_SCANCODE_KP_1: return 79;
	case SDL_SCANCODE_KP_2: return 80;
	case SDL_SCANCODE_KP_3: return 81;
	case SDL_SCANCODE_KP_0: return 82;
	case SDL_SCANCODE_KP_PERIOD: return 83;
	case SDL_SCANCODE_NONUSBACKSLASH: return 86;
	case SDL_SCANCODE_F11: return 87;
	case SDL_SCANCODE_F12: return 88;
	case SDL_SCANCODE_KP_ENTER: return 96;
	case SDL_SCANCODE_RCTRL: return 97;
	case SDL_SCANCODE_KP_DIVIDE: return 98;
	case SDL_SCANCODE_PRINTSCREEN: return 99;
	case SDL_SCANCODE_RALT: return 100;
	case SDL_SCANCODE_HOME: return 102;
	case SDL_SCANCODE_UP: return 103;
	case SDL_SCANCODE_PAGEUP: return 104;
	case SDL_SCANCODE_LEFT: return 105;
	case SDL_SCANCODE_RIGHT: return 106;
	case SDL_SCANCODE_END: return 107;
	case SDL_SCANCODE_DOWN: return 108;
	case SDL_SCANCODE_PAGEDOWN: return 109;
	case SDL_SCANCODE_INSERT: return 110;
	case SDL_SCANCODE_DELETE: return 111;
	case SDL_SCANCODE_MUTE: return 113;
	case SDL_SCANCODE_VOLUMEDOWN: return 114;
	case SDL_SCANCODE_VOLUMEUP: return 115;
	case SDL_SCANCODE_POWER: return 116;
	case SDL_SCANCODE_KP_EQUALS: return 117;
	case SDL_SCANCODE_PAUSE: return 119;
	case SDL_SCANCODE_LGUI: return 125;
	case SDL_SCANCODE_RGUI: return 126;
	case SDL_SCANCODE_APPLICATION: return 127;
	case SDL_SCANCODE_F13: return 183;
	case SDL_SCANCODE_F14: return 184;
	case SDL_SCANCODE_F15: return 185;
	case SDL_SCANCODE_F16: return 186;
	case SDL_SCANCODE_F17: return 187;
	case SDL_SCANCODE_F18: return 188;
	case SDL_SCANCODE_F19: return 189;
	case SDL_SCANCODE_F20: return 190;
	case SDL_SCANCODE_F21: return 191;
	case SDL_SCANCODE_F22: return 192;
	case SDL_SCANCODE_F23: return 193;
	case SDL_SCANCODE_F24: return 194;
	default: return 0;
	}
}

static uint16_t
translated_key(SDL_Keycode key)
{
	switch (key) {
	case SDLK_BACKSPACE: return '\b';
	case SDLK_TAB: return '\t';
	case SDLK_RETURN: return '\r';
	case SDLK_ESCAPE: return 0x1b;
	case SDLK_DELETE: return ST_CONSOLE_KEY_DELETE;
	case SDLK_UP: return ST_CONSOLE_KEY_UP;
	case SDLK_DOWN: return ST_CONSOLE_KEY_DOWN;
	case SDLK_LEFT: return ST_CONSOLE_KEY_LEFT;
	case SDLK_RIGHT: return ST_CONSOLE_KEY_RIGHT;
	case SDLK_INSERT: return ST_CONSOLE_KEY_INSERT;
	case SDLK_HOME: return ST_CONSOLE_KEY_HOME;
	case SDLK_END: return ST_CONSOLE_KEY_END;
	case SDLK_PAGEUP: return ST_CONSOLE_KEY_PAGE_UP;
	case SDLK_PAGEDOWN: return ST_CONSOLE_KEY_PAGE_DOWN;
	case SDLK_F1: return ST_CONSOLE_KEY_F1;
	case SDLK_F2: return ST_CONSOLE_KEY_F2;
	case SDLK_F3: return ST_CONSOLE_KEY_F3;
	case SDLK_F4: return ST_CONSOLE_KEY_F4;
	case SDLK_F5: return ST_CONSOLE_KEY_F5;
	case SDLK_F6: return ST_CONSOLE_KEY_F6;
	case SDLK_F7: return ST_CONSOLE_KEY_F7;
	case SDLK_F8: return ST_CONSOLE_KEY_F8;
	case SDLK_F9: return ST_CONSOLE_KEY_F9;
	case SDLK_F10: return ST_CONSOLE_KEY_F10;
	case SDLK_F11: return ST_CONSOLE_KEY_F11;
	case SDLK_F12: return ST_CONSOLE_KEY_F12;
	default: return 0;
	}
}

static struct sdl_state *
state_for_event(const SDL_Event *event)
{
	Uint32 identifier = 0;

	switch (event->type) {
	case SDL_WINDOWEVENT: identifier = event->window.windowID; break;
	case SDL_KEYDOWN:
	case SDL_KEYUP: identifier = event->key.windowID; break;
	case SDL_TEXTINPUT: identifier = event->text.windowID; break;
	case SDL_MOUSEMOTION: identifier = event->motion.windowID; break;
	case SDL_MOUSEBUTTONDOWN:
	case SDL_MOUSEBUTTONUP: identifier = event->button.windowID; break;
	case SDL_MOUSEWHEEL: identifier = event->wheel.windowID; break;
	default: break;
	}
	for (struct sdl_state *state = manager.states; state != NULL;
	    state = state->next)
		if (state->window_id == identifier)
			return state;
	return NULL;
}

static bool
utf8_next(const unsigned char **current, uint32_t *result)
{
	const unsigned char *text = *current;
	size_t consumed;

	if (st_utf8_decode_one(result, &consumed, text, strlen((const char *)text))
	    < 0)
		return false;
	*current = text + consumed;
	return true;
}

static void
logical_mouse(struct sdl_state *state, int x, int y,
	struct st_console_event *event)
{
	int window_width = 0;
	int window_height = 0;
	int output_width = 0;
	int output_height = 0;
	int64_t output_x;
	int64_t output_y;

	manager.api.get_window_size(state->window, &window_width, &window_height);
	if (manager.api.get_renderer_output_size(state->renderer, &output_width,
	    &output_height) != 0 || window_width <= 0 || window_height <= 0
	    || output_width <= 0 || output_height <= 0) {
		event->data.mouse.x = 0;
		event->data.mouse.y = 0;
		event->data.mouse.pixel_x = -1;
		event->data.mouse.pixel_y = -1;
		return;
	}
	output_x = (int64_t)x * output_width / window_width;
	output_y = (int64_t)y * output_height / window_height;
	st_console_bitmap_mouse_position(&state->frame, output_x, output_y,
	    event);
}

static uint32_t
mouse_button(Uint8 button)
{
	switch (button) {
	case SDL_BUTTON_LEFT: return ST_CONSOLE_MOUSE_BUTTON_LEFT;
	case SDL_BUTTON_MIDDLE: return ST_CONSOLE_MOUSE_BUTTON_MIDDLE;
	case SDL_BUTTON_RIGHT: return ST_CONSOLE_MOUSE_BUTTON_RIGHT;
	case SDL_BUTTON_X1: return ST_CONSOLE_MOUSE_BUTTON_X1;
	case SDL_BUTTON_X2: return ST_CONSOLE_MOUSE_BUTTON_X2;
	default: return 0;
	}
}

static uint32_t
mouse_buttons(Uint32 buttons)
{
	uint32_t result = 0;

	if ((buttons & SDL_BUTTON_LMASK) != 0)
		result |= ST_CONSOLE_MOUSE_BUTTON_LEFT;
	if ((buttons & SDL_BUTTON_MMASK) != 0)
		result |= ST_CONSOLE_MOUSE_BUTTON_MIDDLE;
	if ((buttons & SDL_BUTTON_RMASK) != 0)
		result |= ST_CONSOLE_MOUSE_BUTTON_RIGHT;
	if ((buttons & SDL_BUTTON_X1MASK) != 0)
		result |= ST_CONSOLE_MOUSE_BUTTON_X1;
	if ((buttons & SDL_BUTTON_X2MASK) != 0)
		result |= ST_CONSOLE_MOUSE_BUTTON_X2;
	return result;
}

static void
dispatch_event(const SDL_Event *source)
{
	struct sdl_state *state = state_for_event(source);
	struct st_console_event event = {0};

	if (source->type == SDL_QUIT) {
		for (state = manager.states; state != NULL; state = state->next) {
			event.type = ST_CONSOLE_EVENT_CLOSE;
			(void)st_console_display_post_event(state->console_display, &event);
		}
		return;
	}
	if (state == NULL)
		return;
	switch (source->type) {
	case SDL_WINDOWEVENT:
		if (source->window.event == SDL_WINDOWEVENT_MAXIMIZED)
			state->maximized = true;
		else if (source->window.event == SDL_WINDOWEVENT_RESTORED) {
			state->maximized = false;
			(void)set_window_pixel_geometry(state,
			    state->windowed_width, state->windowed_height,
			    state->minimum_width, state->minimum_height, true);
		}
		else if (source->window.event == SDL_WINDOWEVENT_FOCUS_GAINED
		    || source->window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
			state->focused = source->window.event
			    == SDL_WINDOWEVENT_FOCUS_GAINED;
			if (state->focused)
				(void)manager.api.show_cursor(state->pointer_visible
				    ? SDL_ENABLE : SDL_DISABLE);
			event.type = ST_CONSOLE_EVENT_FOCUS;
			event.data.focus.focused = state->focused;
			(void)st_console_display_post_event(state->console_display, &event);
		}
		else if (source->window.event == SDL_WINDOWEVENT_CLOSE) {
			event.type = ST_CONSOLE_EVENT_CLOSE;
			(void)st_console_display_post_event(state->console_display, &event);
		}
		else if (source->window.event == SDL_WINDOWEVENT_EXPOSED)
			(void)draw_state(state, NULL, manager.api.get_ticks(), true);
		else if (source->window.event == SDL_WINDOWEVENT_RESIZED
		    || source->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
			if (!state->fullscreen && !state->maximized) {
				int width;
				int height;

				if (manager.api.get_renderer_output_size(state->renderer,
				    &width, &height) == 0 && width > 0 && height > 0) {
					state->windowed_width = (uint32_t)width;
					state->windowed_height = (uint32_t)height;
				}
			}
			(void)draw_state(state, NULL, manager.api.get_ticks(), true);
		}
		break;
	case SDL_KEYDOWN:
	case SDL_KEYUP: {
		uint16_t physical = evdev_key(source->key.keysym.scancode);
		uint16_t key = translated_key(source->key.keysym.sym);

		state->key_modifiers = event_modifiers(source->key.keysym.mod);
		if (physical != 0) {
			event.type = ST_CONSOLE_EVENT_PHYSICAL_KEY;
			event.data.physical_key.evdev = physical;
			event.data.physical_key.modifiers = state->key_modifiers;
			event.data.physical_key.pressed = source->type == SDL_KEYDOWN;
			event.data.physical_key.repeat = source->key.repeat != 0;
			(void)st_console_display_post_event(state->console_display, &event);
		}
		if (source->type == SDL_KEYDOWN
		    && (state->key_modifiers & ST_CONSOLE_MODIFIER_ALT) != 0) {
			if ((source->key.keysym.scancode == SDL_SCANCODE_RETURN
			    || source->key.keysym.scancode == SDL_SCANCODE_KP_ENTER)
			    && source->key.repeat == 0) {
				(void)set_fullscreen_state(state, !state->fullscreen);
				break;
			}
			if (source->key.keysym.scancode == SDL_SCANCODE_LEFT) {
				snap_window(state, false);
				break;
			}
			if (source->key.keysym.scancode == SDL_SCANCODE_RIGHT) {
				snap_window(state, true);
				break;
			}
			if (source->key.keysym.scancode == SDL_SCANCODE_HOME
			    || source->key.keysym.scancode == SDL_SCANCODE_KP_7) {
				int display_index;
				uint32_t unused_width;
				uint32_t unused_height;

				display_pixel_bounds(state, &unused_width,
				    &unused_height, &display_index);
				if (display_index < 0)
					break;
				manager.api.set_window_position(state->window,
				    SDL_WINDOWPOS_CENTERED_DISPLAY(display_index),
				    SDL_WINDOWPOS_CENTERED_DISPLAY(display_index));
				break;
			}
		}
		if (source->type == SDL_KEYDOWN && key != 0) {
			event = (struct st_console_event){
				.type = ST_CONSOLE_EVENT_KEY_CODE,
				.data.key = {
					.code = key,
					.modifiers = state->key_modifiers,
				},
			};
			(void)st_console_display_post_event(state->console_display, &event);
		}
		break;
	}
	case SDL_TEXTINPUT: {
		const unsigned char *text = (const unsigned char *)source->text.text;

		while (*text != 0) {
			uint32_t codepoint;

			if (!utf8_next(&text, &codepoint))
				break;
			event = (struct st_console_event){
				.type = ST_CONSOLE_EVENT_TEXT,
				.data.text = {
					.codepoint = codepoint,
					.modifiers = state->key_modifiers,
				},
			};
			(void)st_console_display_post_event(state->console_display, &event);
		}
		break;
	}
	case SDL_MOUSEMOTION:
		event.type = ST_CONSOLE_EVENT_MOUSE;
		event.data.mouse.action = ST_CONSOLE_MOUSE_MOVE;
		state->mouse_x = source->motion.x;
		state->mouse_y = source->motion.y;
		logical_mouse(state, state->mouse_x, state->mouse_y, &event);
		state->mouse_buttons = mouse_buttons(source->motion.state);
		event.data.mouse.buttons = state->mouse_buttons;
		(void)st_console_display_post_event(state->console_display, &event);
		break;
	case SDL_MOUSEBUTTONDOWN:
	case SDL_MOUSEBUTTONUP: {
		uint32_t button = mouse_button(source->button.button);

		if (source->type == SDL_MOUSEBUTTONDOWN)
			state->mouse_buttons |= button;
		else
			state->mouse_buttons &= ~button;
		event.type = ST_CONSOLE_EVENT_MOUSE;
		event.data.mouse.action = ST_CONSOLE_MOUSE_BUTTON;
		state->mouse_x = source->button.x;
		state->mouse_y = source->button.y;
		logical_mouse(state, state->mouse_x, state->mouse_y, &event);
		event.data.mouse.buttons = state->mouse_buttons;
		(void)st_console_display_post_event(state->console_display, &event);
		break;
	}
	case SDL_MOUSEWHEEL:
		event.type = ST_CONSOLE_EVENT_MOUSE;
		event.data.mouse.action = ST_CONSOLE_MOUSE_WHEEL;
		event.data.mouse.wheel_x = source->wheel.direction
		    == SDL_MOUSEWHEEL_FLIPPED ? -source->wheel.x : source->wheel.x;
		event.data.mouse.wheel_y = source->wheel.direction
		    == SDL_MOUSEWHEEL_FLIPPED ? -source->wheel.y : source->wheel.y;
		event.data.mouse.buttons = state->mouse_buttons;
		logical_mouse(state, state->mouse_x, state->mouse_y, &event);
		(void)st_console_display_post_event(state->console_display, &event);
		break;
	default:
		break;
	}
}

static int
copy_clipboard(char **result)
{
	char *source = manager.api.get_clipboard_text();
	char *copy;
	size_t length;

	if (source == NULL)
		return IO_ERROR_IO;
	length = strlen(source);
	copy = malloc(length + 1);
	if (copy != NULL)
		memcpy(copy, source, length + 1);
	manager.api.free_memory(source);
	if (copy == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	*result = copy;
	return 0;
}

static int
process_command(struct sdl_command *command)
{
	struct sdl_state *state = command->state;

	switch (command->type) {
	case SDL_COMMAND_CREATE:
		return create_state(state, command->data.size.width,
		    command->data.size.height, command->data.size.minimum_width,
		    command->data.size.minimum_height);
	case SDL_COMMAND_DESTROY:
		destroy_state(state);
		return 0;
	case SDL_COMMAND_ACTIVATE:
		state->console = command->data.console;
		return 0;
	case SDL_COMMAND_PRESENT:
		return draw_state(state, state->console, manager.api.get_ticks(),
		    false);
	case SDL_COMMAND_RESIZE:
		{
		int status;
		struct st_console_bitmap_geometry next = {
			command->data.size.source_width, command->data.size.source_height,
			state->console->aspect_width, state->console->aspect_height};
		int output_width;
		int output_height;
		uint32_t width;
		uint32_t height;
		uint32_t minimum_width;
		uint32_t minimum_height;

		if (state->fullscreen || state->maximized) {
			output_width = (int)state->windowed_width;
			output_height = (int)state->windowed_height;
		}
		else if (manager.api.get_renderer_output_size(state->renderer,
		    &output_width, &output_height) != 0 || output_width <= 0
		    || output_height <= 0)
			return IO_ERROR_IO;
		if (st_console_preserve_geometry_scale(&state->window_geometry, &next,
		    (uint32_t)output_width,
		    (uint32_t)output_height, &width, &height, &minimum_width,
		    &minimum_height) != 0)
			return IO_ERROR_OUT_OF_RANGE;
		state->windowed_width = width;
		state->windowed_height = height;
		state->minimum_width = minimum_width;
		state->minimum_height = minimum_height;
		status = set_window_pixel_geometry(state, width, height,
		    minimum_width, minimum_height,
		    !state->fullscreen && !state->maximized);
		if (status == 0)
			state->window_geometry = next;
		return status;
		}
	case SDL_COMMAND_SET_TITLE:
		manager.api.set_window_title(state->window, command->data.text);
		return 0;
	case SDL_COMMAND_SET_CLIPBOARD:
		return manager.api.set_clipboard_text(command->data.text) == 0
		    ? 0 : IO_ERROR_IO;
	case SDL_COMMAND_GET_CLIPBOARD:
		return copy_clipboard(command->data.text_result);
	case SDL_COMMAND_OPEN_URL:
		if (manager.api.open_url == NULL)
			return IO_ERROR_NOT_SUPPORTED;
		return manager.api.open_url(command->data.text) == 0
		    ? 0 : IO_ERROR_IO;
	case SDL_COMMAND_SET_POINTER:
		state->pointer_visible = command->data.enabled;
		if (!state->focused)
			return 0;
		return manager.api.show_cursor(state->pointer_visible
		    ? SDL_ENABLE : SDL_DISABLE) >= 0 ? 0 : IO_ERROR_IO;
	case SDL_COMMAND_SET_FULLSCREEN:
		return set_fullscreen_state(state, command->data.enabled);
	}
	return IO_ERROR_INVALID_ARGUMENT;
}

static int
submit_command(struct sdl_command *command)
{
	int status;

	command->done = false;
	command->next = NULL;
	status = st_mutex_lock(&manager.mutex);
	if (status != ST_THREAD_SUCCESS) {
		return IO_ERROR_IO;
	}
	if (!manager.active) {
		(void)st_mutex_unlock(&manager.mutex);
		return IO_ERROR_NOT_SUPPORTED;
	}
	if (manager.commands_tail == NULL)
		manager.commands_head = command;
	else
		manager.commands_tail->next = command;
	manager.commands_tail = command;
	(void)st_condition_signal(&manager.changed);
	/* The queued command borrows the caller's stack until completion. */
	while (!command->done)
		(void)st_condition_wait(&command->state->completed, &manager.mutex);
	status = command->result;
	(void)st_mutex_unlock(&manager.mutex);
	return status;
}

static struct sdl_command *
next_command(bool *application_done)
{
	struct sdl_command *command;
	struct timespec deadline;

	(void)st_time_utc_deadline_after_ms(&deadline, 10);
	(void)st_mutex_lock(&manager.mutex);
	if (manager.commands_head == NULL && !manager.application_done)
		(void)st_condition_timedwait(&manager.changed, &manager.mutex,
		    &deadline);
	command = manager.commands_head;
	if (command != NULL) {
		manager.commands_head = command->next;
		if (manager.commands_head == NULL)
			manager.commands_tail = NULL;
	}
	*application_done = manager.application_done;
	(void)st_mutex_unlock(&manager.mutex);
	return command;
}

static void
complete_command(struct sdl_command *command, int result)
{
	(void)st_mutex_lock(&manager.mutex);
	command->result = result;
	command->done = true;
	(void)st_condition_signal(&command->state->completed);
	(void)st_mutex_unlock(&manager.mutex);
}

static int
application_thread(void *argument)
{
	struct application_call *call = argument;
	int result = call->main_function(call->argc, call->argv);

	(void)st_mutex_lock(&manager.mutex);
	manager.application_result = result;
	manager.application_done = true;
	(void)st_condition_signal(&manager.changed);
	(void)st_mutex_unlock(&manager.mutex);
	return 0;
}

static void
shutdown_sdl(void)
{
	if (!manager.initialized)
		return;
	while (manager.states != NULL)
		destroy_state(manager.states);
	manager.api.stop_text_input();
	manager.api.quit_subsystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
	(void)dynamic_library_close(manager.library);
	manager.library = NULL;
	manager.initialized = false;
	memset(&manager.api, 0, sizeof(manager.api));
}

int
st_console_sdl_run_application(int argc, char **argv,
	int (*application_main)(int, char **))
{
	struct application_call call = {argc, argv, application_main};
	st_thread_t application;
	int status;
	int result;

	if (application_main == NULL)
		return 1;
	st_call_once(&manager_once, initialize_manager);
	if (!manager.synchronization_ready)
		return 1;
	(void)st_mutex_lock(&manager.mutex);
	if (manager.active) {
		(void)st_mutex_unlock(&manager.mutex);
		return 1;
	}
	manager.active = true;
	manager.application_done = false;
	manager.application_result = 1;
	(void)st_mutex_unlock(&manager.mutex);
	status = st_thread_create(&application, application_thread, &call);
	if (status != ST_THREAD_SUCCESS) {
		(void)st_mutex_lock(&manager.mutex);
		manager.active = false;
		(void)st_mutex_unlock(&manager.mutex);
		return 1;
	}
	for (;;) {
		struct sdl_command *command;
		bool done;

		command = next_command(&done);
		if (command != NULL)
			complete_command(command, process_command(command));
		if (manager.initialized) {
			SDL_Event event;
			uint32_t now;

			while (manager.api.poll_event(&event) != 0)
				dispatch_event(&event);
			now = manager.api.get_ticks();
			for (struct sdl_state *state = manager.states; state != NULL;
			    state = state->next)
				(void)draw_state(state, NULL, now, false);
		}
#if ST_CONSOLE_HAVE_QUARTZ
		st_console_quartz_pump();
#endif
		if (done && command == NULL)
			break;
	}
	(void)st_thread_join(application, NULL);
	shutdown_sdl();
	(void)st_mutex_lock(&manager.mutex);
	result = manager.application_result;
	manager.active = false;
	(void)st_mutex_unlock(&manager.mutex);
	return result;
}

static int
sdl_activate(st_console_t *console)
{
	return submit_command(&(struct sdl_command){
		.type = SDL_COMMAND_ACTIVATE,
		.state = console->display->backend_state,
		.data.console = console,
	});
}

static int
sdl_present(st_console_t *console)
{
	return submit_command(&(struct sdl_command){
		.type = SDL_COMMAND_PRESENT,
		.state = console->display->backend_state,
	});
}

static int
sdl_resize(st_console_t *console, uint32_t columns, uint32_t rows)
{
	uint32_t width;
	uint32_t height;

	if (columns > (UINT32_MAX - console->pixel_extra_width)
	    / console->cell_width
	    || rows > (UINT32_MAX - console->pixel_extra_height)
	    / console->cell_height)
		return IO_ERROR_OUT_OF_RANGE;
	width = columns * console->cell_width + console->pixel_extra_width;
	height = rows * console->cell_height + console->pixel_extra_height;
	if (width > INT_MAX || height > INT_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	return submit_command(&(struct sdl_command){
		.type = SDL_COMMAND_RESIZE,
		.state = console->display->backend_state,
		.data.size = {.source_width = width, .source_height = height},
	});
}

static int
sdl_set_title(st_console_display_t *display, const char *title)
{
	return submit_command(&(struct sdl_command){SDL_COMMAND_SET_TITLE,
	    display->backend_state, {.text = title}, .result = 0,
	    .done = false, .next = NULL});
}

static int
sdl_set_clipboard(st_console_display_t *display, const char *text)
{
	return submit_command(&(struct sdl_command){SDL_COMMAND_SET_CLIPBOARD,
	    display->backend_state, {.text = text}, .result = 0,
	    .done = false, .next = NULL});
}

static int
sdl_get_clipboard(st_console_display_t *display, char **result)
{
	return submit_command(&(struct sdl_command){SDL_COMMAND_GET_CLIPBOARD,
	    display->backend_state, {.text_result = result}, .result = 0,
	    .done = false, .next = NULL});
}

static int
sdl_open_url(st_console_display_t *display, const char *url)
{
	return submit_command(&(struct sdl_command){SDL_COMMAND_OPEN_URL,
	    display->backend_state, {.text = url}, .result = 0,
	    .done = false, .next = NULL});
}

static int
sdl_set_pointer(st_console_display_t *display, bool visible)
{
	return submit_command(&(struct sdl_command){SDL_COMMAND_SET_POINTER,
	    display->backend_state, {.enabled = visible}, .result = 0,
	    .done = false, .next = NULL});
}

static int
sdl_set_fullscreen(st_console_display_t *display, bool fullscreen)
{
	return submit_command(&(struct sdl_command){SDL_COMMAND_SET_FULLSCREEN,
	    display->backend_state, {.enabled = fullscreen}, .result = 0,
	    .done = false, .next = NULL});
}

static void
sdl_destroy(st_console_display_t *display)
{
	struct sdl_state *state = display->backend_state;

	(void)submit_command(&(struct sdl_command){
		.type = SDL_COMMAND_DESTROY,
		.state = state,
	});
	st_console_bitmap_presenter_destroy(state->presenter);
	st_condition_destroy(&state->completed);
	free(state);
	display->backend_state = NULL;
}

static int
sdl_attach(st_console_t *console)
{
	return st_console_bitmap_validate_geometry(console, INT_MAX);
}

static const struct st_console_backend_ops sdl_ops = {
	.attach = sdl_attach,
	.presents_pixels = true,
	.deferred_present = true,
	.present = sdl_present,
	.resize = sdl_resize,
	.set_title = sdl_set_title,
	.set_clipboard_text = sdl_set_clipboard,
	.get_clipboard_text = sdl_get_clipboard,
	.open_url = sdl_open_url,
	.set_pointer_visible = sdl_set_pointer,
	.set_fullscreen = sdl_set_fullscreen,
	.activate = sdl_activate,
	.destroy = sdl_destroy,
};

int
st_console_sdl_init(st_console_t *console)
{
	struct sdl_state *state;
	int status;

	status = st_console_bitmap_validate_geometry(console, INT_MAX);
	if (status != 0)
		return status;
	st_call_once(&manager_once, initialize_manager);
	if (!manager.synchronization_ready)
		return IO_ERROR_IO;
	state = calloc(1, sizeof(*state));
	if (state == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	state->console = console;
	state->window_geometry = (struct st_console_bitmap_geometry){
		console->pixels->width, console->pixels->height,
		console->aspect_width, console->aspect_height};
	state->console_display = console->display;
	uint32_t minimum_width;
	uint32_t minimum_height;

	status = st_console_aspect_correct_size(console->pixels->width,
	    console->pixels->height, console->aspect_width,
	    console->aspect_height, &minimum_width, &minimum_height);
	if (status != 0) {
		free(state);
		return status;
	}
	status = st_console_bitmap_presenter_create(&state->presenter, console);
	if (status != 0) {
		free(state);
		return status;
	}
	if (st_condition_init(&state->completed) != ST_THREAD_SUCCESS) {
		st_console_bitmap_presenter_destroy(state->presenter);
		free(state);
		return IO_ERROR_IO;
	}
	status = submit_command(&(struct sdl_command){
		.type = SDL_COMMAND_CREATE,
		.state = state,
		.data.size = {
			.width = console->display->initial_window_width,
			.height = console->display->initial_window_height,
			.source_width = console->pixels->width,
			.source_height = console->pixels->height,
			.minimum_width = minimum_width,
			.minimum_height = minimum_height,
		},
	});
	if (status != 0) {
		st_console_bitmap_presenter_destroy(state->presenter);
		st_condition_destroy(&state->completed);
		free(state);
		return status;
	}
	console->display->backend_state = state;
	console->display->backend_ops = &sdl_ops;
	return 0;
}

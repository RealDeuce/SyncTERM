/* Wayland bitmap console backend. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "console_internal.h"
#include "console_config.h"
#include "console_wayland_loader.h"

#include "dynamic_library.h"
#include "io_error.h"
#include "st_time.h"

#include "wayland-client.h"
#include "xdg-shell-client.h"
#include "viewporter-client.h"
#if ST_CONSOLE_HAVE_WAYLAND_ICON
#include "xdg-toplevel-icon-v1-client.h"
#endif
#include <xkbcommon/xkbcommon.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

enum command_type {
	CMD_PRESENT,
	CMD_ACTIVATE,
	CMD_RESIZE,
	CMD_TITLE,
	CMD_POINTER,
	CMD_FULLSCREEN,
	CMD_SET_CLIPBOARD,
	CMD_GET_CLIPBOARD,
	CMD_GET_SCALING,
	CMD_DESTROY
};

struct command {
	enum command_type type;
	union {
		st_console_t *console;
		struct {
			uint32_t width, height;
		} size;
		const char *text;
		char **text_result;
		double *scaling_result;
		bool enabled;
	} data;
	int result;
	bool done;
};

struct state;
struct offer {
	struct wl_data_offer *object;
	bool utf8;
	bool text;
	struct offer *next;
};

struct buffer {
	struct state *state;
	struct wl_buffer *object;
	uint32_t *pixels;
	size_t size;
	uint32_t width, height;
	struct st_console_rect pending;
	bool busy, valid, pending_valid;
};

typedef struct xkb_context *(*st_xkb_context_new_fn)(enum xkb_context_flags);
typedef void (*st_xkb_context_unref_fn)(struct xkb_context *);
typedef struct xkb_keymap *(*st_xkb_keymap_new_from_string_fn)(
	struct xkb_context *, const char *, enum xkb_keymap_format,
	enum xkb_keymap_compile_flags);
typedef void (*st_xkb_keymap_unref_fn)(struct xkb_keymap *);
typedef struct xkb_state *(*st_xkb_state_new_fn)(struct xkb_keymap *);
typedef void (*st_xkb_state_unref_fn)(struct xkb_state *);
typedef int (*st_xkb_state_update_mask_fn)(struct xkb_state *,
	xkb_mod_mask_t, xkb_mod_mask_t, xkb_mod_mask_t, xkb_layout_index_t,
	xkb_layout_index_t, xkb_layout_index_t);
typedef xkb_keysym_t (*st_xkb_state_key_get_one_sym_fn)(struct xkb_state *,
	xkb_keycode_t);
typedef uint32_t (*st_xkb_state_key_get_utf32_fn)(struct xkb_state *,
	xkb_keycode_t);
typedef int (*st_xkb_state_mod_name_is_active_fn)(struct xkb_state *,
	const char *, enum xkb_state_component);

struct xkb_api {
	struct xkb_context *(*context_new)(enum xkb_context_flags);
	void (*context_unref)(struct xkb_context *);
	struct xkb_keymap *(*keymap_new_from_string)(
	    struct xkb_context *,
	    const char *,
	    enum xkb_keymap_format,
	    enum xkb_keymap_compile_flags);
	void (*keymap_unref)(struct xkb_keymap *);
	struct xkb_state *(*state_new)(struct xkb_keymap *);
	void (*state_unref)(struct xkb_state *);
	int (*state_update_mask)(struct xkb_state *,
				 xkb_mod_mask_t,
				 xkb_mod_mask_t,
				 xkb_mod_mask_t,
				 xkb_layout_index_t,
				 xkb_layout_index_t,
				 xkb_layout_index_t);
	xkb_keysym_t (*state_key_get_one_sym)(struct xkb_state *,
					      xkb_keycode_t);
	uint32_t (*state_key_get_utf32)(struct xkb_state *, xkb_keycode_t);
	int (*state_mod_name_is_active)(struct xkb_state *,
					const char *,
					enum xkb_state_component);
};

struct state {
	/* Only the backend thread changes or dereferences this pointer. */
	st_console_t *console;
	st_console_display_t *console_display;
	struct st_console_bitmap_geometry window_geometry;
	st_thread_t thread;
	st_mutex_t mutex;
	st_condition_t changed;
	struct command *command;
	int wake[2];
	int start_status;
	bool started, stop, failed, configured, pointer_visible, close_posted;
	bool fullscreen, fullscreen_requested, maximized;
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_subcompositor *subcompositor;
	struct wl_shm *shm;
	struct wl_seat *seat;
	struct wl_data_device_manager *data_manager;
	struct wl_data_device *data_device;
	struct offer *pending_offers;
	struct offer *selection_offer;
	struct wl_data_source *selection_source;
	struct wl_keyboard *keyboard;
	struct wl_pointer *pointer;
	struct xdg_wm_base *wm_base;
	struct wp_viewporter *viewporter;
#if ST_CONSOLE_HAVE_WAYLAND_ICON
	struct xdg_toplevel_icon_manager_v1 *icon_manager;
#endif
	struct wl_surface *surface;
	struct wl_surface *video_surface;
	struct wl_surface *cursor_surface;
	struct wl_subsurface *video_subsurface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *toplevel;
	struct wp_viewport *viewport;
	struct buffer buffers[2], backgrounds[2], cursor_buffer;
	struct st_console_bitmap_presenter *presenter;
	struct st_console_bitmap_frame frame;
	const struct st_rgb_buffer *latest_pixels;
	uint32_t window_width, window_height, buttons, pointer_serial;
	uint32_t windowed_width, windowed_height;
	uint32_t bounds_width, bounds_height;
	uint32_t background_width, background_height;
	uint32_t input_serial;
	uint32_t repeat_key, repeat_next;
	int32_t repeat_rate, repeat_delay;
	char *clipboard;
	wl_fixed_t pointer_x, pointer_y;
	dynamic_library_t *xkb_library;
	struct xkb_api xkb;
	struct xkb_context *xkb_context;
	struct xkb_keymap *xkb_keymap;
	struct xkb_state *xkb_state;
};


static atomic_uint shm_sequence;

static uint32_t now_ms(void)
{
	uint64_t now = 0;
	(void)st_time_monotonic_ms(&now);
	return (uint32_t)now;
}

#define XKB_LOAD(state, member, name)                                          \
	(((state)->xkb.member = (st_xkb_ ## member ## _fn)                      \
	    dynamic_library_symbol((state)->xkb_library, name)) != NULL)

static int load_xkb(struct state *state)
{
	static const char *const names[] = {"xkbcommon"};
	if (dynamic_library_open(
		&state->xkb_library, names, 1, 0, DYNAMIC_LIBRARY_LOCAL) != 0)
		return IO_ERROR_NOT_FOUND;
	if (!XKB_LOAD(state, context_new, "xkb_context_new") ||
	    !XKB_LOAD(state, context_unref, "xkb_context_unref") ||
	    !XKB_LOAD(
		state, keymap_new_from_string, "xkb_keymap_new_from_string") ||
	    !XKB_LOAD(state, keymap_unref, "xkb_keymap_unref") ||
	    !XKB_LOAD(state, state_new, "xkb_state_new") ||
	    !XKB_LOAD(state, state_unref, "xkb_state_unref") ||
	    !XKB_LOAD(state, state_update_mask, "xkb_state_update_mask") ||
	    !XKB_LOAD(
		state, state_key_get_one_sym, "xkb_state_key_get_one_sym") ||
	    !XKB_LOAD(state, state_key_get_utf32, "xkb_state_key_get_utf32") ||
	    !XKB_LOAD(state,
		      state_mod_name_is_active,
		      "xkb_state_mod_name_is_active"))
		return IO_ERROR_NOT_SUPPORTED;
	state->xkb_context = state->xkb.context_new(XKB_CONTEXT_NO_FLAGS);
	return state->xkb_context == NULL ? IO_ERROR_IO : 0;
}

static int shm_file(size_t size)
{
	char name[64];
	int fd = -1;
	for (unsigned int tries = 0; tries < 100 && fd < 0; ++tries) {
		unsigned int sequence = atomic_fetch_add(&shm_sequence, 1);
		(void)snprintf(name,
			       sizeof(name),
			       "/st-console-%lu-%u",
			       (unsigned long)getpid(),
			       sequence);
		fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
		if (fd >= 0)
			(void)shm_unlink(name);
		else if (errno != EEXIST)
			return -1;
	}
	if (fd < 0 || size > INT32_MAX || ftruncate(fd, (off_t)size) != 0) {
		if (fd >= 0)
			(void)close(fd);
		return -1;
	}
	return fd;
}

static void buffer_release(void *data, struct wl_buffer *object)
{
	struct buffer *buffer = data;
	(void)object;
	buffer->busy = false;
	if (buffer->pending_valid && buffer->state->latest_pixels != NULL &&
	    buffer->width == buffer->state->latest_pixels->width &&
	    buffer->height == buffer->state->latest_pixels->height) {
		const struct st_rgb_buffer *pixels =
		    buffer->state->latest_pixels;
		for (uint32_t y = 0; y < buffer->pending.height; ++y) {
			size_t offset =
			    (size_t)(buffer->pending.y + y) * buffer->width +
			    buffer->pending.x;
			memcpy(&buffer->pixels[offset],
			       &pixels->pixels[offset],
			       (size_t)buffer->pending.width *
				   sizeof(*buffer->pixels));
		}
		buffer->pending_valid = false;
	} else if (buffer->pending_valid) {
		buffer->pending_valid = false;
		buffer->valid = false;
	}
}
static const struct wl_buffer_listener buffer_listener = {
    .release = buffer_release,
};

static void buffer_destroy(struct buffer *buffer)
{
	struct state *state = buffer->state;
	if (buffer->object != NULL)
		wl_buffer_destroy(buffer->object);
	if (buffer->pixels != NULL)
		(void)munmap(buffer->pixels, buffer->size);
	*buffer = (struct buffer){.state = state};
}

static int buffer_create(struct state *state,
			 struct buffer *buffer,
			 uint32_t width,
			 uint32_t height,
			 uint32_t format)
{
	struct wl_shm_pool *pool;
	size_t stride, size;
	int fd;
	if (width == 0 || height == 0 || width > INT32_MAX / 4 ||
	    (size_t)width * 4 > SIZE_MAX / height)
		return IO_ERROR_OUT_OF_RANGE;
	stride = (size_t)width * 4;
	size = stride * height;
	fd = shm_file(size);
	if (fd < 0)
		return IO_ERROR_IO;
	buffer->pixels =
	    mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (buffer->pixels == MAP_FAILED) {
		buffer->pixels = NULL;
		(void)close(fd);
		return IO_ERROR_IO;
	}
	buffer->state = state;
	buffer->size = size;
	buffer->width = width;
	buffer->height = height;
	pool = wl_shm_create_pool(state->shm, fd, (int32_t)size);
	(void)close(fd);
	if (pool == NULL) {
		buffer_destroy(buffer);
		return IO_ERROR_IO;
	}
	buffer->object = wl_shm_pool_create_buffer(
	    pool, 0, (int32_t)width, (int32_t)height, (int32_t)stride, format);
	wl_shm_pool_destroy(pool);
	if (buffer->object == NULL) {
		buffer_destroy(buffer);
		return IO_ERROR_IO;
	}
	if (wl_buffer_add_listener(buffer->object, &buffer_listener, buffer) != 0) {
		buffer_destroy(buffer);
		return IO_ERROR_IO;
	}
	return 0;
}

static int get_buffer_from(struct state *state,
			   struct buffer *buffers,
			   uint32_t width,
			   uint32_t height,
			   uint32_t format,
			   struct buffer **result)
{
	*result = NULL;
	for (size_t i = 0; i < 2; ++i) {
		struct buffer *buffer = &buffers[i];
		if (!buffer->busy) {
			if (buffer->width != width || buffer->height != height) {
				int status;

				buffer_destroy(buffer);
				status = buffer_create(
				    state, buffer, width, height, format);
				if (status != 0)
					return status;
			}
			*result = buffer;
			return 0;
		}
	}
	return IO_ERROR_WOULD_BLOCK;
}

static int get_buffer(struct state *state,
		      uint32_t width,
		      uint32_t height,
		      struct buffer **result)
{
	return get_buffer_from(
	    state, state->buffers, width, height, WL_SHM_FORMAT_XRGB8888, result);
}

static void
set_toplevel_icon(struct state *state)
{
#if ST_CONSOLE_HAVE_WAYLAND_ICON
	const struct st_console_icon *source = state->console_display->config.icon;
	struct xdg_toplevel_icon_v1 *icon;
	struct buffer buffer = {.state = state};

	if (source == NULL || source->width != source->height
	    || state->icon_manager == NULL)
		return;
	if (buffer_create(state, &buffer, source->width, source->height,
	    WL_SHM_FORMAT_ARGB8888) != 0)
		return;
	for (uint32_t y = 0; y < source->height; ++y) {
		const unsigned char *row =
		    &source->pixels[(size_t)y * source->stride];

		for (uint32_t x = 0; x < source->width; ++x) {
			const unsigned char *pixel = &row[(size_t)x * 4];
			uint32_t alpha = pixel[3];
			uint32_t red = ((uint32_t)pixel[0] * alpha + 127) / 255;
			uint32_t green = ((uint32_t)pixel[1] * alpha + 127) / 255;
			uint32_t blue = ((uint32_t)pixel[2] * alpha + 127) / 255;

			buffer.pixels[(size_t)y * source->width + x]
			    = (alpha << 24) | (red << 16) | (green << 8) | blue;
		}
	}
	icon = xdg_toplevel_icon_manager_v1_create_icon(state->icon_manager);
	if (icon != NULL) {
		xdg_toplevel_icon_v1_add_buffer(icon, buffer.object, 1);
		xdg_toplevel_icon_manager_v1_set_icon(state->icon_manager,
		    state->toplevel, icon);
		xdg_toplevel_icon_v1_destroy(icon);
	}
	buffer_destroy(&buffer);
#else
	(void)state;
#endif
}

static void add_pending(struct buffer *buffer,
			const struct st_console_rect *rect)
{
	uint32_t right = rect->x + rect->width;
	uint32_t bottom = rect->y + rect->height;
	if (!buffer->pending_valid) {
		buffer->pending = *rect;
		buffer->pending_valid = true;
		return;
	}
	if (rect->x < buffer->pending.x) {
		buffer->pending.width += buffer->pending.x - rect->x;
		buffer->pending.x = rect->x;
	}
	if (rect->y < buffer->pending.y) {
		buffer->pending.height += buffer->pending.y - rect->y;
		buffer->pending.y = rect->y;
	}
	if (right > buffer->pending.x + buffer->pending.width)
		buffer->pending.width = right - buffer->pending.x;
	if (bottom > buffer->pending.y + buffer->pending.height)
		buffer->pending.height = bottom - buffer->pending.y;
}

static int update_buffers(void *context,
			  const struct st_rgb_buffer *pixels,
			  const struct st_console_rect *rectangle,
			  const void *image_identity, uint64_t generation)
{
	struct state *state = context;
	(void)image_identity;
	(void)generation;
	state->latest_pixels = pixels;
	for (size_t i = 0; i < 2; ++i) {
		struct buffer *buffer = &state->buffers[i];
		if (!buffer->valid || buffer->width != pixels->width ||
		    buffer->height != pixels->height)
			continue;
		if (buffer->busy)
			add_pending(buffer, rectangle);
		else {
			for (uint32_t y = 0; y < rectangle->height; ++y) {
				size_t offset =
				    (size_t)(rectangle->y + y) * pixels->width +
				    rectangle->x;
				memcpy(&buffer->pixels[offset],
				       &pixels->pixels[offset],
				       (size_t)rectangle->width *
					   sizeof(*buffer->pixels));
			}
		}
	}
	wl_surface_damage_buffer(state->video_surface,
				 (int32_t)rectangle->x,
				 (int32_t)rectangle->y,
				 (int32_t)rectangle->width,
				 (int32_t)rectangle->height);
	return 0;
}

static int draw(struct state *state, const st_console_t *console, bool forced)
{
	struct buffer *buffer;
	struct buffer *background = NULL;
	bool background_changed;
	bool full;
	int status = st_console_bitmap_presenter_frame(state->presenter,
						       console,
						       state->window_width,
						       state->window_height,
						       now_ms(),
						       update_buffers,
						       state,
						       &state->frame);
	if (status != 0)
		return status;
	if (!state->frame.changed && !forced)
		return 0;
	status = get_buffer(state, state->frame.pixels->width,
	    state->frame.pixels->height, &buffer);
	if (status == IO_ERROR_WOULD_BLOCK)
		return 0;
	if (status != 0)
		return status;
	background_changed = state->background_width != state->window_width
	    || state->background_height != state->window_height;
	if (background_changed) {
		status = get_buffer_from(state, state->backgrounds,
		    state->window_width, state->window_height,
		    WL_SHM_FORMAT_XRGB8888, &background);
		if (status == IO_ERROR_WOULD_BLOCK)
			return 0;
		if (status != 0)
			return status;
	}
	state->latest_pixels = state->frame.pixels;
	full = !buffer->valid;
	if (full) {
		memcpy(
		    buffer->pixels, state->frame.pixels->pixels, buffer->size);
		buffer->valid = true;
		buffer->pending_valid = false;
	}
	if (state->viewport != NULL) {
		if (state->frame.scaling_mode == ST_CONSOLE_SCALING_EXTERNAL)
			wp_viewport_set_destination(
			    state->viewport,
			    (int32_t)state->frame.destination.width,
			    (int32_t)state->frame.destination.height);
		else
			wp_viewport_set_destination(state->viewport, -1, -1);
	}
	wl_subsurface_set_position(state->video_subsurface,
				   (int32_t)state->frame.destination.x,
				   (int32_t)state->frame.destination.y);
	wl_surface_attach(state->video_surface, buffer->object, 0, 0);
	if (full || forced)
		wl_surface_damage_buffer(
		    state->video_surface, 0, 0, INT32_MAX, INT32_MAX);
	wl_surface_commit(state->video_surface);
	buffer->busy = true;
	if (background_changed) {
		if (!background->valid) {
			memset(background->pixels, 0, background->size);
			background->valid = true;
		}
		wl_surface_attach(state->surface, background->object, 0, 0);
		wl_surface_damage_buffer(
		    state->surface, 0, 0, INT32_MAX, INT32_MAX);
		background->busy = true;
		state->background_width = state->window_width;
		state->background_height = state->window_height;
	}
	wl_surface_commit(state->surface);
	return wl_display_flush(state->display) < 0 && errno != EAGAIN
		   ? IO_ERROR_IO
		   : 0;
}

static uint16_t key_from_sym(xkb_keysym_t sym)
{
	if (sym >= XKB_KEY_F1 && sym <= XKB_KEY_F12)
		return (uint16_t)(ST_CONSOLE_KEY_F1 + sym - XKB_KEY_F1);
	switch (sym) {
	case XKB_KEY_BackSpace:
		return 8;
	case XKB_KEY_Tab:
	case XKB_KEY_ISO_Left_Tab:
		return 9;
	case XKB_KEY_Return:
	case XKB_KEY_KP_Enter:
		return 13;
	case XKB_KEY_Escape:
		return 27;
	case XKB_KEY_Home:
	case XKB_KEY_KP_Home:
		return ST_CONSOLE_KEY_HOME;
	case XKB_KEY_Left:
	case XKB_KEY_KP_Left:
		return ST_CONSOLE_KEY_LEFT;
	case XKB_KEY_Up:
	case XKB_KEY_KP_Up:
		return ST_CONSOLE_KEY_UP;
	case XKB_KEY_Right:
	case XKB_KEY_KP_Right:
		return ST_CONSOLE_KEY_RIGHT;
	case XKB_KEY_Down:
	case XKB_KEY_KP_Down:
		return ST_CONSOLE_KEY_DOWN;
	case XKB_KEY_Page_Up:
	case XKB_KEY_KP_Page_Up:
		return ST_CONSOLE_KEY_PAGE_UP;
	case XKB_KEY_Page_Down:
	case XKB_KEY_KP_Page_Down:
		return ST_CONSOLE_KEY_PAGE_DOWN;
	case XKB_KEY_End:
	case XKB_KEY_KP_End:
		return ST_CONSOLE_KEY_END;
	case XKB_KEY_Insert:
	case XKB_KEY_KP_Insert:
		return ST_CONSOLE_KEY_INSERT;
	case XKB_KEY_Delete:
	case XKB_KEY_KP_Delete:
		return ST_CONSOLE_KEY_DELETE;
	default:
		return 0;
	}
}

static uint32_t modifiers(struct state *state)
{
	uint32_t result = 0;
#define ACTIVE(name, bit)                                                      \
	do {                                                                   \
		if (state->xkb.state_mod_name_is_active(                       \
			state->xkb_state, name, XKB_STATE_MODS_EFFECTIVE) > 0) \
			result |= bit;                                         \
	} while (0)
	ACTIVE(XKB_MOD_NAME_SHIFT, ST_CONSOLE_MODIFIER_SHIFT);
	ACTIVE(XKB_MOD_NAME_CTRL, ST_CONSOLE_MODIFIER_CONTROL);
	ACTIVE(XKB_MOD_NAME_ALT, ST_CONSOLE_MODIFIER_ALT);
	ACTIVE(XKB_MOD_NAME_LOGO, ST_CONSOLE_MODIFIER_GUI);
	ACTIVE(XKB_MOD_NAME_CAPS, ST_CONSOLE_MODIFIER_CAPS_LOCK);
	ACTIVE(XKB_MOD_NAME_NUM, ST_CONSOLE_MODIFIER_NUM_LOCK);
#undef ACTIVE
	return result;
}

static void keyboard_keymap(void *data,
			    struct wl_keyboard *keyboard,
			    uint32_t format,
			    int32_t fd,
			    uint32_t size)
{
	struct state *state = data;
	char *map;
	(void)keyboard;
	if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || size == 0) {
		(void)close(fd);
		return;
	}
	map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	(void)close(fd);
	if (map == MAP_FAILED)
		return;
	if (state->xkb_state != NULL)
		state->xkb.state_unref(state->xkb_state);
	if (state->xkb_keymap != NULL)
		state->xkb.keymap_unref(state->xkb_keymap);
	state->xkb_keymap =
	    state->xkb.keymap_new_from_string(state->xkb_context,
					      map,
					      XKB_KEYMAP_FORMAT_TEXT_V1,
					      XKB_KEYMAP_COMPILE_NO_FLAGS);
	(void)munmap(map, size);
	state->xkb_state = state->xkb_keymap == NULL
			       ? NULL
			       : state->xkb.state_new(state->xkb_keymap);
}

static void keyboard_enter(void *data,
			   struct wl_keyboard *keyboard,
			   uint32_t serial,
			   struct wl_surface *surface,
			   struct wl_array *keys)
{
	struct state *state = data;
	const struct st_console_event event = {
	    .type = ST_CONSOLE_EVENT_FOCUS,
	    .data.focus.focused = true,
	};
	(void)keyboard;
	(void)surface;
	(void)keys;
	state->input_serial = serial;
	(void)st_console_display_post_event(state->console_display, &event);
}

static void keyboard_leave(void *data,
			   struct wl_keyboard *keyboard,
			   uint32_t serial,
			   struct wl_surface *surface)
{
	struct state *state = data;
	const struct st_console_event event = {
	    .type = ST_CONSOLE_EVENT_FOCUS,
	    .data.focus.focused = false,
	};
	(void)keyboard;
	(void)serial;
	(void)surface;
	state->repeat_key = 0;
	(void)st_console_display_post_event(state->console_display, &event);
}

static void
emit_key(struct state *state, uint32_t key, bool pressed, bool repeat)
{
	struct st_console_event event = {
	    .type = ST_CONSOLE_EVENT_PHYSICAL_KEY,
	    .data.physical_key = {.evdev = (uint16_t)key,
				  .pressed = pressed,
				  .repeat = repeat},
	};
	xkb_keycode_t code = key + 8;
	xkb_keysym_t sym;
	uint16_t translated;
	uint32_t text, mods;
	if (key > UINT16_MAX)
		return;
	mods = state->xkb_state == NULL ? 0 : modifiers(state);
	event.data.physical_key.modifiers = mods;
	(void)st_console_display_post_event(state->console_display, &event);
	if (!pressed || state->xkb_state == NULL)
		return;
	sym = state->xkb.state_key_get_one_sym(state->xkb_state, code);
	if ((mods & ST_CONSOLE_MODIFIER_ALT) != 0
	    && !repeat && (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter)) {
		if (state->fullscreen || state->fullscreen_requested) {
			state->fullscreen_requested = false;
			xdg_toplevel_unset_fullscreen(state->toplevel);
		}
		else {
			state->windowed_width = state->window_width;
			state->windowed_height = state->window_height;
			state->fullscreen_requested = true;
			xdg_toplevel_set_fullscreen(state->toplevel, NULL);
		}
		wl_surface_commit(state->surface);
		return;
	}
	if ((mods & ST_CONSOLE_MODIFIER_ALT) != 0 && !state->fullscreen
	    && !state->fullscreen_requested && !state->maximized
	    && (sym == XKB_KEY_Left || sym == XKB_KEY_KP_Left
	    || sym == XKB_KEY_Right || sym == XKB_KEY_KP_Right)) {
		uint32_t width;
		uint32_t height;
		bool larger = sym == XKB_KEY_Right || sym == XKB_KEY_KP_Right;

		if (st_console_snap_geometry_size(&state->window_geometry,
		    state->window_width, state->window_height, larger,
		    state->bounds_width, state->bounds_height,
		    &width, &height) == 0
		    && (width != state->window_width
		    || height != state->window_height)) {
			state->window_width = width;
			state->window_height = height;
			state->windowed_width = width;
			state->windowed_height = height;
			(void)draw(state, state->console, true);
		}
		return;
	}
	translated = key_from_sym(sym);
	if (translated != 0) {
		event =
		    (struct st_console_event){.type = ST_CONSOLE_EVENT_KEY_CODE,
					      .data.key = {
						      .code = translated,
						      .modifiers = mods}};
		(void)st_console_display_post_event(state->console_display, &event);
	}
	if (translated == 0
	    && (mods & (ST_CONSOLE_MODIFIER_CONTROL | ST_CONSOLE_MODIFIER_ALT))
	    == 0) {
		text = state->xkb.state_key_get_utf32(state->xkb_state, code);
		if (text != 0) {
			event = (struct st_console_event){
			    .type = ST_CONSOLE_EVENT_TEXT,
			    .data.text = {
				    .codepoint = text,
				    .modifiers = mods}};
			(void)st_console_display_post_event(state->console_display, &event);
		}
	}
}

static void keyboard_key(void *data,
			 struct wl_keyboard *keyboard,
			 uint32_t serial,
			 uint32_t time,
			 uint32_t key,
			 uint32_t pressed)
{
	struct state *state = data;

	(void)keyboard;
	(void)time;
	state->input_serial = serial;
	emit_key(state, key, pressed != 0, false);
	if (pressed != 0 && state->repeat_rate > 0) {
		state->repeat_key = key + 1;
		state->repeat_next = now_ms() + (uint32_t)state->repeat_delay;
	} else if (state->repeat_key == key + 1)
		state->repeat_key = 0;
}

static void keyboard_modifiers(void *data,
			       struct wl_keyboard *keyboard,
			       uint32_t serial,
			       uint32_t depressed,
			       uint32_t latched,
			       uint32_t locked,
			       uint32_t group)
{
	struct state *state = data;
	(void)keyboard;
	(void)serial;
	if (state->xkb_state != NULL)
		(void)state->xkb.state_update_mask(
		    state->xkb_state, depressed, latched, locked, 0, 0, group);
}
static void
keyboard_repeat(void *d, struct wl_keyboard *k, int32_t r, int32_t delay)
{
	struct state *s = d;
	(void)k;
	s->repeat_rate = r;
	s->repeat_delay = delay < 0 ? 0 : delay;
	s->repeat_key = 0;
}
static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers,
    .repeat_info = keyboard_repeat,
};

static int set_pointer(struct state *s, bool visible);

static void pointer_enter(void *data,
			  struct wl_pointer *pointer,
			  uint32_t serial,
			  struct wl_surface *surface,
			  wl_fixed_t x,
			  wl_fixed_t y)
{
	struct state *state = data;
	(void)pointer;
	(void)surface;
	(void)x;
	(void)y;
	state->pointer_serial = serial;
	state->input_serial = serial;
	(void)set_pointer(state, state->pointer_visible);
}
static void
pointer_leave(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *w)
{
	struct state *state = d;
	state->pointer_serial = 0;
	(void)p;
	(void)s;
	(void)w;
}

static void pointer_motion(void *data,
			   struct wl_pointer *pointer,
			   uint32_t time,
			   wl_fixed_t x,
			   wl_fixed_t y)
{
	struct state *state = data;
	struct st_console_event event = {
	    .type = ST_CONSOLE_EVENT_MOUSE,
	    .data.mouse = {.action = ST_CONSOLE_MOUSE_MOVE,
			   .buttons = state->buttons,
			   .modifiers = state->xkb_state == NULL
			       ? 0 : modifiers(state)}};
	(void)pointer;
	(void)time;
	if (state->frame.destination.width == 0)
		return;
	state->pointer_x = x;
	state->pointer_y = y;
	st_console_bitmap_mouse_position(&state->frame, wl_fixed_to_int(x),
	    wl_fixed_to_int(y), &event);
	(void)st_console_display_post_event(state->console_display, &event);
}

static void pointer_button(void *data,
			   struct wl_pointer *pointer,
			   uint32_t serial,
			   uint32_t time,
			   uint32_t button,
			   uint32_t pressed)
{
	struct state *state = data;
	uint32_t mask = button == 0x110	  ? ST_CONSOLE_MOUSE_BUTTON_LEFT
			: button == 0x111 ? ST_CONSOLE_MOUSE_BUTTON_RIGHT
			: button == 0x112 ? ST_CONSOLE_MOUSE_BUTTON_MIDDLE
			: button == 0x113 ? ST_CONSOLE_MOUSE_BUTTON_X1
			: button == 0x114 ? ST_CONSOLE_MOUSE_BUTTON_X2
					  : 0;
	struct st_console_event event = {.type = ST_CONSOLE_EVENT_MOUSE,
					 .data.mouse.action =
					     ST_CONSOLE_MOUSE_BUTTON};
	(void)pointer;
	(void)time;
	state->input_serial = serial;
	if (mask == 0)
		return;
	if (pressed && mask == ST_CONSOLE_MOUSE_BUTTON_LEFT
	    && state->xkb_state != NULL
	    && (modifiers(state) & ST_CONSOLE_MODIFIER_ALT) != 0) {
		xdg_toplevel_move(state->toplevel, state->seat, serial);
		return;
	}
	if (pressed)
		state->buttons |= mask;
	else
		state->buttons &= ~mask;
	event.data.mouse.buttons = state->buttons;
	event.data.mouse.modifiers = state->xkb_state == NULL
	    ? 0 : modifiers(state);
	st_console_bitmap_mouse_position(&state->frame,
	    wl_fixed_to_int(state->pointer_x),
	    wl_fixed_to_int(state->pointer_y), &event);
	(void)st_console_display_post_event(state->console_display, &event);
}

static void pointer_axis(void *data,
			 struct wl_pointer *pointer,
			 uint32_t time,
			 uint32_t axis,
			 wl_fixed_t value)
{
	struct state *state = data;
	struct st_console_event event = {
	    .type = ST_CONSOLE_EVENT_MOUSE,
	    .data.mouse = {.action = ST_CONSOLE_MOUSE_WHEEL,
			   .buttons = state->buttons,
			   .modifiers = state->xkb_state == NULL
			       ? 0 : modifiers(state)}};
	int32_t direction = wl_fixed_to_int(value);
	int32_t amount;
	(void)pointer;
	(void)time;
	if (direction == 0)
		return;
	amount = direction > 0 ? -1 : 1;
	if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
		event.data.mouse.wheel_y = amount;
	else
		event.data.mouse.wheel_x = amount;
	st_console_bitmap_mouse_position(&state->frame,
	    wl_fixed_to_int(state->pointer_x), wl_fixed_to_int(state->pointer_y),
	    &event);
	(void)st_console_display_post_event(state->console_display, &event);
}
static void pointer_frame(void *d, struct wl_pointer *p)
{
	(void)d;
	(void)p;
}
static void pointer_axis_source(void *d, struct wl_pointer *p, uint32_t s)
{
	(void)d;
	(void)p;
	(void)s;
}
static void
pointer_axis_stop(void *d, struct wl_pointer *p, uint32_t t, uint32_t a)
{
	(void)d;
	(void)p;
	(void)t;
	(void)a;
}
static void
pointer_axis_discrete(void *d, struct wl_pointer *p, uint32_t a, int32_t v)
{
	(void)d;
	(void)p;
	(void)a;
	(void)v;
}
static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
    .frame = pointer_frame,
    .axis_source = pointer_axis_source,
    .axis_stop = pointer_axis_stop,
    .axis_discrete = pointer_axis_discrete,
};

static void
offer_mime(void *data, struct wl_data_offer *offer, const char *mime)
{
	struct offer *value = data;

	if (offer != value->object)
		return;
	if (strcmp(mime, "text/plain;charset=utf-8") == 0)
		value->utf8 = true;
	else if (strcmp(mime, "text/plain") == 0)
		value->text = true;
}
static void offer_actions(void *d, struct wl_data_offer *o, uint32_t a)
{
	(void)d;
	(void)o;
	(void)a;
}
static void offer_action(void *d, struct wl_data_offer *o, uint32_t a)
{
	(void)d;
	(void)o;
	(void)a;
}
static const struct wl_data_offer_listener offer_listener = {
    .offer = offer_mime,
    .source_actions = offer_actions,
    .action = offer_action,
};

static void source_target(void *d, struct wl_data_source *s, const char *m)
{
	(void)d;
	(void)s;
	(void)m;
}
static void source_send(void *data,
			struct wl_data_source *source,
			const char *mime,
			int32_t fd)
{
	struct state *state = data;
	sigset_t blocked;
	size_t sent = 0,
	       length = state->clipboard == NULL ? 0 : strlen(state->clipboard);
	(void)source;
	(void)mime;
	if (sigemptyset(&blocked) != 0 || sigaddset(&blocked, SIGPIPE) != 0
	    || pthread_sigmask(SIG_BLOCK, &blocked, NULL) != 0) {
		(void)close(fd);
		return;
	}
	while (sent < length) {
		ssize_t n = write(fd, state->clipboard + sent, length - sent);
		if (n > 0)
			sent += (size_t)n;
		else if (n < 0 && errno == EINTR)
			continue;
		else
			break;
	}
	(void)close(fd);
}
static void source_cancelled(void *data, struct wl_data_source *source)
{
	struct state *state = data;
	if (state->selection_source == source)
		state->selection_source = NULL;
	wl_data_source_destroy(source);
}
static void source_drop(void *d, struct wl_data_source *s)
{
	(void)d;
	(void)s;
}
static void source_finished(void *d, struct wl_data_source *s)
{
	(void)d;
	(void)s;
}
static void source_action(void *d, struct wl_data_source *s, uint32_t a)
{
	(void)d;
	(void)s;
	(void)a;
}
static const struct wl_data_source_listener source_listener = {
    .target = source_target,
    .send = source_send,
    .cancelled = source_cancelled,
    .dnd_drop_performed = source_drop,
    .dnd_finished = source_finished,
    .action = source_action,
};

static void device_offer(void *data,
			 struct wl_data_device *device,
			 struct wl_data_offer *offer)
{
	struct state *state = data;
	struct offer *value = calloc(1, sizeof(*value));

	(void)device;
	if (value == NULL) {
		wl_data_offer_destroy(offer);
		return;
	}
	value->object = offer;
	value->next = state->pending_offers;
	state->pending_offers = value;
	(void)wl_data_offer_add_listener(offer, &offer_listener, value);
}
static void device_enter(void *d,
			 struct wl_data_device *v,
			 uint32_t s,
			 struct wl_surface *w,
			 wl_fixed_t x,
			 wl_fixed_t y,
			 struct wl_data_offer *o)
{
	struct state *state = d;
	(void)v;
	(void)s;
	(void)w;
	(void)x;
	(void)y;
	struct offer **link = &state->pending_offers;
	while (*link != NULL && (*link)->object != o)
		link = &(*link)->next;
	if (*link != NULL) {
		struct offer *value = *link;

		*link = value->next;
		wl_data_offer_destroy(value->object);
		free(value);
	}
}
static void device_leave(void *d, struct wl_data_device *v)
{
	(void)d;
	(void)v;
}
static void device_motion(
    void *d, struct wl_data_device *v, uint32_t t, wl_fixed_t x, wl_fixed_t y)
{
	(void)d;
	(void)v;
	(void)t;
	(void)x;
	(void)y;
}
static void device_drop(void *d, struct wl_data_device *v)
{
	(void)d;
	(void)v;
}
static void device_selection(void *data,
			     struct wl_data_device *device,
			     struct wl_data_offer *offer)
{
	struct state *state = data;
	(void)device;
	struct offer **link = &state->pending_offers;

	if (state->selection_offer != NULL &&
	    state->selection_offer->object == offer)
		return;
	if (state->selection_offer != NULL &&
	    state->selection_offer->object != offer) {
		wl_data_offer_destroy(state->selection_offer->object);
		free(state->selection_offer);
	}
	state->selection_offer = NULL;
	while (*link != NULL && (*link)->object != offer)
		link = &(*link)->next;
	if (*link != NULL) {
		state->selection_offer = *link;
		*link = (*link)->next;
		state->selection_offer->next = NULL;
	}
}
static const struct wl_data_device_listener device_listener = {
    .data_offer = device_offer,
    .enter = device_enter,
    .leave = device_leave,
    .motion = device_motion,
    .drop = device_drop,
    .selection = device_selection,
};

static char *copy_text(const char *text)
{
	size_t length = strlen(text);
	char *copy = malloc(length + 1);

	if (copy != NULL)
		memcpy(copy, text, length + 1);
	return copy;
}

static int set_clipboard(struct state *state, const char *text)
{
	struct wl_data_source *source;
	char *copy;
	if (!state->data_device || !state->data_manager ||
	    state->input_serial == 0)
		return IO_ERROR_NOT_SUPPORTED;
	copy = copy_text(text);
	if (!copy)
		return IO_ERROR_OUT_OF_MEMORY;
	source = wl_data_device_manager_create_data_source(state->data_manager);
	if (!source) {
		free(copy);
		return IO_ERROR_IO;
	}
	(void)wl_data_source_add_listener(source, &source_listener, state);
	wl_data_source_offer(source, "text/plain;charset=utf-8");
	wl_data_source_offer(source, "text/plain");
	free(state->clipboard);
	state->clipboard = copy;
	if (state->selection_source)
		wl_data_source_destroy(state->selection_source);
	state->selection_source = source;
	wl_data_device_set_selection(
	    state->data_device, source, state->input_serial);
	return 0;
}

static int get_clipboard(struct state *state, char **result)
{
	int pipes[2];
	struct pollfd fds[2];
	char *text = NULL;
	size_t used = 0, capacity = 0;
	const char *mime =
	    state->selection_offer != NULL && state->selection_offer->utf8
		? "text/plain;charset=utf-8"
	    : state->selection_offer != NULL && state->selection_offer->text
		? "text/plain"
		: NULL;
	uint32_t start = now_ms();
	if (state->selection_source && state->clipboard) {
		*result = copy_text(state->clipboard);
		return *result ? 0 : IO_ERROR_OUT_OF_MEMORY;
	}
	if (!state->selection_offer || !mime)
		return IO_ERROR_NOT_FOUND;
	if (pipe(pipes) != 0)
		return IO_ERROR_IO;
	wl_data_offer_receive(state->selection_offer->object, mime, pipes[1]);
	(void)close(pipes[1]);
	(void)wl_display_flush(state->display);
	fds[0] = (struct pollfd){.fd = pipes[0], .events = POLLIN};
	fds[1] = (struct pollfd){.fd = wl_display_get_fd(state->display),
				 .events = POLLIN};
	for (;;) {
		if (used == capacity) {
			size_t next = capacity ? capacity * 2 : 4096;
			char *p;
			if (next < capacity || next == SIZE_MAX) {
				free(text);
				(void)close(pipes[0]);
				return IO_ERROR_OUT_OF_MEMORY;
			}
			p = realloc(text, next);
			if (!p) {
				free(text);
				(void)close(pipes[0]);
				return IO_ERROR_OUT_OF_MEMORY;
			}
			text = p;
			capacity = next;
		}
		int ready = poll(fds, 2, 25);
		if (ready < 0 && errno != EINTR) {
			free(text);
			(void)close(pipes[0]);
			return IO_ERROR_IO;
		}
		if (fds[1].revents & POLLIN)
			(void)wl_display_dispatch(state->display);
		if (fds[0].revents & (POLLIN | POLLHUP)) {
			ssize_t n =
			    read(pipes[0], text + used, capacity - used);
			if (n > 0)
				used += (size_t)n;
			else if (n == 0)
				break;
			else if (errno != EINTR) {
				free(text);
				(void)close(pipes[0]);
				return IO_ERROR_IO;
			}
		}
		if (now_ms() - start > 2000) {
			free(text);
			(void)close(pipes[0]);
			return IO_ERROR_TIMED_OUT;
		}
	}
	(void)close(pipes[0]);
	if (used == capacity) {
		char *p = realloc(text, capacity + 1);
		if (!p) {
			free(text);
			return IO_ERROR_OUT_OF_MEMORY;
		}
		text = p;
	}
	text[used] = '\0';
	*result = text;
	return 0;
}

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps)
{
	struct state *state = data;
	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) != 0 &&
	    state->keyboard == NULL) {
		state->keyboard = wl_seat_get_keyboard(seat);
		(void)wl_keyboard_add_listener(
		    state->keyboard, &keyboard_listener, state);
	}
	if ((caps & WL_SEAT_CAPABILITY_POINTER) != 0 &&
	    state->pointer == NULL) {
		state->pointer = wl_seat_get_pointer(seat);
		(void)wl_pointer_add_listener(
		    state->pointer, &pointer_listener, state);
	}
}
static void seat_name(void *d, struct wl_seat *s, const char *n)
{
	(void)d;
	(void)s;
	(void)n;
}
static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

static void wm_ping(void *d, struct xdg_wm_base *w, uint32_t s)
{
	(void)d;
	xdg_wm_base_pong(w, s);
}
static const struct xdg_wm_base_listener wm_listener = {.ping = wm_ping};
static void
surface_configure(void *data, struct xdg_surface *surface, uint32_t serial)
{
	((struct state *)data)->configured = true;
	xdg_surface_ack_configure(surface, serial);
}
static const struct xdg_surface_listener surface_listener = {
    .configure = surface_configure,
};
static void top_configure(void *data,
			  struct xdg_toplevel *top,
			  int32_t w,
			  int32_t h,
			  struct wl_array *states)
{
	struct state *s = data;
	uint32_t *value;
	bool fullscreen = false;
	bool maximized = false;

	(void)top;
	wl_array_for_each(value, states)
		if (*value == XDG_TOPLEVEL_STATE_FULLSCREEN)
			fullscreen = true;
		else if (*value == XDG_TOPLEVEL_STATE_MAXIMIZED)
			maximized = true;
	if ((fullscreen && !s->fullscreen) || (maximized && !s->maximized)) {
		s->windowed_width = s->window_width;
		s->windowed_height = s->window_height;
	}
	s->fullscreen = fullscreen;
	s->maximized = maximized;
	if (w > 0)
		s->window_width = w;
	else if (!fullscreen && !maximized)
		s->window_width = s->windowed_width;
	if (h > 0)
		s->window_height = h;
	else if (!fullscreen && !maximized)
		s->window_height = s->windowed_height;
	if (!fullscreen && !maximized) {
		s->windowed_width = s->window_width;
		s->windowed_height = s->window_height;
	}
	st_console_display_request_repaint(s->console_display);
}
static void post_close(struct state *s)
{
	const struct st_console_event e = {.type = ST_CONSOLE_EVENT_CLOSE};

	if (s->close_posted)
		return;
	s->close_posted = true;
	(void)st_console_display_post_event(s->console_display, &e);
}
static void top_close(void *data, struct xdg_toplevel *top)
{
	struct state *s = data;
	(void)top;
	post_close(s);
}
static void top_bounds(void *d, struct xdg_toplevel *t, int32_t w, int32_t h)
{
	struct state *s = d;

	(void)t;
	s->bounds_width = w > 0 ? (uint32_t)w : 0;
	s->bounds_height = h > 0 ? (uint32_t)h : 0;
}
static void top_caps(void *d, struct xdg_toplevel *t, struct wl_array *a)
{
	(void)d;
	(void)t;
	(void)a;
}
static const struct xdg_toplevel_listener top_listener = {
    .configure = top_configure,
    .close = top_close,
    .configure_bounds = top_bounds,
    .wm_capabilities = top_caps};

static void registry_global(void *data,
			    struct wl_registry *registry,
			    uint32_t name,
			    const char *interface,
			    uint32_t version)
{
	struct state *s = data;
#define BIND(member, iface, maximum)                                           \
	s->member =                                                            \
	    wl_registry_bind(registry,                                         \
			     name,                                             \
			     &iface##_interface,                               \
			     version < (maximum) ? version : (maximum))
	if (strcmp(interface, wl_compositor_interface.name) == 0)
		BIND(compositor, wl_compositor, 4);
	else if (strcmp(interface, wl_subcompositor_interface.name) == 0)
		BIND(subcompositor, wl_subcompositor, 1);
	else if (strcmp(interface, wl_shm_interface.name) == 0)
		BIND(shm, wl_shm, 1);
	else if (strcmp(interface, wl_seat_interface.name) == 0)
		BIND(seat, wl_seat, 7);
	else if (strcmp(interface, wl_data_device_manager_interface.name) == 0)
		BIND(data_manager, wl_data_device_manager, 3);
	else if (strcmp(interface, xdg_wm_base_interface.name) == 0)
		BIND(wm_base, xdg_wm_base, 6);
	else if (strcmp(interface, wp_viewporter_interface.name) == 0)
		BIND(viewporter, wp_viewporter, 1);
#if ST_CONSOLE_HAVE_WAYLAND_ICON
	else if (strcmp(interface,
	    xdg_toplevel_icon_manager_v1_interface.name) == 0)
		BIND(icon_manager, xdg_toplevel_icon_manager_v1, 1);
#endif
#undef BIND
}
static void registry_remove(void *d, struct wl_registry *r, uint32_t n)
{
	(void)d;
	(void)r;
	(void)n;
}
static const struct wl_registry_listener registry_listener = {
    .global = registry_global, .global_remove = registry_remove};

static void objects_destroy(struct state *s)
{
	if (s->xkb_state)
		s->xkb.state_unref(s->xkb_state);
	if (s->xkb_keymap)
		s->xkb.keymap_unref(s->xkb_keymap);
	if (s->xkb_context)
		s->xkb.context_unref(s->xkb_context);
	for (size_t i = 0; i < 2; ++i)
		buffer_destroy(&s->buffers[i]);
	for (size_t i = 0; i < 2; ++i)
		buffer_destroy(&s->backgrounds[i]);
	buffer_destroy(&s->cursor_buffer);
	if (s->pointer)
		wl_pointer_destroy(s->pointer);
	if (s->keyboard)
		wl_keyboard_destroy(s->keyboard);
	while (s->pending_offers != NULL) {
		struct offer *offer = s->pending_offers;

		s->pending_offers = offer->next;
		wl_data_offer_destroy(offer->object);
		free(offer);
	}
	if (s->selection_offer) {
		wl_data_offer_destroy(s->selection_offer->object);
		free(s->selection_offer);
	}
	if (s->selection_source)
		wl_data_source_destroy(s->selection_source);
	if (s->data_device)
		wl_data_device_destroy(s->data_device);
	if (s->cursor_surface)
		wl_surface_destroy(s->cursor_surface);
	if (s->viewport)
		wp_viewport_destroy(s->viewport);
	if (s->toplevel)
		xdg_toplevel_destroy(s->toplevel);
	if (s->xdg_surface)
		xdg_surface_destroy(s->xdg_surface);
	if (s->video_subsurface)
		wl_subsurface_destroy(s->video_subsurface);
	if (s->video_surface)
		wl_surface_destroy(s->video_surface);
	if (s->surface)
		wl_surface_destroy(s->surface);
	if (s->seat)
		wl_seat_destroy(s->seat);
	if (s->data_manager)
		wl_data_device_manager_destroy(s->data_manager);
	if (s->viewporter)
		wp_viewporter_destroy(s->viewporter);
#if ST_CONSOLE_HAVE_WAYLAND_ICON
	if (s->icon_manager)
		xdg_toplevel_icon_manager_v1_destroy(s->icon_manager);
#endif
	if (s->wm_base)
		xdg_wm_base_destroy(s->wm_base);
	if (s->shm)
		wl_shm_destroy(s->shm);
	if (s->compositor)
		wl_compositor_destroy(s->compositor);
	if (s->subcompositor)
		wl_subcompositor_destroy(s->subcompositor);
	if (s->registry)
		wl_registry_destroy(s->registry);
	if (s->display)
		wl_display_disconnect(s->display);
	(void)dynamic_library_close(s->xkb_library);
	free(s->clipboard);
}

static int objects_create(struct state *s)
{
	s->display = wl_display_connect(NULL);
	if (!s->display)
		return IO_ERROR_NOT_FOUND;
	s->registry = wl_display_get_registry(s->display);
	if (!s->registry)
		return IO_ERROR_IO;
	(void)wl_registry_add_listener(s->registry, &registry_listener, s);
	if (wl_display_roundtrip(s->display) < 0 || !s->compositor ||
	    !s->subcompositor || !s->shm || !s->wm_base)
		return IO_ERROR_NOT_SUPPORTED;
	if (load_xkb(s) != 0)
		return IO_ERROR_NOT_SUPPORTED;
	(void)xdg_wm_base_add_listener(s->wm_base, &wm_listener, s);
	if (s->seat)
		(void)wl_seat_add_listener(s->seat, &seat_listener, s);
	if (s->seat && s->data_manager) {
		s->data_device = wl_data_device_manager_get_data_device(
		    s->data_manager, s->seat);
		if (s->data_device)
			(void)wl_data_device_add_listener(
			    s->data_device, &device_listener, s);
	}
	s->surface = wl_compositor_create_surface(s->compositor);
	s->video_surface = wl_compositor_create_surface(s->compositor);
	s->cursor_surface = wl_compositor_create_surface(s->compositor);
	if (!s->surface || !s->video_surface || !s->cursor_surface)
		return IO_ERROR_IO;
	s->video_subsurface = wl_subcompositor_get_subsurface(
	    s->subcompositor, s->video_surface, s->surface);
	if (!s->video_subsurface)
		return IO_ERROR_IO;
	s->xdg_surface = xdg_wm_base_get_xdg_surface(s->wm_base, s->surface);
	if (!s->xdg_surface)
		return IO_ERROR_IO;
	s->toplevel = xdg_surface_get_toplevel(s->xdg_surface);
	if (!s->toplevel)
		return IO_ERROR_IO;
	(void)xdg_surface_add_listener(s->xdg_surface, &surface_listener, s);
	(void)xdg_toplevel_add_listener(s->toplevel, &top_listener, s);
	xdg_toplevel_set_title(s->toplevel, "SyncTERM");
	xdg_toplevel_set_app_id(s->toplevel, "syncterm");
	set_toplevel_icon(s);
	if (s->viewporter)
		s->viewport =
		    wp_viewporter_get_viewport(s->viewporter, s->video_surface);
	st_console_bitmap_presenter_set_external_scaling_available(
	    s->presenter, s->viewport != NULL);
	uint32_t minimum_width;
	uint32_t minimum_height;

	s->window_width = s->console->display->initial_window_width;
	s->window_height = s->console->display->initial_window_height;
	s->windowed_width = s->window_width;
	s->windowed_height = s->window_height;
	if (st_console_aspect_correct_size(s->console->pixels->width,
	    s->console->pixels->height, s->console->aspect_width,
	    s->console->aspect_height, &minimum_width,
	    &minimum_height) != 0 || s->window_width > INT32_MAX
	    || s->window_height > INT32_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	xdg_toplevel_set_min_size(s->toplevel, (int32_t)minimum_width,
	    (int32_t)minimum_height);
	wl_surface_commit(s->surface);
	while (!s->configured)
		if (wl_display_dispatch(s->display) < 0)
			return IO_ERROR_IO;
	return draw(s, s->console, true);
}

static int set_pointer(struct state *s, bool visible)
{
	/* A cursor request only applies while the pointer is over our surface. */
	s->pointer_visible = visible;
	if (!s->pointer || s->pointer_serial == 0)
		return 0;
	if (!visible)
		wl_pointer_set_cursor(
		    s->pointer, s->pointer_serial, NULL, 0, 0);
	else {
		if (!s->cursor_buffer.object) {
			if (buffer_create(s,
					  &s->cursor_buffer,
					  16,
					  16,
					  WL_SHM_FORMAT_ARGB8888) != 0)
				return IO_ERROR_IO;
			memset(
			    s->cursor_buffer.pixels, 0, s->cursor_buffer.size);
			for (uint32_t y = 0; y < 14; ++y)
				for (uint32_t x = 0; x <= y / 2; ++x)
					s->cursor_buffer.pixels[y * 16 + x] =
					    x == 0 || x == y / 2 ? 0xff000000
								 : 0xffffffff;
			wl_surface_attach(
			    s->cursor_surface, s->cursor_buffer.object, 0, 0);
			wl_surface_damage_buffer(
			    s->cursor_surface, 0, 0, 16, 16);
			wl_surface_commit(s->cursor_surface);
		}
		wl_pointer_set_cursor(
		    s->pointer, s->pointer_serial, s->cursor_surface, 0, 0);
	}
	s->pointer_visible = visible;
	return 0;
}

static int process(struct state *s, struct command *c)
{
	switch (c->type) {
	case CMD_ACTIVATE:
		s->console = c->data.console;
		return 0;
	case CMD_PRESENT:
		return draw(s, s->console, false);
	case CMD_RESIZE:
		{
		struct st_console_bitmap_geometry next = {
			c->data.size.width, c->data.size.height,
			s->console->aspect_width, s->console->aspect_height};
		uint32_t width;
		uint32_t height;
		uint32_t minimum_width;
		uint32_t minimum_height;
		uint32_t current_width = s->fullscreen || s->fullscreen_requested
		    || s->maximized
		    ? s->windowed_width : s->window_width;
		uint32_t current_height = s->fullscreen || s->fullscreen_requested
		    || s->maximized
		    ? s->windowed_height : s->window_height;

		if (st_console_preserve_geometry_scale(&s->window_geometry, &next,
		    current_width,
		    current_height, &width, &height,
		    &minimum_width, &minimum_height) != 0
		    || width > INT32_MAX || height > INT32_MAX)
			return IO_ERROR_OUT_OF_RANGE;
		s->windowed_width = width;
		s->windowed_height = height;
		s->window_geometry = next;
		if (!s->fullscreen && !s->fullscreen_requested && !s->maximized) {
			s->window_width = width;
			s->window_height = height;
		}
		xdg_toplevel_set_min_size(s->toplevel,
		    (int32_t)minimum_width, (int32_t)minimum_height);
		wl_surface_commit(s->surface);
		return 0;
		}
	case CMD_TITLE:
		xdg_toplevel_set_title(s->toplevel, c->data.text);
		wl_surface_commit(s->surface);
		return 0;
	case CMD_POINTER:
		return set_pointer(s, c->data.enabled);
	case CMD_FULLSCREEN:
		if (c->data.enabled) {
			if (!s->fullscreen && !s->fullscreen_requested) {
				s->windowed_width = s->window_width;
				s->windowed_height = s->window_height;
			}
			s->fullscreen_requested = true;
			xdg_toplevel_set_fullscreen(s->toplevel, NULL);
		}
		else {
			s->fullscreen_requested = false;
			xdg_toplevel_unset_fullscreen(s->toplevel);
		}
		wl_surface_commit(s->surface);
		return 0;
	case CMD_GET_SCALING:
		return st_console_bitmap_geometry_scaling(&s->window_geometry,
		    s->window_width, s->window_height,
		    c->data.scaling_result);
	case CMD_SET_CLIPBOARD:
		return set_clipboard(s, c->data.text);
	case CMD_GET_CLIPBOARD:
		return get_clipboard(s, c->data.text_result);
	case CMD_DESTROY:
		s->stop = true;
		return 0;
	}
	return IO_ERROR_INVALID_ARGUMENT;
}

static void pending(struct state *s)
{
	char b;
	struct command *c;
	int result;
	while (read(s->wake[0], &b, 1) < 0 && errno == EINTR)
		;
	(void)st_mutex_lock(&s->mutex);
	c = s->command;
	(void)st_mutex_unlock(&s->mutex);
	if (!c)
		return;
	result = process(s, c);
	(void)st_mutex_lock(&s->mutex);
	c->result = result;
	c->done = true;
	s->command = NULL;
	(void)st_condition_broadcast(&s->changed);
	(void)st_mutex_unlock(&s->mutex);
}

static bool
terminal_poll_event(short events)
{
	return (events & (POLLHUP | POLLERR | POLLNVAL)) != 0;
}

static int worker_loop(struct state *s)
{
	struct pollfd fds[2];

	fds[0] = (struct pollfd){.fd = wl_display_get_fd(s->display),
				 .events = POLLIN};
	fds[1] = (struct pollfd){.fd = s->wake[0], .events = POLLIN};
	while (!s->stop) {
		int ready;

		if (wl_display_dispatch_pending(s->display) < 0)
			return IO_ERROR_IO;
		if (s->repeat_key && s->repeat_rate > 0 &&
		    (int32_t)(now_ms() - s->repeat_next) >= 0) {
			uint32_t interval = 1000U / (uint32_t)s->repeat_rate;

			if (interval == 0)
				interval = 1;
			emit_key(s, s->repeat_key - 1, true, true);
			s->repeat_next += interval;
		}
		if (wl_display_flush(s->display) < 0 && errno != EAGAIN)
			return IO_ERROR_IO;
		do
			ready = poll(fds, 2, 20);
		while (ready < 0 && errno == EINTR);
		if (ready < 0)
			return IO_ERROR_IO;
		if ((fds[1].revents & POLLIN) != 0)
			pending(s);
		if (s->stop)
			return 0;
		if (terminal_poll_event(fds[1].revents))
			return IO_ERROR_IO;
		if ((fds[0].revents & POLLIN) != 0
		    && wl_display_dispatch(s->display) < 0)
			return IO_ERROR_IO;
		if (terminal_poll_event(fds[0].revents))
			return IO_ERROR_IO;
	}
	return 0;
}

static void worker_finish(struct state *s, int status)
{
	if (status != 0 && !s->stop)
		post_close(s);
	(void)st_mutex_lock(&s->mutex);
	s->failed = true;
	if (s->command) {
		s->command->result = IO_ERROR_IO;
		s->command->done = true;
		s->command = NULL;
	}
	(void)st_condition_broadcast(&s->changed);
	(void)st_mutex_unlock(&s->mutex);
}

static int worker(void *argument)
{
	struct state *s = argument;
	int status = objects_create(s);

	(void)st_mutex_lock(&s->mutex);
	s->start_status = status;
	s->started = true;
	(void)st_condition_broadcast(&s->changed);
	(void)st_mutex_unlock(&s->mutex);
	if (status) {
		objects_destroy(s);
		return 1;
	}
	status = worker_loop(s);
	worker_finish(s, status);
	objects_destroy(s);
	return status == 0 ? 0 : 1;
}

static int submit(struct state *s, struct command *c)
{
	char b = 0;
	ssize_t n;
	int status = st_mutex_lock(&s->mutex);
	if (status != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	while (s->command && !s->failed)
		(void)st_condition_wait(&s->changed, &s->mutex);
	if (s->failed) {
		(void)st_mutex_unlock(&s->mutex);
		return IO_ERROR_IO;
	}
	s->command = c;
	do
		n = write(s->wake[1], &b, 1);
	while (n < 0 && errno == EINTR);
	if (n != 1) {
		s->command = NULL;
		(void)st_mutex_unlock(&s->mutex);
		return IO_ERROR_IO;
	}
	while (!c->done)
		(void)st_condition_wait(&s->changed, &s->mutex);
	status = c->result;
	(void)st_mutex_unlock(&s->mutex);
	return status;
}

static int
activate(st_console_t *console)
{
	return submit(console->display->backend_state, &(struct command){
		.type = CMD_ACTIVATE,
		.data.console = console,
	});
}

static int present(st_console_t *c)
{
	return submit(c->display->backend_state, &(struct command){.type = CMD_PRESENT});
}
static int resize(st_console_t *c, uint32_t cols, uint32_t rows)
{
	uint32_t width;
	uint32_t height;

	if (cols > (UINT32_MAX - c->pixel_extra_width) / c->cell_width ||
	    rows > (UINT32_MAX - c->pixel_extra_height) / c->cell_height)
		return IO_ERROR_OUT_OF_RANGE;
	width = cols * c->cell_width + c->pixel_extra_width;
	height = rows * c->cell_height + c->pixel_extra_height;
	if (width > INT32_MAX || height > INT32_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	return submit(
	    c->display->backend_state,
	    &(struct command){
		.type = CMD_RESIZE,
		.data.size = {width, height}});
}
static int title(st_console_display_t *display, const char *t)
{
	return submit(display->backend_state,
		      &(struct command){.type = CMD_TITLE, .data.text = t});
}
static int pointer(st_console_display_t *display, bool v)
{
	return submit(
	    display->backend_state,
	    &(struct command){.type = CMD_POINTER, .data.enabled = v});
}
static int fullscreen(st_console_display_t *display, bool v)
{
	return submit(
	    display->backend_state,
	    &(struct command){.type = CMD_FULLSCREEN, .data.enabled = v});
}
static int scaling(st_console_display_t *display, double *r)
{
	return submit(display->backend_state,
	    &(struct command){.type = CMD_GET_SCALING,
		.data.scaling_result = r});
}
static int clipboard_set(st_console_display_t *display, const char *t)
{
	return submit(
	    display->backend_state,
	    &(struct command){.type = CMD_SET_CLIPBOARD, .data.text = t});
}
static int clipboard_get(st_console_display_t *display, char **r)
{
	return submit(display->backend_state,
		      &(struct command){.type = CMD_GET_CLIPBOARD,
					.data.text_result = r});
}
static void destroy(st_console_display_t *display)
{
	struct state *s = display->backend_state;
	(void)submit(s, &(struct command){.type = CMD_DESTROY});
	(void)st_thread_join(s->thread, NULL);
	st_console_bitmap_presenter_destroy(s->presenter);
	(void)close(s->wake[0]);
	(void)close(s->wake[1]);
	st_condition_destroy(&s->changed);
	st_mutex_destroy(&s->mutex);
	free(s);
	display->backend_state = NULL;
}
static int attach(st_console_t *console)
{
	return st_console_bitmap_validate_geometry(console, INT32_MAX);
}

static const struct st_console_backend_ops ops = {
	.attach = attach,
    .presents_pixels = true,
    .deferred_present = true,
    .animated_present = true,
    .present = present,
    .resize = resize,
    .set_title = title,
    .set_clipboard_text = clipboard_set,
    .get_clipboard_text = clipboard_get,
    .set_pointer_visible = pointer,
    .set_fullscreen = fullscreen,
	.get_scaling = scaling,
	.activate = activate,
    .destroy = destroy};

int st_console_wayland_init(st_console_t *c)
{
	struct state *s;
	int status;
	status = st_console_bitmap_validate_geometry(c, INT32_MAX);
	if (status != 0)
		return status;
	status = st_wayland_loader_init();
	if (status)
		return status;
	s = calloc(1, sizeof(*s));
	if (!s)
		return IO_ERROR_OUT_OF_MEMORY;
	s->console = c;
	s->window_geometry = (struct st_console_bitmap_geometry){
		c->pixels->width, c->pixels->height,
		c->aspect_width, c->aspect_height};
	s->wake[0] = s->wake[1] = -1;
	s->console_display = c->display;
	s->pointer_visible = true;
	for (size_t i = 0; i < 2; ++i) {
		s->buffers[i].state = s;
		s->backgrounds[i].state = s;
	}
	s->cursor_buffer.state = s;
	if (st_mutex_init(&s->mutex, ST_MUTEX_PLAIN) != ST_THREAD_SUCCESS)
		status = IO_ERROR_IO;
	else if (st_condition_init(&s->changed) != ST_THREAD_SUCCESS) {
		st_mutex_destroy(&s->mutex);
		status = IO_ERROR_IO;
	} else if (pipe(s->wake) != 0) {
		st_condition_destroy(&s->changed);
		st_mutex_destroy(&s->mutex);
		status = IO_ERROR_IO;
	} else
		status = st_console_bitmap_presenter_create(&s->presenter, c);
	if (!status &&
	    st_thread_create(&s->thread, worker, s) != ST_THREAD_SUCCESS)
		status = IO_ERROR_IO;
	if (!status) {
		(void)st_mutex_lock(&s->mutex);
		while (!s->started)
			(void)st_condition_wait(&s->changed, &s->mutex);
		status = s->start_status;
		(void)st_mutex_unlock(&s->mutex);
		if (status)
			(void)st_thread_join(s->thread, NULL);
	}
	if (status) {
		st_console_bitmap_presenter_destroy(s->presenter);
		if (s->wake[0] >= 0) {
			(void)close(s->wake[0]);
			(void)close(s->wake[1]);
			st_condition_destroy(&s->changed);
			st_mutex_destroy(&s->mutex);
		}
		free(s);
		return status;
	}
	c->display->backend_state = s;
	c->display->backend_ops = &ops;
	return 0;
}

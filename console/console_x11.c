/* Runtime-loaded X11 bitmap console backend. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "console_internal.h"
#include "console_config.h"
#include "console_x11_keys.h"
#include "console_x11_pixels.h"

#include "dynamic_library.h"
#include "io_error.h"
#include "st_time.h"
#include "st_utf8.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#if ST_CONSOLE_HAVE_XRANDR
#include <X11/extensions/Xrandr.h>
#endif
#if ST_CONSOLE_HAVE_XINERAMA
#include <X11/extensions/Xinerama.h>
#endif
#if ST_CONSOLE_HAVE_XRENDER
#include <X11/extensions/Xrender.h>
#endif
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef Status (*x11_init_threads_fn)(void);
typedef Display *(*x11_open_display_fn)(const char *);
typedef int (*x11_close_display_fn)(Display *);
typedef int (*x11_default_screen_fn)(Display *);
typedef Window (*x11_root_window_fn)(Display *, int);
typedef Visual *(*x11_default_visual_fn)(Display *, int);
typedef int (*x11_default_depth_fn)(Display *, int);
typedef unsigned long (*x11_black_pixel_fn)(Display *, int);
typedef int (*x11_connection_number_fn)(Display *);
typedef Window (*x11_create_simple_window_fn)(Display *, Window, int, int,
	unsigned int, unsigned int, unsigned int, unsigned long, unsigned long);
typedef int (*x11_destroy_window_fn)(Display *, Window);
typedef int (*x11_map_window_fn)(Display *, Window);
typedef int (*x11_resize_window_fn)(Display *, Window, unsigned int,
	unsigned int);
typedef Status (*x11_get_window_attributes_fn)(Display *, Window,
	XWindowAttributes *);
typedef int (*x11_move_window_fn)(Display *, Window, int, int);
typedef Bool (*x11_translate_coordinates_fn)(Display *, Window, Window,
	int, int, int *, int *, Window *);
typedef void (*x11_set_wm_normal_hints_fn)(Display *, Window, XSizeHints *);
typedef int (*x11_select_input_fn)(Display *, Window, long);
typedef GC (*x11_create_gc_fn)(Display *, Drawable, unsigned long,
	XGCValues *);
typedef int (*x11_free_gc_fn)(Display *, GC);
typedef int (*x11_set_foreground_fn)(Display *, GC, unsigned long);
typedef int (*x11_fill_rectangle_fn)(Display *, Drawable, GC, int, int,
	unsigned int, unsigned int);
typedef XImage *(*x11_create_image_fn)(Display *, Visual *, unsigned int,
	int, int, char *, unsigned int, unsigned int, int, int);
typedef int (*x11_put_image_fn)(Display *, Drawable, GC, XImage *, int, int,
	int, int, unsigned int, unsigned int);
typedef int (*x11_flush_fn)(Display *);
typedef int (*x11_sync_fn)(Display *, Bool);
typedef int (*x11_pending_fn)(Display *);
typedef int (*x11_next_event_fn)(Display *, XEvent *);
typedef int (*x11_peek_event_fn)(Display *, XEvent *);
typedef KeySym (*x11_lookup_keysym_fn)(XKeyEvent *, int);
typedef int (*x11_lookup_string_fn)(XKeyEvent *, char *, int, KeySym *,
	XComposeStatus *);
typedef char *(*x11_set_locale_modifiers_fn)(const char *);
typedef XIM (*x11_open_im_fn)(Display *, XrmDatabase, char *, char *);
typedef Status (*x11_close_im_fn)(XIM);
typedef XIC (*x11_create_ic_fn)(XIM, ...);
typedef void (*x11_destroy_ic_fn)(XIC);
typedef void (*x11_set_ic_focus_fn)(XIC);
typedef void (*x11_unset_ic_focus_fn)(XIC);
typedef int (*x11_utf8_lookup_string_fn)(XIC, XKeyPressedEvent *, char *,
	int, KeySym *, Status *);
typedef Bool (*x11_filter_event_fn)(XEvent *, Window);
typedef KeyCode (*x11_keysym_to_keycode_fn)(Display *, KeySym);
typedef Atom (*x11_intern_atom_fn)(Display *, const char *, Bool);
typedef Status (*x11_set_wm_protocols_fn)(Display *, Window, Atom *, int);
typedef int (*x11_set_wm_hints_fn)(Display *, Window, XWMHints *);
typedef int (*x11_store_name_fn)(Display *, Window, const char *);
typedef int (*x11_change_property_fn)(Display *, Window, Atom, Atom, int,
	int, const unsigned char *, int);
typedef int (*x11_delete_property_fn)(Display *, Window, Atom);
typedef int (*x11_send_event_fn)(Display *, Window, Bool, long, XEvent *);
typedef Window (*x11_get_selection_owner_fn)(Display *, Atom);
typedef int (*x11_set_selection_owner_fn)(Display *, Atom, Window, Time);
typedef int (*x11_convert_selection_fn)(Display *, Atom, Atom, Atom, Window,
	Time);
typedef int (*x11_get_window_property_fn)(Display *, Window, Atom, long,
	long, Bool, Atom, Atom *, int *, unsigned long *, unsigned long *,
	unsigned char **);
typedef int (*x11_free_memory_fn)(void *);
typedef Pixmap (*x11_create_bitmap_from_data_fn)(Display *, Drawable,
	const char *, unsigned int, unsigned int);
typedef Cursor (*x11_create_pixmap_cursor_fn)(Display *, Pixmap, Pixmap,
	XColor *, XColor *, unsigned int, unsigned int);
typedef Cursor (*x11_create_font_cursor_fn)(Display *, unsigned int);
typedef int (*x11_define_cursor_fn)(Display *, Window, Cursor);
typedef int (*x11_undefine_cursor_fn)(Display *, Window);
typedef int (*x11_free_cursor_fn)(Display *, Cursor);
typedef int (*x11_free_pixmap_fn)(Display *, Pixmap);
typedef Pixmap (*x11_create_pixmap_fn)(Display *, Drawable, unsigned int,
	unsigned int, unsigned int);
typedef Bool (*x11_xkb_lookup_keysym_fn)(Display *, KeyCode, unsigned int,
	unsigned int *, KeySym *);
typedef XkbDescPtr (*x11_xkb_get_keyboard_fn)(Display *, unsigned int,
	unsigned int);
typedef void (*x11_xkb_free_keyboard_fn)(XkbDescPtr, unsigned int, Bool);
#if ST_CONSOLE_HAVE_XRANDR
typedef XRRMonitorInfo *(*x11_xrr_get_monitors_fn)(Display *, Window, Bool,
	int *);
typedef void (*x11_xrr_free_monitors_fn)(XRRMonitorInfo *);
#endif
#if ST_CONSOLE_HAVE_XINERAMA
typedef Bool (*x11_xinerama_is_active_fn)(Display *);
typedef XineramaScreenInfo *(*x11_xinerama_query_screens_fn)(Display *, int *);
#endif
#if ST_CONSOLE_HAVE_XRENDER
typedef Status (*x11_render_query_version_fn)(Display *, int *, int *);
typedef XRenderPictFormat *(*x11_render_find_visual_format_fn)(Display *,
	const Visual *);
typedef Picture (*x11_render_create_picture_fn)(Display *, Drawable,
	const XRenderPictFormat *, unsigned long,
	const XRenderPictureAttributes *);
typedef void (*x11_render_free_picture_fn)(Display *, Picture);
typedef void (*x11_render_set_transform_fn)(Display *, Picture,
	XTransform *);
typedef void (*x11_render_set_filter_fn)(Display *, Picture, const char *,
	XFixed *, int);
typedef void (*x11_render_composite_fn)(Display *, int, Picture, Picture,
	Picture, int, int, int, int, int, int, unsigned int, unsigned int);
#endif

struct x11_api {
	Status (*init_threads)(void);
	Display *(*open_display)(const char *);
	int (*close_display)(Display *);
	int (*default_screen)(Display *);
	Window (*root_window)(Display *, int);
	Visual *(*default_visual)(Display *, int);
	int (*default_depth)(Display *, int);
	unsigned long (*black_pixel)(Display *, int);
	int (*connection_number)(Display *);
	Window (*create_simple_window)(Display *, Window, int, int,
	    unsigned int, unsigned int, unsigned int, unsigned long,
	    unsigned long);
	int (*destroy_window)(Display *, Window);
	int (*map_window)(Display *, Window);
	int (*resize_window)(Display *, Window, unsigned int, unsigned int);
	Status (*get_window_attributes)(Display *, Window, XWindowAttributes *);
	int (*move_window)(Display *, Window, int, int);
	Bool (*translate_coordinates)(Display *, Window, Window, int, int,
	    int *, int *, Window *);
	void (*set_wm_normal_hints)(Display *, Window, XSizeHints *);
	int (*select_input)(Display *, Window, long);
	GC (*create_gc)(Display *, Drawable, unsigned long, XGCValues *);
	int (*free_gc)(Display *, GC);
	int (*set_foreground)(Display *, GC, unsigned long);
	int (*fill_rectangle)(Display *, Drawable, GC, int, int,
	    unsigned int, unsigned int);
	XImage *(*create_image)(Display *, Visual *, unsigned int, int, int,
	    char *, unsigned int, unsigned int, int, int);
	int (*put_image)(Display *, Drawable, GC, XImage *, int, int, int, int,
	    unsigned int, unsigned int);
	int (*flush)(Display *);
	int (*sync)(Display *, Bool);
	int (*pending)(Display *);
	int (*next_event)(Display *, XEvent *);
	int (*peek_event)(Display *, XEvent *);
	KeySym (*lookup_keysym)(XKeyEvent *, int);
	int (*lookup_string)(XKeyEvent *, char *, int, KeySym *, XComposeStatus *);
	char *(*set_locale_modifiers)(const char *);
	XIM (*open_im)(Display *, XrmDatabase, char *, char *);
	Status (*close_im)(XIM);
	XIC (*create_ic)(XIM, ...);
	void (*destroy_ic)(XIC);
	void (*set_ic_focus)(XIC);
	void (*unset_ic_focus)(XIC);
	int (*utf8_lookup_string)(XIC, XKeyPressedEvent *, char *, int,
	    KeySym *, Status *);
	Bool (*filter_event)(XEvent *, Window);
	KeyCode (*keysym_to_keycode)(Display *, KeySym);
	Atom (*intern_atom)(Display *, const char *, Bool);
	Status (*set_wm_protocols)(Display *, Window, Atom *, int);
	int (*set_wm_hints)(Display *, Window, XWMHints *);
	int (*store_name)(Display *, Window, const char *);
	int (*change_property)(Display *, Window, Atom, Atom, int, int,
	    const unsigned char *, int);
	int (*delete_property)(Display *, Window, Atom);
	int (*send_event)(Display *, Window, Bool, long, XEvent *);
	Window (*get_selection_owner)(Display *, Atom);
	int (*set_selection_owner)(Display *, Atom, Window, Time);
	int (*convert_selection)(Display *, Atom, Atom, Atom, Window, Time);
	int (*get_window_property)(Display *, Window, Atom, long, long, Bool,
	    Atom, Atom *, int *, unsigned long *, unsigned long *,
	    unsigned char **);
	int (*free_memory)(void *);
	Pixmap (*create_bitmap_from_data)(Display *, Drawable, const char *,
	    unsigned int, unsigned int);
	Cursor (*create_pixmap_cursor)(Display *, Pixmap, Pixmap, XColor *,
	    XColor *, unsigned int, unsigned int);
	Cursor (*create_font_cursor)(Display *, unsigned int);
	int (*define_cursor)(Display *, Window, Cursor);
	int (*undefine_cursor)(Display *, Window);
	int (*free_cursor)(Display *, Cursor);
	int (*free_pixmap)(Display *, Pixmap);
	Pixmap (*create_pixmap)(Display *, Drawable, unsigned int, unsigned int,
	    unsigned int);
	Bool (*xkb_lookup_keysym)(Display *, KeyCode, unsigned int,
	    unsigned int *, KeySym *);
	XkbDescPtr (*xkb_get_keyboard)(Display *, unsigned int, unsigned int);
	void (*xkb_free_keyboard)(XkbDescPtr, unsigned int, Bool);
#if ST_CONSOLE_HAVE_XRENDER
	Status (*render_query_version)(Display *, int *, int *);
	XRenderPictFormat *(*render_find_visual_format)(Display *, const Visual *);
	Picture (*render_create_picture)(Display *, Drawable,
	    const XRenderPictFormat *, unsigned long,
	    const XRenderPictureAttributes *);
	void (*render_free_picture)(Display *, Picture);
	void (*render_set_transform)(Display *, Picture, XTransform *);
	void (*render_set_filter)(Display *, Picture, const char *, XFixed *, int);
	void (*render_composite)(Display *, int, Picture, Picture, Picture,
	    int, int, int, int, int, int, unsigned int, unsigned int);
#endif
};

enum x11_command_type {
	X11_COMMAND_PRESENT,
	X11_COMMAND_ACTIVATE,
	X11_COMMAND_RESIZE,
	X11_COMMAND_SET_TITLE,
	X11_COMMAND_SET_CLIPBOARD,
	X11_COMMAND_GET_CLIPBOARD,
	X11_COMMAND_SET_POINTER,
	X11_COMMAND_SET_POINTER_SHAPE,
	X11_COMMAND_SET_FULLSCREEN,
	X11_COMMAND_GET_SCALING,
	X11_COMMAND_DESTROY,
};

struct x11_command {
	enum x11_command_type type;
	union {
		st_console_t *console;
		struct {
			uint32_t width;
			uint32_t height;
		} size;
		const char *text;
		char **text_result;
		double *scaling_result;
		bool enabled;
		enum st_console_pointer_shape pointer_shape;
	} data;
	int result;
	bool done;
};

struct x11_state {
	/* Only the backend thread changes or dereferences this pointer. */
	st_console_t *console;
	st_console_display_t *console_display;
	struct st_console_bitmap_geometry window_geometry;
	dynamic_library_t *library;
#if ST_CONSOLE_HAVE_XRANDR
	dynamic_library_t *xrandr_library;
	x11_xrr_get_monitors_fn xrr_get_monitors;
	x11_xrr_free_monitors_fn xrr_free_monitors;
#endif
#if ST_CONSOLE_HAVE_XINERAMA
	dynamic_library_t *xinerama_library;
	x11_xinerama_is_active_fn xinerama_is_active;
	x11_xinerama_query_screens_fn xinerama_query_screens;
#endif
#if ST_CONSOLE_HAVE_XRENDER
	dynamic_library_t *render_library;
#endif
	struct x11_api api;
	st_thread_t thread;
	st_mutex_t mutex;
	st_condition_t changed;
	struct x11_command *command;
	int wake_pipe[2];
	int start_status;
	bool started;
	bool stop;
	bool failed;
	Display *display;
	XIM input_method;
	XIC input_context;
	int screen;
	Window root;
	Window window;
	Visual *visual;
	int depth;
	GC gc;
	XImage *image;
	struct st_console_bitmap_presenter *presenter;
	struct st_console_bitmap_frame frame;
	struct st_console_rect damage;
	struct st_console_rect old_destination;
	uint32_t window_width;
	uint32_t window_height;
	uint32_t windowed_width;
	uint32_t windowed_height;
	uint32_t mouse_buttons;
	unsigned long red_values[256];
	unsigned long green_values[256];
	unsigned long blue_values[256];
	struct st_x11_pixel_writer pixel_writer;
	bool damage_valid;
	bool destination_valid;
	bool pointer_visible;
	bool fullscreen;
	bool maximized;
	bool evdev_keycodes;
	bool key_down[256];
	uint16_t keycode_evdev[256];
	Cursor hidden_cursor;
	Cursor pointer_cursors[ST_CONSOLE_POINTER_LINK + 1];
	enum st_console_pointer_shape pointer_shape;
	Pixmap icon_pixmap;
	Pixmap icon_mask;
	Atom wm_delete;
	Atom wm_ping;
	Atom net_wm_name;
	Atom net_wm_icon_name;
	Atom net_wm_icon;
	Atom net_wm_pid;
	Atom net_wm_window_type;
	Atom net_wm_window_type_normal;
	Atom utf8_string;
	Atom clipboard_atom;
	Atom selection_atom;
	Atom targets_atom;
	Atom clipboard_property;
	Atom net_wm_state;
	Atom net_frame_extents;
	Atom net_wm_desktop;
	Atom net_workarea;
	Atom net_wm_fullscreen;
	Atom net_wm_maximized_vertical;
	Atom net_wm_maximized_horizontal;
	char *clipboard;
	bool sommelier;
#if ST_CONSOLE_HAVE_XRENDER
	bool render_available;
	Pixmap render_pixmap;
	Picture render_source;
	Picture render_destination;
	uint32_t render_width;
	uint32_t render_height;
#endif
};


struct x11_manager {
	st_mutex_t mutex;
	bool ready;
	bool threads_checked;
	bool threads_available;
};

static struct x11_manager x11_manager;
static st_once_flag_t x11_manager_once = ST_ONCE_FLAG_INIT;

static int set_fullscreen(struct x11_state *state, bool enabled);

static bool
cardinal_property(struct x11_state *state, Window window, Atom property,
	long offset, long length, long **values, unsigned long *count)
{
	Atom actual_type;
	int actual_format;
	unsigned long remaining;
	unsigned char *data = NULL;
	int status;

	if (property == None)
		return false;
	status = state->api.get_window_property(state->display, window,
	    property, offset, length, False, XA_CARDINAL, &actual_type,
	    &actual_format, count, &remaining, &data);
	if (status != Success || actual_type != XA_CARDINAL
	    || actual_format != 32 || data == NULL) {
		if (data != NULL)
			(void)state->api.free_memory(data);
		return false;
	}
	*values = (long *)data;
	return true;
}

struct x11_geometry {
	long x;
	long y;
	long width;
	long height;
};

#if ST_CONSOLE_HAVE_XRANDR || ST_CONSOLE_HAVE_XINERAMA
static unsigned long
distance_from_rectangle(long x, long y,
	const struct x11_geometry *rectangle)
{
	unsigned long horizontal = 0;
	unsigned long vertical = 0;
	unsigned long offset;

	if (x < rectangle->x)
		horizontal = (unsigned long)rectangle->x - (unsigned long)x;
	else {
		offset = (unsigned long)x - (unsigned long)rectangle->x;
		if (offset >= (unsigned long)rectangle->width)
			horizontal = offset - (unsigned long)rectangle->width + 1;
	}
	if (y < rectangle->y)
		vertical = (unsigned long)rectangle->y - (unsigned long)y;
	else {
		offset = (unsigned long)y - (unsigned long)rectangle->y;
		if (offset >= (unsigned long)rectangle->height)
			vertical = offset - (unsigned long)rectangle->height + 1;
	}
	if (horizontal > ULONG_MAX - vertical)
		return ULONG_MAX;
	return horizontal + vertical;
}
#endif

static void
current_monitor_geometry(struct x11_state *state,
	struct x11_geometry *geometry)
{
#if ST_CONSOLE_HAVE_XRANDR || ST_CONSOLE_HAVE_XINERAMA
	Window child;
	int window_x = 0;
	int window_y = 0;
	long center_x;
	long center_y;
	unsigned long best_distance = ULONG_MAX;
	bool found = false;
#endif

	*geometry = (struct x11_geometry){
		.width = DisplayWidth(state->display, state->screen),
		.height = DisplayHeight(state->display, state->screen),
	};
#if ST_CONSOLE_HAVE_XRANDR || ST_CONSOLE_HAVE_XINERAMA
	if (state->window == None
	    || state->api.translate_coordinates(state->display, state->window,
	    state->root, 0, 0, &window_x, &window_y, &child) == 0)
		return;
	center_x = (long)window_x + (long)(state->window_width / 2);
	center_y = (long)window_y + (long)(state->window_height / 2);
#endif
#if ST_CONSOLE_HAVE_XRANDR
	if (state->xrr_get_monitors != NULL) {
		int count = 0;
		XRRMonitorInfo *monitors = state->xrr_get_monitors(state->display,
		    state->root, True, &count);

		for (int i = 0; monitors != NULL && i < count; ++i) {
			struct x11_geometry candidate = {
				.x = monitors[i].x,
				.y = monitors[i].y,
				.width = monitors[i].width,
				.height = monitors[i].height,
			};
			unsigned long distance;

			if (candidate.width <= 0 || candidate.height <= 0)
				continue;
			distance = distance_from_rectangle(center_x, center_y,
			    &candidate);
			if (!found || distance < best_distance) {
				*geometry = candidate;
				best_distance = distance;
				found = true;
			}
		}
		if (monitors != NULL)
			state->xrr_free_monitors(monitors);
		if (found && count > 1)
			return;
		found = false;
		best_distance = ULONG_MAX;
	}
#endif
#if ST_CONSOLE_HAVE_XINERAMA
	if (state->xinerama_is_active != NULL
	    && state->xinerama_is_active(state->display) != 0) {
		int count = 0;
		XineramaScreenInfo *screens = state->xinerama_query_screens(
		    state->display, &count);

		for (int i = 0; screens != NULL && i < count; ++i) {
			struct x11_geometry candidate = {
				.x = screens[i].x_org,
				.y = screens[i].y_org,
				.width = screens[i].width,
				.height = screens[i].height,
			};
			unsigned long distance;

			if (candidate.width <= 0 || candidate.height <= 0)
				continue;
			distance = distance_from_rectangle(center_x, center_y,
			    &candidate);
			if (!found || distance < best_distance) {
				*geometry = candidate;
				best_distance = distance;
				found = true;
			}
		}
		if (screens != NULL)
			(void)state->api.free_memory(screens);
	}
#endif
}

static bool
desktop_workarea(struct x11_state *state, struct x11_geometry *geometry)
{
	long desktop = 0;
	long *values;
	unsigned long count;
	bool found = false;

	if (cardinal_property(state, state->window, state->net_wm_desktop,
	    0, 1, &values, &count)) {
		if (count == 1 && values[0] >= 0
		    && (unsigned long)values[0] != UINT32_MAX
		    && values[0] <= LONG_MAX / 4)
			desktop = values[0];
		(void)state->api.free_memory(values);
	}
	if (cardinal_property(state, state->root, state->net_workarea,
	    desktop * 4, 4, &values, &count)) {
		if (count == 4 && values[0] >= 0 && values[0] <= INT_MAX
		    && values[1] >= 0 && values[1] <= INT_MAX
		    && values[2] > 0 && values[3] > 0
		    && values[2] <= INT_MAX && values[3] <= INT_MAX) {
			*geometry = (struct x11_geometry){
				.x = values[0],
				.y = values[1],
				.width = values[2],
				.height = values[3],
			};
			found = true;
		}
		(void)state->api.free_memory(values);
	}
	return found;
}

static void
available_monitor_geometry(struct x11_state *state,
	struct x11_geometry *geometry)
{
	struct x11_geometry workarea;
	long right;
	long bottom;
	long workarea_right;
	long workarea_bottom;

	current_monitor_geometry(state, geometry);
	if (!desktop_workarea(state, &workarea))
		return;
	right = geometry->x + geometry->width;
	bottom = geometry->y + geometry->height;
	workarea_right = workarea.x + workarea.width;
	workarea_bottom = workarea.y + workarea.height;
	if (geometry->x < workarea.x)
		geometry->x = workarea.x;
	if (geometry->y < workarea.y)
		geometry->y = workarea.y;
	if (right > workarea_right)
		right = workarea_right;
	if (bottom > workarea_bottom)
		bottom = workarea_bottom;
	if (right > geometry->x && bottom > geometry->y) {
		geometry->width = right - geometry->x;
		geometry->height = bottom - geometry->y;
	}
}

static void
window_frame_extents(struct x11_state *state, long *left, long *right,
	long *top, long *bottom)
{
	long *values;
	unsigned long count;

	*left = 0;
	*right = 0;
	*top = 0;
	*bottom = 0;
	if (!cardinal_property(state, state->window, state->net_frame_extents,
	    0, 4, &values, &count))
		return;
	if (count == 4 && values[0] >= 0 && values[0] <= INT_MAX
	    && values[1] >= 0 && values[1] <= INT_MAX
	    && values[2] >= 0 && values[2] <= INT_MAX
	    && values[3] >= 0 && values[3] <= INT_MAX) {
		*left = values[0];
		*right = values[1];
		*top = values[2];
		*bottom = values[3];
	}
	(void)state->api.free_memory(values);
}

static void
center_window(struct x11_state *state)
{
	struct x11_geometry area;
	long left = 0;
	long right = 0;
	long top = 0;
	long bottom = 0;
	long x;
	long y;

	if (state->fullscreen || state->maximized)
		return;
	available_monitor_geometry(state, &area);
	window_frame_extents(state, &left, &right, &top, &bottom);
	x = area.x + (area.width - (long)state->window_width + left - right)
	    / 2;
	y = area.y + (area.height - (long)state->window_height + top - bottom)
	    / 2;
	if (x < INT_MIN || x > INT_MAX || y < INT_MIN || y > INT_MAX)
		return;
	(void)state->api.move_window(state->display, state->window, (int)x,
	    (int)y);
	(void)state->api.flush(state->display);
}

static void
window_pixel_bounds(struct x11_state *state, uint32_t *width, uint32_t *height)
{
	struct x11_geometry area;
	long left, right, top, bottom;

	*width = *height = 0;
	available_monitor_geometry(state, &area);
	window_frame_extents(state, &left, &right, &top, &bottom);
	if (area.width > left + right && area.width - left - right <= UINT32_MAX)
		*width = (uint32_t)(area.width - left - right);
	if (area.height > top + bottom && area.height - top - bottom <= UINT32_MAX)
		*height = (uint32_t)(area.height - top - bottom);
}

static void
snap_window(struct x11_state *state, bool larger)
{
	uint32_t width;
	uint32_t height;
	uint32_t maximum_width;
	uint32_t maximum_height;

	if (state->fullscreen || state->maximized)
		return;
	window_pixel_bounds(state, &maximum_width, &maximum_height);
	if (maximum_width == 0 || maximum_height == 0)
		return;
	if (st_console_snap_geometry_size(&state->window_geometry, state->windowed_width,
	    state->windowed_height, larger, (uint32_t)maximum_width,
	    (uint32_t)maximum_height,
	    &width, &height) != 0
	    || (width == state->windowed_width
	    && height == state->windowed_height))
		return;
	state->windowed_width = width;
	state->windowed_height = height;
	(void)state->api.resize_window(state->display, state->window, width,
	    height);
	(void)state->api.flush(state->display);
}

static void
initialize_x11_manager(void)
{
	if (st_mutex_init(&x11_manager.mutex, ST_MUTEX_PLAIN)
	    == ST_THREAD_SUCCESS)
		x11_manager.ready = true;
}

#define LOAD_REQUIRED(state, member, symbol) \
	(((state)->api.member = (x11_ ## member ## _fn) \
	    dynamic_library_symbol((state)->library, (symbol))) != NULL)

static int
load_xlib(struct x11_state *state)
{
	static const char *const names[] = {"X11"};
	int status = dynamic_library_open(&state->library, names, 1, 6,
	    DYNAMIC_LIBRARY_LOCAL);

	if (status != 0)
		return status;
	if (!LOAD_REQUIRED(state, init_threads, "XInitThreads")
	    || !LOAD_REQUIRED(state, open_display, "XOpenDisplay")
	    || !LOAD_REQUIRED(state, close_display, "XCloseDisplay")
	    || !LOAD_REQUIRED(state, default_screen, "XDefaultScreen")
	    || !LOAD_REQUIRED(state, root_window, "XRootWindow")
	    || !LOAD_REQUIRED(state, default_visual, "XDefaultVisual")
	    || !LOAD_REQUIRED(state, default_depth, "XDefaultDepth")
	    || !LOAD_REQUIRED(state, black_pixel, "XBlackPixel")
	    || !LOAD_REQUIRED(state, connection_number, "XConnectionNumber")
	    || !LOAD_REQUIRED(state, create_simple_window,
	    "XCreateSimpleWindow")
	    || !LOAD_REQUIRED(state, destroy_window, "XDestroyWindow")
	    || !LOAD_REQUIRED(state, map_window, "XMapWindow")
	    || !LOAD_REQUIRED(state, resize_window, "XResizeWindow")
	    || !LOAD_REQUIRED(state, get_window_attributes,
	    "XGetWindowAttributes")
	    || !LOAD_REQUIRED(state, move_window, "XMoveWindow")
	    || !LOAD_REQUIRED(state, translate_coordinates,
	    "XTranslateCoordinates")
	    || !LOAD_REQUIRED(state, set_wm_normal_hints,
	    "XSetWMNormalHints")
	    || !LOAD_REQUIRED(state, select_input, "XSelectInput")
	    || !LOAD_REQUIRED(state, create_gc, "XCreateGC")
	    || !LOAD_REQUIRED(state, free_gc, "XFreeGC")
	    || !LOAD_REQUIRED(state, set_foreground, "XSetForeground")
	    || !LOAD_REQUIRED(state, fill_rectangle, "XFillRectangle")
	    || !LOAD_REQUIRED(state, create_image, "XCreateImage")
	    || !LOAD_REQUIRED(state, put_image, "XPutImage")
	    || !LOAD_REQUIRED(state, flush, "XFlush")
	    || !LOAD_REQUIRED(state, sync, "XSync")
	    || !LOAD_REQUIRED(state, pending, "XPending")
	    || !LOAD_REQUIRED(state, next_event, "XNextEvent")
	    || !LOAD_REQUIRED(state, peek_event, "XPeekEvent")
	    || !LOAD_REQUIRED(state, lookup_keysym, "XLookupKeysym")
	    || !LOAD_REQUIRED(state, lookup_string, "XLookupString")
	    || !LOAD_REQUIRED(state, set_locale_modifiers,
	    "XSetLocaleModifiers")
	    || !LOAD_REQUIRED(state, open_im, "XOpenIM")
	    || !LOAD_REQUIRED(state, close_im, "XCloseIM")
	    || !LOAD_REQUIRED(state, create_ic, "XCreateIC")
	    || !LOAD_REQUIRED(state, destroy_ic, "XDestroyIC")
	    || !LOAD_REQUIRED(state, set_ic_focus, "XSetICFocus")
	    || !LOAD_REQUIRED(state, unset_ic_focus, "XUnsetICFocus")
	    || !LOAD_REQUIRED(state, utf8_lookup_string,
	    "Xutf8LookupString")
	    || !LOAD_REQUIRED(state, filter_event, "XFilterEvent")
	    || !LOAD_REQUIRED(state, keysym_to_keycode, "XKeysymToKeycode")
	    || !LOAD_REQUIRED(state, intern_atom, "XInternAtom")
	    || !LOAD_REQUIRED(state, set_wm_protocols, "XSetWMProtocols")
	    || !LOAD_REQUIRED(state, set_wm_hints, "XSetWMHints")
	    || !LOAD_REQUIRED(state, store_name, "XStoreName")
	    || !LOAD_REQUIRED(state, change_property, "XChangeProperty")
	    || !LOAD_REQUIRED(state, delete_property, "XDeleteProperty")
	    || !LOAD_REQUIRED(state, send_event, "XSendEvent")
	    || !LOAD_REQUIRED(state, get_selection_owner,
	    "XGetSelectionOwner")
	    || !LOAD_REQUIRED(state, set_selection_owner,
	    "XSetSelectionOwner")
	    || !LOAD_REQUIRED(state, convert_selection, "XConvertSelection")
	    || !LOAD_REQUIRED(state, get_window_property,
	    "XGetWindowProperty")
	    || !LOAD_REQUIRED(state, free_memory, "XFree")
	    || !LOAD_REQUIRED(state, create_bitmap_from_data,
	    "XCreateBitmapFromData")
	    || !LOAD_REQUIRED(state, create_pixmap_cursor,
	    "XCreatePixmapCursor")
	    || !LOAD_REQUIRED(state, create_font_cursor, "XCreateFontCursor")
	    || !LOAD_REQUIRED(state, define_cursor, "XDefineCursor")
	    || !LOAD_REQUIRED(state, undefine_cursor, "XUndefineCursor")
	    || !LOAD_REQUIRED(state, free_cursor, "XFreeCursor")
	    || !LOAD_REQUIRED(state, free_pixmap, "XFreePixmap")
	    || !LOAD_REQUIRED(state, create_pixmap, "XCreatePixmap")
	    || !LOAD_REQUIRED(state, xkb_lookup_keysym, "XkbLookupKeySym")) {
		(void)dynamic_library_close(state->library);
		state->library = NULL;
		return IO_ERROR_NOT_SUPPORTED;
	}
	(void)LOAD_REQUIRED(state, xkb_get_keyboard, "XkbGetKeyboard");
	(void)LOAD_REQUIRED(state, xkb_free_keyboard, "XkbFreeKeyboard");
	return 0;
}

#if ST_CONSOLE_HAVE_XRENDER
static int
load_xrender(struct x11_state *state)
{
	static const char *const names[] = {"Xrender"};
	int major;
	int minor;

	if (dynamic_library_open(&state->render_library, names, 1, 1,
	    DYNAMIC_LIBRARY_LOCAL) != 0)
		return 0;
#define LOAD_RENDER(member, symbol) \
	((state->api.member = (x11_ ## member ## _fn) \
	    dynamic_library_symbol(state->render_library, (symbol))) != NULL)
	if (!LOAD_RENDER(render_query_version, "XRenderQueryVersion")
	    || !LOAD_RENDER(render_find_visual_format,
	    "XRenderFindVisualFormat")
	    || !LOAD_RENDER(render_create_picture, "XRenderCreatePicture")
	    || !LOAD_RENDER(render_free_picture, "XRenderFreePicture")
	    || !LOAD_RENDER(render_set_transform, "XRenderSetPictureTransform")
	    || !LOAD_RENDER(render_set_filter, "XRenderSetPictureFilter")
	    || !LOAD_RENDER(render_composite, "XRenderComposite")) {
		(void)dynamic_library_close(state->render_library);
		state->render_library = NULL;
		return 0;
	}
#undef LOAD_RENDER
	if (state->api.render_query_version(state->display, &major, &minor) != 0)
		state->render_available = true;
	return 0;
}
#endif

static void
load_monitor_apis(struct x11_state *state)
{
	(void)state;
#if ST_CONSOLE_HAVE_XRANDR
	static const char *const xrandr_names[] = {"Xrandr"};

	if (dynamic_library_open(&state->xrandr_library, xrandr_names, 1, 2,
	    DYNAMIC_LIBRARY_LOCAL) == 0) {
		state->xrr_get_monitors = (x11_xrr_get_monitors_fn)
		    dynamic_library_symbol(state->xrandr_library,
		    "XRRGetMonitors");
		state->xrr_free_monitors = (x11_xrr_free_monitors_fn)
		    dynamic_library_symbol(state->xrandr_library,
		    "XRRFreeMonitors");
		if (state->xrr_get_monitors == NULL
		    || state->xrr_free_monitors == NULL) {
			(void)dynamic_library_close(state->xrandr_library);
			state->xrandr_library = NULL;
			state->xrr_get_monitors = NULL;
			state->xrr_free_monitors = NULL;
		}
	}
#endif
#if ST_CONSOLE_HAVE_XINERAMA
	static const char *const xinerama_names[] = {"Xinerama"};

	if (dynamic_library_open(&state->xinerama_library, xinerama_names, 1, 1,
	    DYNAMIC_LIBRARY_LOCAL) == 0) {
		state->xinerama_is_active = (x11_xinerama_is_active_fn)
		    dynamic_library_symbol(state->xinerama_library,
		    "XineramaIsActive");
		state->xinerama_query_screens = (x11_xinerama_query_screens_fn)
		    dynamic_library_symbol(state->xinerama_library,
		    "XineramaQueryScreens");
		if (state->xinerama_is_active == NULL
		    || state->xinerama_query_screens == NULL) {
			(void)dynamic_library_close(state->xinerama_library);
			state->xinerama_library = NULL;
			state->xinerama_is_active = NULL;
			state->xinerama_query_screens = NULL;
		}
	}
#endif
}

static int
enable_xlib_threads(struct x11_state *state)
{
	int status;

	st_call_once(&x11_manager_once, initialize_x11_manager);
	if (!x11_manager.ready)
		return IO_ERROR_IO;
	status = st_mutex_lock(&x11_manager.mutex);
	if (status != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (!x11_manager.threads_checked) {
		x11_manager.threads_available = state->api.init_threads() != 0;
		x11_manager.threads_checked = true;
	}
	status = x11_manager.threads_available ? 0 : IO_ERROR_NOT_SUPPORTED;
	(void)st_mutex_unlock(&x11_manager.mutex);
	return status;
}

static uint32_t
milliseconds_now(void)
{
	uint64_t now = 0;

	(void)st_time_monotonic_ms(&now);
	return (uint32_t)now;
}

static void
add_damage(struct x11_state *state, const struct st_console_rect *rectangle)
{
	st_console_rect_accumulate(&state->damage, &state->damage_valid,
	    rectangle);
}

static void
destroy_image(struct x11_state *state)
{
	if (state->image != NULL) {
		XDestroyImage(state->image);
		state->image = NULL;
	}
}

#if ST_CONSOLE_HAVE_XRENDER
static void
destroy_render_resources(struct x11_state *state)
{
	if (state->render_source != 0)
		state->api.render_free_picture(state->display,
		    state->render_source);
	if (state->render_destination != 0)
		state->api.render_free_picture(state->display,
		    state->render_destination);
	if (state->render_pixmap != 0)
		state->api.free_pixmap(state->display, state->render_pixmap);
	state->render_source = 0;
	state->render_destination = 0;
	state->render_pixmap = 0;
	state->render_width = 0;
	state->render_height = 0;
}
#endif

static int
ensure_image(struct x11_state *state, uint32_t width, uint32_t height)
{
	size_t size;

	if (state->image != NULL && state->image->width == (int)width
	    && state->image->height == (int)height)
		return 0;
	if (width > INT_MAX || height > INT_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	destroy_image(state);
#if ST_CONSOLE_HAVE_XRENDER
	destroy_render_resources(state);
#endif
	state->image = state->api.create_image(state->display, state->visual,
	    (unsigned int)state->depth, ZPixmap, 0, NULL, width, height, 32, 0);
	if (state->image == NULL || state->image->bytes_per_line <= 0) {
		destroy_image(state);
		return IO_ERROR_IO;
	}
	if ((size_t)state->image->bytes_per_line > SIZE_MAX / height) {
		destroy_image(state);
		return IO_ERROR_OUT_OF_RANGE;
	}
	size = (size_t)state->image->bytes_per_line * height;
	state->image->data = calloc(1, size);
	if (state->image->data == NULL) {
		destroy_image(state);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	st_x11_pixel_writer_init(&state->pixel_writer, state->image);
	return 0;
}

static unsigned long
component_value(uint32_t component, unsigned long mask)
{
	unsigned int shift = 0;
	unsigned long maximum;

	if (mask == 0)
		return 0;
	while (((mask >> shift) & 1UL) == 0)
		++shift;
	maximum = mask >> shift;
	return (((unsigned long)component * maximum + 127UL) / 255UL << shift)
	    & mask;
}

static unsigned long
native_pixel(const struct x11_state *state, uint32_t pixel)
{
	return state->red_values[(pixel >> 16) & UINT32_C(0xff)]
	    | state->green_values[(pixel >> 8) & UINT32_C(0xff)]
	    | state->blue_values[pixel & UINT32_C(0xff)];
}

static bool
store_native_pixel(XImage *image, uint32_t x, uint32_t y,
	unsigned long value)
{
	unsigned int bytes;
	unsigned char *destination;

	if (image->bits_per_pixel != 8 && image->bits_per_pixel != 16
	    && image->bits_per_pixel != 24 && image->bits_per_pixel != 32)
		return false;
	bytes = (unsigned int)image->bits_per_pixel / 8;
	destination = (unsigned char *)image->data
	    + (size_t)y * (size_t)image->bytes_per_line + (size_t)x * bytes;
	if (image->byte_order == LSBFirst) {
		for (unsigned int i = 0; i < bytes; ++i)
			destination[i] = (unsigned char)(value >> (i * 8));
	}
	else {
		for (unsigned int i = 0; i < bytes; ++i)
			destination[i] = (unsigned char)(value
			    >> ((bytes - i - 1) * 8));
	}
	return true;
}

static int
upload_pixels(void *context, const struct st_rgb_buffer *pixels,
	const struct st_console_rect *rectangle, const void *image_identity,
	uint64_t generation)
{
	struct x11_state *state = context;
	int status = ensure_image(state, pixels->width, pixels->height);

	(void)image_identity;
	(void)generation;

	if (status != 0)
		return status;
	st_x11_pixel_writer_upload(&state->pixel_writer, state->image, pixels, rectangle);
	add_damage(state, rectangle);
	return 0;
}

#if ST_CONSOLE_HAVE_XRENDER
static int
ensure_render_resources(struct x11_state *state)
{
	XRenderPictFormat *format;

	if (state->render_pixmap != 0
	    && state->render_width == (uint32_t)state->image->width
	    && state->render_height == (uint32_t)state->image->height)
		return 0;
	destroy_render_resources(state);
	format = state->api.render_find_visual_format(state->display,
	    state->visual);
	if (format == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	state->render_pixmap = state->api.create_pixmap(state->display,
	    state->window, (unsigned int)state->image->width,
	    (unsigned int)state->image->height, (unsigned int)state->depth);
	if (state->render_pixmap == 0)
		return IO_ERROR_IO;
	state->render_source = state->api.render_create_picture(state->display,
	    state->render_pixmap, format, 0, NULL);
	state->render_destination = state->api.render_create_picture(
	    state->display, state->window, format, 0, NULL);
	if (state->render_source == 0 || state->render_destination == 0) {
		destroy_render_resources(state);
		return IO_ERROR_IO;
	}
	state->api.render_set_filter(state->display, state->render_source,
	    "best", NULL, 0);
	state->render_width = (uint32_t)state->image->width;
	state->render_height = (uint32_t)state->image->height;
	return 0;
}
#endif

static void
clear_window(struct x11_state *state)
{
	(void)state->api.set_foreground(state->display, state->gc,
	    state->api.black_pixel(state->display, state->screen));
	(void)state->api.fill_rectangle(state->display, state->window, state->gc,
	    0, 0, state->window_width, state->window_height);
}

static int
draw_state(struct x11_state *state, const st_console_t *console, bool repaint)
{
	struct st_console_rect full;
	bool layout_changed;
	int status;

	state->damage_valid = false;
	status = st_console_bitmap_presenter_frame(state->presenter, console,
	    state->window_width, state->window_height, milliseconds_now(),
	    upload_pixels, state, &state->frame);
	if (status != 0)
		return status;
	layout_changed = !state->destination_valid
	    || !st_console_rects_equal(&state->old_destination,
	    &state->frame.destination);
	if (repaint && !state->damage_valid && state->image == NULL) {
		full = (struct st_console_rect){0, 0, state->frame.pixels->width,
		    state->frame.pixels->height};
		status = upload_pixels(state, state->frame.pixels, &full,
		    state->frame.image_identity, state->frame.generation);
		if (status != 0)
			return status;
	}
	if (!state->damage_valid && !state->frame.changed && !repaint)
		return 0;
	if (layout_changed || repaint)
		clear_window(state);
	if (state->frame.scaling_mode == ST_CONSOLE_SCALING_EXTERNAL
	    && (state->frame.destination.width != (uint32_t)state->image->width
	    || state->frame.destination.height != (uint32_t)state->image->height)) {
#if ST_CONSOLE_HAVE_XRENDER
		XTransform transform = {{
			{(XFixed)(((uint64_t)(uint32_t)state->image->width << 16)
			    / state->frame.destination.width), 0, 0},
			{0, (XFixed)(((uint64_t)(uint32_t)state->image->height << 16)
			    / state->frame.destination.height), 0},
			{0, 0, XDoubleToFixed(1.0)},
		}};

		if (!state->render_available)
			return IO_ERROR_NOT_SUPPORTED;
		status = ensure_render_resources(state);
		if (status != 0)
			return status;
		if (state->damage_valid)
			(void)state->api.put_image(state->display,
			    state->render_pixmap, state->gc, state->image,
			    (int)state->damage.x, (int)state->damage.y,
			    (int)state->damage.x, (int)state->damage.y,
			    state->damage.width, state->damage.height);
		state->api.render_set_transform(state->display,
		    state->render_source, &transform);
		state->api.render_composite(state->display, PictOpSrc,
		    state->render_source, 0, state->render_destination, 0, 0, 0, 0,
		    (int)state->frame.destination.x,
		    (int)state->frame.destination.y,
		    state->frame.destination.width, state->frame.destination.height);
#else
		return IO_ERROR_NOT_SUPPORTED;
#endif
	}
	else if (state->damage_valid || repaint) {
		const struct st_console_rect *upload = &state->damage;

		if (repaint) {
			full = (struct st_console_rect){0, 0,
			    (uint32_t)state->image->width,
			    (uint32_t)state->image->height};
			upload = &full;
		}
		(void)state->api.put_image(state->display, state->window, state->gc,
		    state->image, (int)upload->x, (int)upload->y,
		    (int)(state->frame.destination.x + upload->x),
		    (int)(state->frame.destination.y + upload->y),
		    upload->width, upload->height);
	}
	(void)state->api.flush(state->display);
	state->old_destination = state->frame.destination;
	state->destination_valid = true;
	return 0;
}

static char *
copy_bytes(const unsigned char *source, size_t length)
{
	char *copy;

	if (length == SIZE_MAX)
		return NULL;
	copy = malloc(length + 1);
	if (copy == NULL)
		return NULL;
	memcpy(copy, source, length);
	copy[length] = '\0';
	return copy;
}

static char *
latin1_to_utf8(const unsigned char *source, size_t length)
{
	size_t extra = 0;
	char *result;
	size_t output = 0;

	for (size_t i = 0; i < length; ++i)
		extra += source[i] >= UINT8_C(0x80);
	if (length > SIZE_MAX - extra - 1)
		return NULL;
	result = malloc(length + extra + 1);
	if (result == NULL)
		return NULL;
	for (size_t i = 0; i < length; ++i) {
		ptrdiff_t encoded = st_utf8_encode_one(result + output,
		    length + extra - output, source[i]);

		if (encoded < 0) {
			free(result);
			return NULL;
		}
		output += (size_t)encoded;
	}
	result[output] = '\0';
	return result;
}

static bool utf8_next(const unsigned char **current,
	const unsigned char *end, uint32_t *result);
static void handle_event(struct x11_state *state, XEvent *event);

static unsigned char *
utf8_to_latin1(const char *source, size_t *result_length)
{
	const unsigned char *current = (const unsigned char *)source;
	const unsigned char *end = current + strlen(source);
	unsigned char *result = malloc((size_t)(end - current) + 1);
	size_t length = 0;

	if (result == NULL)
		return NULL;
	while (current < end) {
		const unsigned char *before = current;
		uint32_t codepoint;

		if (!utf8_next(&current, end, &codepoint)) {
			current = before + 1;
			codepoint = '?';
		}
		result[length++] = codepoint <= UINT8_MAX
		    ? (unsigned char)codepoint : (unsigned char)'?';
	}
	result[length] = '\0';
	*result_length = length;
	return result;
}

static int
clipboard_result(struct x11_state *state, char **result)
{
	unsigned char *property = NULL;
	Atom type;
	int format;
	unsigned long count;
	unsigned long remaining;
	int status;

	status = state->api.get_window_property(state->display, state->window,
	    state->clipboard_property, 0, LONG_MAX / 4, True, AnyPropertyType,
	    &type, &format, &count, &remaining, &property);
	if (status != Success || property == NULL || format != 8) {
		if (property != NULL)
			(void)state->api.free_memory(property);
		return IO_ERROR_IO;
	}
	if (type == state->utf8_string)
		*result = copy_bytes(property, count);
	else if (type == XA_STRING)
		*result = latin1_to_utf8(property, count);
	else
		*result = NULL;
	(void)state->api.free_memory(property);
	return *result == NULL ? IO_ERROR_OUT_OF_MEMORY : 0;
}

static int
get_clipboard(struct x11_state *state, char **result)
{
	Window owner = state->api.get_selection_owner(state->display,
	    state->selection_atom);
	struct pollfd descriptor;
	uint32_t start;

	if (owner == None)
		return IO_ERROR_NOT_FOUND;
	if (owner == state->window) {
		if (state->clipboard == NULL)
			return IO_ERROR_NOT_FOUND;
		*result = copy_bytes((const unsigned char *)state->clipboard,
		    strlen(state->clipboard));
		return *result == NULL ? IO_ERROR_OUT_OF_MEMORY : 0;
	}
	(void)state->api.delete_property(state->display, state->window,
	    state->clipboard_property);
	(void)state->api.convert_selection(state->display, state->selection_atom,
	    state->utf8_string != None ? state->utf8_string : XA_STRING,
	    state->clipboard_property, state->window, CurrentTime);
	(void)state->api.flush(state->display);
	descriptor.fd = state->api.connection_number(state->display);
	descriptor.events = POLLIN;
	start = milliseconds_now();
	while (milliseconds_now() - start < UINT32_C(2000)) {
		while (state->api.pending(state->display) != 0) {
			XEvent event;

			(void)state->api.next_event(state->display, &event);
			if (event.type == SelectionNotify
			    && event.xselection.selection == state->selection_atom) {
				if (event.xselection.property == None)
					return IO_ERROR_NOT_SUPPORTED;
				return clipboard_result(state, result);
			}
			handle_event(state, &event);
		}
		(void)poll(&descriptor, 1, 25);
	}
	return IO_ERROR_TIMED_OUT;
}

static void
selection_request(struct x11_state *state,
	const XSelectionRequestEvent *request)
{
	XEvent response = {0};
	Atom property = request->property == None ? request->target
	    : request->property;

	response.xselection.type = SelectionNotify;
	response.xselection.display = request->display;
	response.xselection.requestor = request->requestor;
	response.xselection.selection = request->selection;
	response.xselection.target = request->target;
	response.xselection.time = request->time;
	response.xselection.property = None;
	if (request->selection == state->selection_atom
	    && state->clipboard != NULL) {
		if (request->target == state->targets_atom) {
			Atom targets[3] = {state->targets_atom, XA_STRING,
			    state->utf8_string};
			int count = state->utf8_string == None ? 2 : 3;

			(void)state->api.change_property(state->display,
			    request->requestor, property, XA_ATOM, 32,
			    PropModeReplace, (const unsigned char *)targets, count);
			response.xselection.property = property;
		}
		else if (request->target == state->utf8_string) {
			(void)state->api.change_property(state->display,
			    request->requestor, property, state->utf8_string, 8,
			    PropModeReplace,
			    (const unsigned char *)state->clipboard,
			    (int)strlen(state->clipboard));
			response.xselection.property = property;
		}
		else if (request->target == XA_STRING) {
			size_t length;
			unsigned char *latin1 = utf8_to_latin1(state->clipboard,
			    &length);

			if (latin1 != NULL && length <= INT_MAX) {
				(void)state->api.change_property(state->display,
				    request->requestor, property, XA_STRING, 8,
				    PropModeReplace, latin1, (int)length);
				response.xselection.property = property;
			}
			free(latin1);
		}
	}
	(void)state->api.send_event(state->display, request->requestor, False,
	    0, &response);
	(void)state->api.flush(state->display);
}

static bool
utf8_next(const unsigned char **current, const unsigned char *end,
	uint32_t *result)
{
	const unsigned char *text = *current;
	size_t consumed;

	if (st_utf8_decode_one(result, &consumed, text, (size_t)(end - text)) < 0)
		return false;
	*current = text + consumed;
	return true;
}

static void
key_event(struct x11_state *state, XKeyEvent *source, bool pressed)
{
	KeySym physical_keysym = state->api.lookup_keysym(source, 0);
	KeySym resolved_keysym = physical_keysym;
	KeySym keysym = NoSymbol;
	unsigned int consumed = 0;
	uint16_t physical;
	bool repeat = false;
	bool control_key = false;
	char stack_text[128];
	char *text = stack_text;
	int text_length = 0;
	Status lookup_status = XLookupNone;

	(void)state->api.xkb_lookup_keysym(state->display, source->keycode,
	    source->state, &consumed, &resolved_keysym);

	if (!pressed && state->api.pending(state->display) != 0) {
		XEvent next;

		(void)state->api.peek_event(state->display, &next);
		if (next.type == KeyPress && next.xkey.keycode == source->keycode
		    && next.xkey.time == source->time)
			return;
	}
	if (source->keycode < 256) {
		repeat = pressed && state->key_down[source->keycode];
		state->key_down[source->keycode] = pressed;
	}
	if (source->keycode < 256
	    && state->keycode_evdev[source->keycode] != 0)
		physical = state->keycode_evdev[source->keycode];
	else if (state->evdev_keycodes && source->keycode >= 8
	    && source->keycode - 8 <= UINT16_MAX)
		physical = (uint16_t)(source->keycode - 8);
	else
		physical = st_console_x11_evdev_from_keysym(physical_keysym);
	if (physical != 0) {
		struct st_console_event event = {
			.type = ST_CONSOLE_EVENT_PHYSICAL_KEY,
			.data.physical_key = {
				.evdev = physical,
				.modifiers = st_console_x11_modifiers(source->state),
				.pressed = pressed,
				.repeat = repeat,
			},
		};

		(void)st_console_display_post_event(state->console_display, &event);
	}
	if (pressed && (source->state & Mod1Mask) != 0) {
		if (!repeat && (physical_keysym == XK_Return
		    || physical_keysym == XK_KP_Enter)) {
			(void)set_fullscreen(state, !state->fullscreen);
			return;
		}
		if (physical_keysym == XK_Left
		    || physical_keysym == XK_KP_Left) {
			snap_window(state, false);
			return;
		}
		if (physical_keysym == XK_Right
		    || physical_keysym == XK_KP_Right) {
			snap_window(state, true);
			return;
		}
		if (physical_keysym == XK_Home
		    || physical_keysym == XK_KP_Home) {
			center_window(state);
			return;
		}
	}
	if (pressed) {
		uint16_t key;

		if (state->input_context != NULL) {
			text_length = state->api.utf8_lookup_string(
			    state->input_context, source, stack_text,
			    (int)sizeof(stack_text), &keysym, &lookup_status);
			if (lookup_status == XBufferOverflow && text_length > 0) {
				text = malloc((size_t)text_length + 1);
				if (text == NULL)
					return;
				text_length = state->api.utf8_lookup_string(
				    state->input_context, source, text,
				    text_length + 1, &keysym, &lookup_status);
			}
		}
		else {
			text_length = state->api.lookup_string(source, stack_text,
			    (int)sizeof(stack_text), &keysym, NULL);
			if (keysym == NoSymbol)
				keysym = resolved_keysym;
			lookup_status = text_length > 0
			    ? XLookupBoth : XLookupKeySym;
		}
		key = lookup_status == XLookupChars ? 0
		    : st_console_x11_key_from_keysym(keysym);
		if (key == 0 && (source->state & ControlMask) != 0
		    && (source->state & Mod1Mask) == 0)
			control_key = st_console_control_key((uint32_t)(keysym == NoSymbol
			    ? resolved_keysym : keysym), &key);

		if (key != 0 || control_key) {
			struct st_console_event event = {
				.type = ST_CONSOLE_EVENT_KEY_CODE,
				.data.key = {
					.code = key,
					.modifiers = st_console_x11_modifiers(source->state),
				},
			};

			(void)st_console_display_post_event(state->console_display, &event);
		}
		else if (text_length > 0
		    && (lookup_status == XLookupChars
		    || ((source->state & ~consumed & (ControlMask | Mod1Mask))
		    == 0))) {
			const unsigned char *current = (const unsigned char *)text;
			const unsigned char *end = current + text_length;

			while (current < end) {
				uint32_t codepoint;

				if (state->input_context != NULL) {
					if (!utf8_next(&current, end, &codepoint))
						break;
				}
				else {
					codepoint = *current++;
					if (codepoint > 0x7f)
						continue;
				}
				struct st_console_event event = {
					.type = ST_CONSOLE_EVENT_TEXT,
					.data.text = {
						.codepoint = codepoint,
						.modifiers = st_console_x11_modifiers(source->state),
					},
				};

				(void)st_console_display_post_event(state->console_display, &event);
			}
		}
		if (text != stack_text)
			free(text);
	}
}

static void
build_keycode_map(struct x11_state *state)
{
	XkbDescPtr keyboard;

	if (state->api.xkb_get_keyboard == NULL
	    || state->api.xkb_free_keyboard == NULL)
		return;
	keyboard = state->api.xkb_get_keyboard(state->display,
	    XkbGBN_KeyNamesMask, XkbUseCoreKbd);
	if (keyboard == NULL)
		return;
	if (keyboard->names != NULL && keyboard->names->keys != NULL) {
		for (int code = keyboard->min_key_code;
		    code <= keyboard->max_key_code && code < 256; ++code) {
			if (code >= 0)
				state->keycode_evdev[code]
				    = st_console_x11_evdev_from_keyname(
				    keyboard->names->keys[code].name);
		}
	}
	(void)state->api.xkb_free_keyboard(keyboard,
	    XkbAllComponentsMask, True);
}

static bool
window_property(struct x11_state *state, Window window, Atom property,
	Atom expected_type, unsigned long *value)
{
	Atom type = None;
	int format = 0;
	unsigned long count = 0;
	unsigned long remaining = 0;
	unsigned char *data = NULL;
	bool valid = false;

	if (property != None
	    && state->api.get_window_property(state->display, window, property,
	    0, 1, False, expected_type, &type, &format, &count, &remaining,
	    &data) == Success && type == expected_type && format == 32
	    && count == 1 && data != NULL) {
		*value = ((const unsigned long *)data)[0];
		valid = true;
	}
	if (data != NULL)
		(void)state->api.free_memory(data);
	return valid;
}

static void
initialize_ewmh_atoms(struct x11_state *state)
{
	struct atom_binding {
		const char *name;
		Atom atom;
		Atom *destination;
	};
	struct atom_binding bindings[] = {
		{"_NET_FRAME_EXTENTS", None, &state->net_frame_extents},
		{"_NET_WM_DESKTOP", None, &state->net_wm_desktop},
		{"_NET_WM_ICON", None, &state->net_wm_icon},
		{"_NET_WM_ICON_NAME", None, &state->net_wm_icon_name},
		{"_NET_WM_NAME", None, &state->net_wm_name},
		{"_NET_WM_PID", None, &state->net_wm_pid},
		{"_NET_WM_PING", None, &state->wm_ping},
		{"_NET_WM_STATE", None, &state->net_wm_state},
		{"_NET_WM_STATE_FULLSCREEN", None, &state->net_wm_fullscreen},
		{"_NET_WM_STATE_MAXIMIZED_VERT", None,
		    &state->net_wm_maximized_vertical},
		{"_NET_WM_STATE_MAXIMIZED_HORZ", None,
		    &state->net_wm_maximized_horizontal},
		{"_NET_WM_WINDOW_TYPE", None, &state->net_wm_window_type},
		{"_NET_WM_WINDOW_TYPE_NORMAL", None,
		    &state->net_wm_window_type_normal},
		{"_NET_WORKAREA", None, &state->net_workarea},
	};
	Atom supported = state->api.intern_atom(state->display,
	    "_NET_SUPPORTED", False);
	Atom check = state->api.intern_atom(state->display,
	    "_NET_SUPPORTING_WM_CHECK", False);
	unsigned long manager;
	unsigned long manager_check;
	long offset = 0;
	unsigned long remaining = 1;

	for (size_t i = 0; i < sizeof(bindings) / sizeof(bindings[0]); ++i) {
		*bindings[i].destination = None;
		bindings[i].atom = state->api.intern_atom(state->display,
		    bindings[i].name, False);
	}
	if (!window_property(state, state->root, check, XA_WINDOW, &manager)
	    || manager == None
	    || !window_property(state, (Window)manager, check, XA_WINDOW,
	    &manager_check) || manager_check != manager)
		return;
	{
		Atom type = None;
		int format = 0;
		unsigned long count = 0;
		unsigned long trailing = 0;
		unsigned char *data = NULL;
		Atom wm_name = state->api.intern_atom(state->display,
		    "_NET_WM_NAME", False);

		if (state->api.get_window_property(state->display, (Window)manager,
		    wm_name, 0, 64, False, AnyPropertyType, &type, &format, &count,
		    &trailing, &data) == Success && format == 8 && data != NULL
		    && count == strlen("Sommelier")
		    && memcmp(data, "Sommelier", count) == 0)
			state->sommelier = true;
		if (data != NULL)
			(void)state->api.free_memory(data);
	}
	while (remaining != 0) {
		Atom type = None;
		int format = 0;
		unsigned long count = 0;
		unsigned char *data = NULL;

		if (state->api.get_window_property(state->display, state->root,
		    supported, offset, 256, False, XA_ATOM, &type, &format, &count,
		    &remaining, &data) != Success || type != XA_ATOM || format != 32
		    || data == NULL) {
			if (data != NULL)
				(void)state->api.free_memory(data);
			return;
		}
		for (unsigned long i = 0; i < count; ++i) {
			Atom atom = (Atom)((const unsigned long *)data)[i];

			for (size_t j = 0;
			    j < sizeof(bindings) / sizeof(bindings[0]); ++j) {
				if (bindings[j].atom == atom)
					*bindings[j].destination = atom;
			}
		}
		(void)state->api.free_memory(data);
		if (count == 0)
			break;
		offset += (long)count;
	}
}

static void
set_window_icon_hints(struct x11_state *state)
{
	const struct st_console_icon *icon = state->console_display->config.icon;
	uint32_t width;
	uint32_t height;
	XImage *image;
	char *mask_data;
	size_t mask_stride;
	XWMHints hints = {
		.flags = InputHint | StateHint,
		.input = True,
		.initial_state = NormalState,
	};

	if (icon == NULL)
		goto set_hints;
	width = icon->width > 32 ? 32 : icon->width;
	height = icon->height > 32 ? 32 : icon->height;
	image = state->api.create_image(state->display, state->visual,
	    (unsigned int)state->depth, ZPixmap, 0, NULL, width, height, 32, 0);
	if (image == NULL || image->bytes_per_line <= 0)
		goto destroy_image;
	if ((size_t)image->bytes_per_line > SIZE_MAX / height)
		goto destroy_image;
	image->data = calloc(height, (size_t)image->bytes_per_line);
	if (image->data == NULL)
		goto destroy_image;
	mask_stride = ((size_t)width + 7) / 8;
	mask_data = calloc(height, mask_stride);
	if (mask_data == NULL)
		goto destroy_image;
	for (uint32_t y = 0; y < height; ++y) {
		uint32_t source_y = (uint32_t)((uint64_t)y * icon->height / height);
		const unsigned char *row = &icon->pixels[
		    (size_t)source_y * icon->stride];

		for (uint32_t x = 0; x < width; ++x) {
			uint32_t source_x = (uint32_t)((uint64_t)x * icon->width / width);
			const unsigned char *pixel = &row[(size_t)source_x * 4];
			uint32_t rgb = (uint32_t)pixel[0] << 16
			    | (uint32_t)pixel[1] << 8 | pixel[2];

			(void)store_native_pixel(image, x, y,
			    native_pixel(state, rgb));
			if (pixel[3] != 0)
				mask_data[(size_t)y * mask_stride + x / 8]
				    |= (char)(1U << (x % 8));
		}
	}
	state->icon_pixmap = state->api.create_pixmap(state->display,
	    state->root, width, height, (unsigned int)state->depth);
	state->icon_mask = state->api.create_bitmap_from_data(state->display,
	    state->root, mask_data, width, height);
	free(mask_data);
	if (state->icon_pixmap == None || state->icon_mask == None)
		goto destroy_image;
	(void)state->api.put_image(state->display, state->icon_pixmap, state->gc,
	    image, 0, 0, 0, 0, width, height);
	hints.flags |= IconPixmapHint | IconMaskHint;
	hints.icon_pixmap = state->icon_pixmap;
	hints.icon_mask = state->icon_mask;

destroy_image:
	if (image != NULL)
		XDestroyImage(image);
set_hints:
	(void)state->api.set_wm_hints(state->display, state->window, &hints);
}

static void
set_window_icon(struct x11_state *state)
{
	const struct st_console_icon *icon = state->console_display->config.icon;
	size_t pixels;
	unsigned long *property;
	Atom atom;

	if (icon == NULL || icon->width > INT_MAX || icon->height > INT_MAX
	    || icon->width > ((size_t)INT_MAX - 2) / icon->height)
		return;
	pixels = (size_t)icon->width * icon->height;
	property = malloc((pixels + 2) * sizeof(*property));
	if (property == NULL)
		return;
	property[0] = icon->width;
	property[1] = icon->height;
	for (uint32_t y = 0; y < icon->height; ++y) {
		const unsigned char *row = &icon->pixels[(size_t)y * icon->stride];

		for (uint32_t x = 0; x < icon->width; ++x) {
			const unsigned char *pixel = &row[(size_t)x * 4];

			property[2 + (size_t)y * icon->width + x]
			    = ((unsigned long)pixel[3] << 24)
			    | ((unsigned long)pixel[0] << 16)
			    | ((unsigned long)pixel[1] << 8) | pixel[2];
		}
	}
	atom = state->net_wm_icon;
	if (atom != None)
		(void)state->api.change_property(state->display, state->window,
		    atom, XA_CARDINAL, 32, PropModeReplace,
		    (const unsigned char *)property, (int)(pixels + 2));
	free(property);
}

static void
set_window_title(struct x11_state *state, const char *title)
{
	(void)state->api.store_name(state->display, state->window, title);
	if (state->net_wm_name != None && state->utf8_string != None)
		(void)state->api.change_property(state->display, state->window,
		    state->net_wm_name, state->utf8_string, 8, PropModeReplace,
		    (const unsigned char *)title, (int)strlen(title));
}

static void
set_window_identity(struct x11_state *state)
{
	static const unsigned char wm_class[] = "syncterm\0SyncTERM";
	static const unsigned char name[] = "SyncTERM";
	unsigned long process = (unsigned long)getpid();
	Atom type = state->net_wm_window_type_normal;

	(void)state->api.change_property(state->display, state->window,
	    XA_WM_CLASS, XA_STRING, 8, PropModeReplace, wm_class,
	    (int)sizeof(wm_class));
	(void)state->api.change_property(state->display, state->window,
	    XA_WM_ICON_NAME, XA_STRING, 8, PropModeReplace, name,
	    (int)(sizeof(name) - 1));
	if (state->net_wm_icon_name != None && state->utf8_string != None)
		(void)state->api.change_property(state->display, state->window,
		    state->net_wm_icon_name, state->utf8_string, 8,
		    PropModeReplace, name, (int)(sizeof(name) - 1));
	if (state->net_wm_pid != None)
		(void)state->api.change_property(state->display, state->window,
		    state->net_wm_pid, XA_CARDINAL, 32, PropModeReplace,
		    (const unsigned char *)&process, 1);
	if (state->net_wm_window_type != None && type != None)
		(void)state->api.change_property(state->display, state->window,
		    state->net_wm_window_type, XA_ATOM, 32, PropModeReplace,
		    (const unsigned char *)&type, 1);
}

static void
update_window_state(struct x11_state *state)
{
	Atom type = None;
	int format = 0;
	unsigned long count = 0;
	unsigned long remaining = 0;
	unsigned char *property = NULL;
	bool maximized_vertical = false;
	bool maximized_horizontal = false;
	bool was_maximized = state->maximized;

	if (state->net_wm_state == None)
		return;
	if (state->api.get_window_property(state->display, state->window,
	    state->net_wm_state, 0, 64, False, XA_ATOM, &type, &format, &count,
	    &remaining, &property) == Success && property != NULL
	    && type == XA_ATOM && format == 32) {
		const unsigned long *atoms = (const unsigned long *)property;

		for (unsigned long i = 0; i < count; ++i) {
			if ((Atom)atoms[i] == state->net_wm_maximized_vertical)
				maximized_vertical = true;
			else if ((Atom)atoms[i]
			    == state->net_wm_maximized_horizontal)
				maximized_horizontal = true;
		}
	}
	if (property != NULL)
		(void)state->api.free_memory(property);
	state->maximized = maximized_vertical || maximized_horizontal;
	if (was_maximized && !state->maximized && !state->fullscreen) {
		(void)state->api.resize_window(state->display, state->window,
		    state->windowed_width, state->windowed_height);
		(void)state->api.flush(state->display);
	}
}

static void
handle_event(struct x11_state *state, XEvent *event)
{
	struct st_console_event output = {0};

	switch (event->type) {
	case Expose:
		st_console_display_request_repaint(state->console_display);
		break;
	case ConfigureNotify:
		if (event->xconfigure.width > 0 && event->xconfigure.height > 0) {
			uint32_t width = (uint32_t)event->xconfigure.width;
			uint32_t height = (uint32_t)event->xconfigure.height;

			if (width != state->window_width
			    || height != state->window_height) {
				state->window_width = width;
				state->window_height = height;
				if (!state->fullscreen && !state->maximized) {
					state->windowed_width = width;
					state->windowed_height = height;
				}
				st_console_display_request_repaint(
				    state->console_display);
			}
		}
		break;
	case PropertyNotify:
		if (event->xproperty.atom == state->net_wm_state)
			update_window_state(state);
		break;
	case FocusIn:
	case FocusOut:
		if (event->xfocus.mode == NotifyGrab
		    || event->xfocus.mode == NotifyUngrab
		    || event->xfocus.detail == NotifyInferior
		    || event->xfocus.detail == NotifyPointer)
			break;
		if (event->type == FocusOut)
			memset(state->key_down, 0, sizeof(state->key_down));
		if (state->input_context != NULL) {
			if (event->type == FocusIn)
				state->api.set_ic_focus(state->input_context);
			else
				state->api.unset_ic_focus(state->input_context);
		}
		output.type = ST_CONSOLE_EVENT_FOCUS;
		output.data.focus.focused = event->type == FocusIn;
		(void)st_console_display_post_event(state->console_display, &output);
		break;
	case ClientMessage:
		if ((Atom)event->xclient.data.l[0] == state->wm_delete) {
			output.type = ST_CONSOLE_EVENT_CLOSE;
			(void)st_console_display_post_event(state->console_display, &output);
		}
		else if ((Atom)event->xclient.data.l[0] == state->wm_ping) {
			event->xclient.window = state->root;
			(void)state->api.send_event(state->display, state->root,
			    False, SubstructureNotifyMask
			    | SubstructureRedirectMask, event);
		}
		break;
	case KeyPress:
		key_event(state, &event->xkey, true);
		break;
	case KeyRelease:
		key_event(state, &event->xkey, false);
		break;
	case MotionNotify:
		if (state->frame.destination.width == 0
		    || state->frame.destination.height == 0)
			break;
		output.type = ST_CONSOLE_EVENT_MOUSE;
		output.data.mouse.action = ST_CONSOLE_MOUSE_MOVE;
		output.data.mouse.modifiers = st_console_x11_modifiers(
		    event->xmotion.state);
		st_console_bitmap_mouse_position(&state->frame, event->xmotion.x,
		    event->xmotion.y, &output);
		output.data.mouse.buttons = state->mouse_buttons;
		(void)st_console_display_post_event(state->console_display, &output);
		break;
	case ButtonPress:
	case ButtonRelease: {
		uint32_t button = 0;

		if (event->xbutton.button == Button1)
			button = ST_CONSOLE_MOUSE_BUTTON_LEFT;
		else if (event->xbutton.button == Button2)
			button = ST_CONSOLE_MOUSE_BUTTON_MIDDLE;
		else if (event->xbutton.button == Button3)
			button = ST_CONSOLE_MOUSE_BUTTON_RIGHT;
		else if (event->xbutton.button == 8)
			button = ST_CONSOLE_MOUSE_BUTTON_X1;
		else if (event->xbutton.button == 9)
			button = ST_CONSOLE_MOUSE_BUTTON_X2;
		if (event->type == ButtonPress && event->xbutton.button >= 4
		    && event->xbutton.button <= 7) {
			output.type = ST_CONSOLE_EVENT_MOUSE;
			output.data.mouse.action = ST_CONSOLE_MOUSE_WHEEL;
			output.data.mouse.modifiers = st_console_x11_modifiers(
			    event->xbutton.state);
			if (event->xbutton.button == 4)
				output.data.mouse.wheel_y = 1;
			else if (event->xbutton.button == 5)
				output.data.mouse.wheel_y = -1;
			else if (event->xbutton.button == 6)
				output.data.mouse.wheel_x = -1;
			else
				output.data.mouse.wheel_x = 1;
			output.data.mouse.buttons = state->mouse_buttons;
			st_console_bitmap_mouse_position(&state->frame,
			    event->xbutton.x, event->xbutton.y, &output);
			(void)st_console_display_post_event(state->console_display, &output);
			break;
		}
		if (button == 0)
			break;
		if (event->type == ButtonPress)
			state->mouse_buttons |= button;
		else
			state->mouse_buttons &= ~button;
		output.type = ST_CONSOLE_EVENT_MOUSE;
		output.data.mouse.action = ST_CONSOLE_MOUSE_BUTTON;
		output.data.mouse.modifiers = st_console_x11_modifiers(
		    event->xbutton.state);
		st_console_bitmap_mouse_position(&state->frame, event->xbutton.x,
		    event->xbutton.y, &output);
		output.data.mouse.buttons = state->mouse_buttons;
		(void)st_console_display_post_event(state->console_display, &output);
		break;
	}
	case SelectionRequest:
		selection_request(state, &event->xselectionrequest);
		break;
	case SelectionClear:
		if (event->xselectionclear.selection == state->selection_atom) {
			free(state->clipboard);
			state->clipboard = NULL;
		}
		break;
	default:
		break;
	}
}

static int
set_pointer_shape(struct x11_state *state,
	enum st_console_pointer_shape shape)
{
	static const unsigned int shapes[] = {
		XC_left_ptr, XC_xterm, XC_hand2,
	};
	Cursor cursor;

	if (shape < ST_CONSOLE_POINTER_DEFAULT || shape > ST_CONSOLE_POINTER_LINK)
		return IO_ERROR_INVALID_ARGUMENT;
	cursor = state->pointer_cursors[shape];
	if (cursor == None) {
		cursor = state->api.create_font_cursor(state->display, shapes[shape]);
		if (cursor == None)
			return IO_ERROR_IO;
		state->pointer_cursors[shape] = cursor;
	}
	state->pointer_shape = shape;
	if (state->pointer_visible)
		(void)state->api.define_cursor(state->display, state->window,
		    cursor);
	return 0;
}

static int
set_pointer_visible(struct x11_state *state, bool visible)
{
	if (visible) {
		state->pointer_visible = true;
		return set_pointer_shape(state, state->pointer_shape);
	}
	if (state->hidden_cursor == None) {
		static const char empty[] = {0};
		Pixmap bitmap = state->api.create_bitmap_from_data(state->display,
		    state->window, empty, 1, 1);
		XColor colour = {0};

		if (bitmap == None)
			return IO_ERROR_IO;
		state->hidden_cursor = state->api.create_pixmap_cursor(
		    state->display, bitmap, bitmap, &colour, &colour, 0, 0);
		(void)state->api.free_pixmap(state->display, bitmap);
		if (state->hidden_cursor == None)
			return IO_ERROR_IO;
	}
	(void)state->api.define_cursor(state->display, state->window,
	    state->hidden_cursor);
	state->pointer_visible = false;
	return 0;
}

static int
set_fullscreen(struct x11_state *state, bool enabled)
{
	XEvent event = {0};

	if (state->net_wm_state == None || state->net_wm_fullscreen == None)
		return IO_ERROR_NOT_SUPPORTED;
	if (enabled == state->fullscreen)
		return 0;
	if (enabled) {
		state->windowed_width = state->window_width;
		state->windowed_height = state->window_height;
	}
	event.xclient.type = ClientMessage;
	event.xclient.display = state->display;
	event.xclient.window = state->window;
	event.xclient.message_type = state->net_wm_state;
	event.xclient.format = 32;
	event.xclient.data.l[0] = enabled ? 1 : 0;
	event.xclient.data.l[1] = (long)state->net_wm_fullscreen;
	event.xclient.data.l[3] = 1;
	if (state->api.send_event(state->display, state->root, False,
	    SubstructureNotifyMask | SubstructureRedirectMask, &event) == 0)
		return IO_ERROR_IO;
	state->fullscreen = enabled;
	if (!enabled)
		(void)state->api.resize_window(state->display, state->window,
		    state->windowed_width, state->windowed_height);
	return 0;
}

static int
set_size_hints(struct x11_state *state, uint32_t width, uint32_t height)
{
	XSizeHints hints = {
		.flags = PMinSize,
		.min_width = (int)width,
		.min_height = (int)height,
	};

	if (width > INT_MAX || height > INT_MAX
	    || state->console->aspect_width > INT_MAX
	    || state->console->aspect_height > INT_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	if (state->console->aspect_width != 0) {
		hints.flags |= PAspect;
		hints.min_aspect.x = (int)state->console->aspect_width;
		hints.min_aspect.y = (int)state->console->aspect_height;
		hints.max_aspect = hints.min_aspect;
	}
	state->api.set_wm_normal_hints(state->display, state->window, &hints);
	return 0;
}

static int
settle_window_resize(struct x11_state *state, uint32_t width,
	uint32_t height)
{
	uint32_t started = milliseconds_now();
	XWindowAttributes attributes;
	struct pollfd descriptor = {
		.fd = state->api.connection_number(state->display),
		.events = POLLIN,
	};

	for (;;) {
		while (state->api.pending(state->display) != 0) {
			XEvent event;

			(void)state->api.next_event(state->display, &event);
			if (!state->api.filter_event(&event, state->window))
				handle_event(state, &event);
		}
		if (state->api.get_window_attributes(state->display,
		    state->window, &attributes) == 0)
			return IO_ERROR_IO;
		if ((uint32_t)attributes.width == width
		    && (uint32_t)attributes.height == height)
			break;
		if ((uint32_t)(milliseconds_now() - started) >= UINT32_C(1000))
			break;
		(void)poll(&descriptor, 1, 10);
	}
	state->window_width = (uint32_t)attributes.width;
	state->window_height = (uint32_t)attributes.height;
	state->windowed_width = state->window_width;
	state->windowed_height = state->window_height;
	return 0;
}

static int
process_command(struct x11_state *state, struct x11_command *command)
{
	switch (command->type) {
	case X11_COMMAND_ACTIVATE:
		state->console = command->data.console;
		return 0;
	case X11_COMMAND_PRESENT:
		return draw_state(state, state->console, false);
	case X11_COMMAND_RESIZE:
		{
		struct st_console_bitmap_geometry next = {
			command->data.size.width, command->data.size.height,
			state->console->aspect_width, state->console->aspect_height};
		uint32_t width;
		uint32_t height;
		uint32_t minimum_width;
		uint32_t minimum_height;
		uint32_t maximum_width;
		uint32_t maximum_height;

		window_pixel_bounds(state, &maximum_width, &maximum_height);
		if (st_console_mode_change_size(&state->window_geometry, &next,
		    state->fullscreen || state->maximized
		    ? state->windowed_width : state->window_width,
		    state->fullscreen || state->maximized ? state->windowed_height
		    : state->window_height, maximum_width, maximum_height, &width, &height,
		    &minimum_width, &minimum_height) != 0
		    || width > UINT_MAX || height > UINT_MAX)
			return IO_ERROR_OUT_OF_RANGE;
		if (set_size_hints(state, minimum_width, minimum_height) != 0)
			return IO_ERROR_OUT_OF_RANGE;
		state->windowed_width = width;
		state->windowed_height = height;
		state->window_geometry = next;
		if (!state->fullscreen && !state->maximized) {
			(void)state->api.resize_window(state->display, state->window,
			    width, height);
			/* XSync only confirms that X processed the request; a window
			 * manager may resize later.  The next activation must use the
			 * resulting geometry, not a stale ConfigureNotify. */
			return settle_window_resize(state, width, height);
		}
		return 0;
		}
	case X11_COMMAND_SET_TITLE:
		set_window_title(state, command->data.text);
		return 0;
	case X11_COMMAND_SET_CLIPBOARD: {
		char *copy = copy_bytes(
		    (const unsigned char *)command->data.text,
		    strlen(command->data.text));

		if (copy == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		free(state->clipboard);
		state->clipboard = copy;
		(void)state->api.set_selection_owner(state->display,
		    state->selection_atom, state->window, CurrentTime);
		return state->api.get_selection_owner(state->display,
		    state->selection_atom) == state->window ? 0 : IO_ERROR_IO;
	}
	case X11_COMMAND_GET_CLIPBOARD:
		return get_clipboard(state, command->data.text_result);
	case X11_COMMAND_SET_POINTER:
		return set_pointer_visible(state, command->data.enabled);
	case X11_COMMAND_SET_POINTER_SHAPE:
		return set_pointer_shape(state, command->data.pointer_shape);
	case X11_COMMAND_SET_FULLSCREEN:
		return set_fullscreen(state, command->data.enabled);
	case X11_COMMAND_GET_SCALING:
		return st_console_bitmap_geometry_scaling(&state->window_geometry,
		    state->window_width, state->window_height,
		    command->data.scaling_result);
	case X11_COMMAND_DESTROY:
		state->stop = true;
		return 0;
	}
	return IO_ERROR_INVALID_ARGUMENT;
}

static void
complete_command(struct x11_state *state, struct x11_command *command,
	int result)
{
	(void)st_mutex_lock(&state->mutex);
	command->result = result;
	command->done = true;
	state->command = NULL;
	(void)st_condition_broadcast(&state->changed);
	(void)st_mutex_unlock(&state->mutex);
}

static void
process_pending_command(struct x11_state *state)
{
	struct x11_command *command;
	char byte;

	while (read(state->wake_pipe[0], &byte, 1) < 0 && errno == EINTR)
		;
	(void)st_mutex_lock(&state->mutex);
	command = state->command;
	(void)st_mutex_unlock(&state->mutex);
	if (command != NULL)
		complete_command(state, command, process_command(state, command));
}

static void
destroy_x11_objects(struct x11_state *state)
{
	if (state->hidden_cursor != None)
		(void)state->api.free_cursor(state->display, state->hidden_cursor);
	for (size_t i = 0; i < sizeof(state->pointer_cursors)
	    / sizeof(state->pointer_cursors[0]); ++i) {
		if (state->pointer_cursors[i] != None)
			(void)state->api.free_cursor(state->display,
			    state->pointer_cursors[i]);
	}
	if (state->input_context != NULL)
		state->api.destroy_ic(state->input_context);
	if (state->input_method != NULL)
		(void)state->api.close_im(state->input_method);
#if ST_CONSOLE_HAVE_XRENDER
	destroy_render_resources(state);
#endif
	destroy_image(state);
	if (state->gc != NULL)
		(void)state->api.free_gc(state->display, state->gc);
	if (state->window != None)
		(void)state->api.destroy_window(state->display, state->window);
	if (state->icon_pixmap != None)
		(void)state->api.free_pixmap(state->display, state->icon_pixmap);
	if (state->icon_mask != None)
		(void)state->api.free_pixmap(state->display, state->icon_mask);
	if (state->display != NULL)
		(void)state->api.close_display(state->display);
	state->gc = NULL;
	state->input_context = NULL;
	state->input_method = NULL;
	state->window = None;
	state->icon_pixmap = None;
	state->icon_mask = None;
	state->display = NULL;
}

static int
create_x11_objects(struct x11_state *state)
{
	static char resource_name[] = "SyncTERM";
	static char resource_class[] = "SyncTERM";
	XGCValues values = {0};
	Atom protocols[2];
	int protocol_count = 0;
	KeyCode escape;
	KeyCode up;
	KeyCode insert;
	int matches = 0;

	state->display = state->api.open_display(NULL);
	if (state->display == NULL)
		return IO_ERROR_NOT_FOUND;
	state->screen = state->api.default_screen(state->display);
	state->root = state->api.root_window(state->display, state->screen);
	load_monitor_apis(state);
	state->visual = state->api.default_visual(state->display, state->screen);
	state->depth = state->api.default_depth(state->display, state->screen);
	for (unsigned int i = 0; i < 256; ++i) {
		state->red_values[i] = component_value(i,
		    state->visual->red_mask);
		state->green_values[i] = component_value(i,
		    state->visual->green_mask);
		state->blue_values[i] = component_value(i,
		    state->visual->blue_mask);
	}
	uint32_t minimum_width;
	uint32_t minimum_height;

	state->window_width = state->console->display->initial_window_width;
	state->window_height = state->console->display->initial_window_height;
	state->windowed_width = state->window_width;
	state->windowed_height = state->window_height;
	if (st_console_aspect_correct_size(state->console->pixels->width,
	    state->console->pixels->height, state->console->aspect_width,
	    state->console->aspect_height, &minimum_width,
	    &minimum_height) != 0
	    || state->window_width > INT_MAX || state->window_height > INT_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	state->window = state->api.create_simple_window(state->display,
	    state->root, 0, 0, state->window_width, state->window_height, 0,
	    state->api.black_pixel(state->display, state->screen),
	    state->api.black_pixel(state->display, state->screen));
	if (state->window == None)
		return IO_ERROR_IO;
	if (set_size_hints(state, minimum_width, minimum_height) != 0)
		return IO_ERROR_OUT_OF_RANGE;
	(void)state->api.set_locale_modifiers("");
	state->input_method = state->api.open_im(state->display, NULL,
	    resource_name, resource_class);
	if (state->input_method != NULL) {
		state->input_context = state->api.create_ic(state->input_method,
		    XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
		    XNClientWindow, state->window, XNFocusWindow, state->window,
		    NULL);
		if (state->input_context == NULL) {
			(void)state->api.close_im(state->input_method);
			state->input_method = NULL;
		}
	}
	values.function = GXcopy;
	values.graphics_exposures = False;
	state->gc = state->api.create_gc(state->display, state->window,
	    GCFunction | GCGraphicsExposures, &values);
	if (state->gc == NULL)
		return IO_ERROR_IO;
	(void)state->api.select_input(state->display, state->window,
	    ExposureMask | StructureNotifyMask | FocusChangeMask | KeyPressMask
	    | KeyReleaseMask | PointerMotionMask | ButtonPressMask
	    | ButtonReleaseMask | PropertyChangeMask);
	state->wm_delete = state->api.intern_atom(state->display,
	    "WM_DELETE_WINDOW", False);
	state->utf8_string = state->api.intern_atom(state->display,
	    "UTF8_STRING", False);
	initialize_ewmh_atoms(state);
	if (state->wm_delete != None)
		protocols[protocol_count++] = state->wm_delete;
	if (state->wm_ping != None)
		protocols[protocol_count++] = state->wm_ping;
	if (protocol_count != 0)
		(void)state->api.set_wm_protocols(state->display, state->window,
		    protocols, protocol_count);
	state->clipboard_atom = state->api.intern_atom(state->display,
	    "CLIPBOARD", False);
	state->selection_atom = state->sommelier
	    && state->clipboard_atom != None ? state->clipboard_atom : XA_PRIMARY;
	state->targets_atom = state->api.intern_atom(state->display,
	    "TARGETS", False);
	state->clipboard_property = state->api.intern_atom(state->display,
	    "ST_CONSOLE_CLIPBOARD", False);
	set_window_title(state, "SyncTERM");
	set_window_identity(state);
	set_window_icon_hints(state);
	set_window_icon(state);
	escape = state->api.keysym_to_keycode(state->display, XK_Escape);
	up = state->api.keysym_to_keycode(state->display, XK_Up);
	insert = state->api.keysym_to_keycode(state->display, XK_Insert);
	if (escape == 9)
		++matches;
	if (up == 111)
		++matches;
	if (insert == 118)
		++matches;
	state->evdev_keycodes = matches >= 2;
	build_keycode_map(state);
#if ST_CONSOLE_HAVE_XRENDER
	(void)load_xrender(state);
	st_console_bitmap_presenter_set_external_scaling_available(
	    state->presenter, state->render_available);
#else
	st_console_bitmap_presenter_set_external_scaling_available(
	    state->presenter, false);
#endif
	(void)state->api.map_window(state->display, state->window);
	(void)state->api.sync(state->display, False);
	state->pointer_visible = true;
	state->pointer_shape = ST_CONSOLE_POINTER_DEFAULT;
	(void)set_pointer_shape(state, state->pointer_shape);
	return draw_state(state, state->console, true);
}

static int
x11_thread(void *argument)
{
	struct x11_state *state = argument;
	struct pollfd descriptors[2];
	int status = create_x11_objects(state);

	(void)st_mutex_lock(&state->mutex);
	state->start_status = status;
	state->started = true;
	(void)st_condition_broadcast(&state->changed);
	(void)st_mutex_unlock(&state->mutex);
	if (status != 0) {
		destroy_x11_objects(state);
		return 1;
	}
	descriptors[0] = (struct pollfd){
		.fd = state->api.connection_number(state->display),
		.events = POLLIN,
	};
	descriptors[1] = (struct pollfd){
		.fd = state->wake_pipe[0],
		.events = POLLIN,
	};
	while (!state->stop) {
		while (state->api.pending(state->display) != 0) {
			XEvent event;

			(void)state->api.next_event(state->display, &event);
			if (!state->api.filter_event(&event, state->window))
				handle_event(state, &event);
		}
		if (poll(descriptors, 2, 20) < 0 && errno != EINTR)
			break;
		if ((descriptors[1].revents & POLLIN) != 0)
			process_pending_command(state);
	}
	(void)st_mutex_lock(&state->mutex);
	state->failed = true;
	if (state->command != NULL) {
		state->command->result = IO_ERROR_IO;
		state->command->done = true;
		state->command = NULL;
	}
	(void)st_condition_broadcast(&state->changed);
	(void)st_mutex_unlock(&state->mutex);
	destroy_x11_objects(state);
	return 0;
}

static int
wake_thread(struct x11_state *state)
{
	char byte = 0;
	ssize_t written;

	do {
		written = write(state->wake_pipe[1], &byte, 1);
	} while (written < 0 && errno == EINTR);
	return written == 1 ? 0 : IO_ERROR_IO;
}

static int
submit_command(struct x11_state *state, struct x11_command *command)
{
	int status;

	status = st_mutex_lock(&state->mutex);
	if (status != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (state->failed) {
		(void)st_mutex_unlock(&state->mutex);
		return IO_ERROR_IO;
	}
	while (state->command != NULL) {
		status = st_condition_wait(&state->changed, &state->mutex);
		if (status != ST_THREAD_SUCCESS) {
			(void)st_mutex_unlock(&state->mutex);
			return IO_ERROR_IO;
		}
		if (state->failed) {
			(void)st_mutex_unlock(&state->mutex);
			return IO_ERROR_IO;
		}
	}
	state->command = command;
	status = wake_thread(state);
	if (status != 0) {
		state->command = NULL;
		(void)st_mutex_unlock(&state->mutex);
		return status;
	}
	/* The queued command borrows the caller's stack until completion. */
	while (!command->done)
		(void)st_condition_wait(&state->changed, &state->mutex);
	status = command->result;
	(void)st_mutex_unlock(&state->mutex);
	return status;
}

static int
x11_activate(st_console_t *console)
{
	return submit_command(console->display->backend_state, &(struct x11_command){
		.type = X11_COMMAND_ACTIVATE,
		.data.console = console,
	});
}

static int
x11_present(st_console_t *console)
{
	return submit_command(console->display->backend_state, &(struct x11_command){
		.type = X11_COMMAND_PRESENT,
	});
}

static int
x11_resize(st_console_t *console, uint32_t columns, uint32_t rows)
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
	return submit_command(console->display->backend_state, &(struct x11_command){
		.type = X11_COMMAND_RESIZE,
		.data.size = {
			.width = width,
			.height = height,
		},
	});
}

static int
x11_set_title(st_console_display_t *display, const char *title)
{
	return submit_command(display->backend_state, &(struct x11_command){
		.type = X11_COMMAND_SET_TITLE,
		.data.text = title,
	});
}

static int
x11_set_clipboard(st_console_display_t *display, const char *text)
{
	return submit_command(display->backend_state, &(struct x11_command){
		.type = X11_COMMAND_SET_CLIPBOARD,
		.data.text = text,
	});
}

static int
x11_get_clipboard(st_console_display_t *display, char **result)
{
	return submit_command(display->backend_state, &(struct x11_command){
		.type = X11_COMMAND_GET_CLIPBOARD,
		.data.text_result = result,
	});
}

static int
x11_set_pointer(st_console_display_t *display, bool visible)
{
	return submit_command(display->backend_state, &(struct x11_command){
		.type = X11_COMMAND_SET_POINTER,
		.data.enabled = visible,
	});
}

static int
x11_set_pointer_shape(st_console_display_t *display,
	enum st_console_pointer_shape shape)
{
	return submit_command(display->backend_state, &(struct x11_command){
		.type = X11_COMMAND_SET_POINTER_SHAPE,
		.data.pointer_shape = shape,
	});
}

static int
x11_set_fullscreen(st_console_display_t *display, bool fullscreen)
{
	return submit_command(display->backend_state, &(struct x11_command){
		.type = X11_COMMAND_SET_FULLSCREEN,
		.data.enabled = fullscreen,
	});
}

static int
x11_get_scaling(st_console_display_t *display, double *result)
{
	return submit_command(display->backend_state, &(struct x11_command){
		.type = X11_COMMAND_GET_SCALING,
		.data.scaling_result = result,
	});
}

static void
x11_destroy(st_console_display_t *display)
{
	struct x11_state *state = display->backend_state;

	(void)submit_command(state, &(struct x11_command){
		.type = X11_COMMAND_DESTROY,
	});
	(void)st_thread_join(state->thread, NULL);
	st_console_bitmap_presenter_destroy(state->presenter);
	free(state->clipboard);
	close(state->wake_pipe[0]);
	close(state->wake_pipe[1]);
	st_condition_destroy(&state->changed);
	st_mutex_destroy(&state->mutex);
	free(state);
	display->backend_state = NULL;
}

static int
x11_attach(st_console_t *console)
{
	int status = st_console_bitmap_validate_geometry(console, INT_MAX);

	if (status != 0)
		return status;
	if (console->aspect_width > INT_MAX || console->aspect_height > INT_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	return 0;
}

static const struct st_console_backend_ops x11_ops = {
	.attach = x11_attach,
	.presents_pixels = true,
	.deferred_present = true,
	.animated_present = true,
	.present = x11_present,
	.resize = x11_resize,
	.set_title = x11_set_title,
	.set_clipboard_text = x11_set_clipboard,
	.get_clipboard_text = x11_get_clipboard,
	.set_pointer_visible = x11_set_pointer,
	.set_pointer_shape = x11_set_pointer_shape,
	.set_fullscreen = x11_set_fullscreen,
	.get_scaling = x11_get_scaling,
	.activate = x11_activate,
	.destroy = x11_destroy,
};

int
st_console_x11_init(st_console_t *console)
{
	struct x11_state *state;
	int status;

	status = st_console_bitmap_validate_geometry(console, INT_MAX);
	if (status != 0)
		return status;
	if (console->aspect_width > INT_MAX || console->aspect_height > INT_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	state = calloc(1, sizeof(*state));
	if (state == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	state->wake_pipe[0] = -1;
	state->wake_pipe[1] = -1;
	state->console_display = console->display;
	state->console = console;
	state->window_geometry = (struct st_console_bitmap_geometry){
		console->pixels->width, console->pixels->height,
		console->aspect_width, console->aspect_height};
	status = load_xlib(state);
	if (status == 0)
		status = enable_xlib_threads(state);
	if (status == 0
	    && st_mutex_init(&state->mutex, ST_MUTEX_PLAIN) != ST_THREAD_SUCCESS)
		status = IO_ERROR_IO;
	else if (status == 0
	    && st_condition_init(&state->changed) != ST_THREAD_SUCCESS) {
		st_mutex_destroy(&state->mutex);
		status = IO_ERROR_IO;
	}
	if (status == 0 && pipe(state->wake_pipe) != 0) {
		st_condition_destroy(&state->changed);
		st_mutex_destroy(&state->mutex);
		status = IO_ERROR_IO;
	}
	if (status == 0)
		status = st_console_bitmap_presenter_create(&state->presenter,
		    console);
	if (status == 0
	    && st_thread_create(&state->thread, x11_thread, state)
	    != ST_THREAD_SUCCESS)
		status = IO_ERROR_IO;
	if (status == 0) {
		(void)st_mutex_lock(&state->mutex);
		while (!state->started)
			(void)st_condition_wait(&state->changed, &state->mutex);
		status = state->start_status;
		(void)st_mutex_unlock(&state->mutex);
		if (status != 0)
			(void)st_thread_join(state->thread, NULL);
	}
	if (status != 0) {
		st_console_bitmap_presenter_destroy(state->presenter);
		if (state->wake_pipe[0] >= 0)
			close(state->wake_pipe[0]);
		if (state->wake_pipe[1] >= 0)
			close(state->wake_pipe[1]);
		if (state->wake_pipe[0] >= 0) {
			st_condition_destroy(&state->changed);
			st_mutex_destroy(&state->mutex);
		}
		free(state);
		return status;
	}
	console->display->backend_state = state;
	console->display->backend_ops = &x11_ops;
	return 0;
}

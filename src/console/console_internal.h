/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_CONSOLE_INTERNAL_H_
#define ST_CONSOLE_INTERNAL_H_

#include <stdatomic.h>

#include "array_list.h"
#include "st_console.h"
#include "st_console_test.h"
#include "st_event.h"
#include "st_threads.h"

/* Private to drawn_cells. Logical cell damage is consumed by presenters. */
#define ST_CONSOLE_RASTER_INVALID (UINT32_C(1) << 31)
_Static_assert((ST_CONSOLE_RASTER_INVALID & (ST_CONSOLE_CELL_PUBLIC_FLAGS | ST_CONSOLE_CELL_DIRTY)) == 0,
    "Raster invalidation must be independent of logical cell flags");

struct st_console_font_registration {
	struct st_console_font_registration *strike;
	uint16_t               identifier;
	struct st_console_font font;
	bool                   owned;
	bool                   fallback;
	atomic_uint_fast64_t   last_used;
};

struct st_console_backend_ops {
	bool presents_pixels;
	bool deferred_present;
	bool animated_present;
	bool pull_present;
	/* Per-console preparation; must not change the physical display. */
	int (*attach)(st_console_t *console);
	int (*present)(st_console_t *console);
	/* Bitmap preparation consumes live state under the console lock. Only
	 * backend-owned images may be used by show_frame after handoff. */
	int (*prepare_frame)(st_console_t *console);
	int (*show_frame)(st_console_display_t *display);
	/* columns/rows specify the requested grid. Console cell dimensions,
	 * raster margins and aspect ratio are staged before resize; reconfigure
	 * has not yet replaced the console's grid, cells or pixels.
	 * The caller holds the console state lock and display backend mutex. */
	int  (*resize)(st_console_t *console, uint32_t columns, uint32_t rows);
	void (*palette_changed)(st_console_t *console, uint16_t first, const uint32_t *colours, uint32_t count);
	uint32_t (*capabilities)(const st_console_t *console);
	int      (*set_title)(st_console_display_t *display, const char *title);
	int      (*set_clipboard_text)(st_console_display_t *display, const char *text);
	int      (*get_clipboard_text)(st_console_display_t *display, char **result);
	int      (*open_url)(st_console_display_t *display, const char *url);
	/* Read an available native mouse-presence signal without an input event. */
	bool (*mouse_present)(const st_console_display_t *display);
	int  (*set_pointer_visible)(st_console_display_t *display, bool visible);
	int  (*set_pointer_shape)(st_console_display_t *display, enum st_console_pointer_shape shape);
	int  (*set_fullscreen)(st_console_display_t *display, bool fullscreen);
	int  (*get_scaling)(st_console_display_t *display, double *scaling);
	/* Synchronously transfer the backend thread's retained console pointer. */
	int (*activate)(st_console_t *console);
	/* Start readers only after the first console is an input destination. */
	int  (*start_input)(st_console_display_t *display);
	void (*destroy)(st_console_display_t *display);
};

struct st_console_display {
	/* Serializes attachment, activation, and removal, before console locks. */
	st_mutex_t     lifecycle_mutex;
	st_mutex_t     backend_mutex;
	st_mutex_t     input_mutex;
	st_mutex_t     presentation_mutex;
	st_condition_t presentation_ready;
	st_thread_t    presentation_thread;
	/* Pinned by the worker under presentation_mutex until a frame finishes. */
	st_console_t           *presentation_active;
	_Atomic(st_console_t *) active;
	/* Published before initial readers start; later activation changes this
	 * selection only after the new display state is installed successfully. */
	_Atomic(st_console_t *) input_active;
	st_console_t           *consoles;
	/* Removed consoles retained while a failed backend may still reference them. */
	st_console_t                        *retired_consoles;
	struct st_console_display_config     config;
	struct st_console_ansi_io            ansi_io;
	uint32_t                             initial_window_width;
	uint32_t                             initial_window_height;
	const struct st_console_backend_ops *backend_ops;
	void                                *backend_state;
	/* Borrowed from the window backend, accessed under backend_mutex.
	 * Native event handlers use their uploaded pixels and copied geometry. */
	struct st_console_bitmap_presenter *window_presenter;
	enum st_console_backend             backend;
	uint8_t                             physical_keys[(UINT16_MAX + 1U) / 8U];
	/* Alt+Keypad composition and physical state are protected by input_mutex. */
	st_console_t *alt_keypad_owner;
	uint32_t      alt_keypad_value;
	bool          alt_keypad_armed;
	bool          alt_keypad_digits;
	bool          alt_keypad_unicode;
	bool          alt_keypad_invalid;
	size_t        console_count;
	bool          backend_initialized;
	/* Backend state could not be restored; only activation may recover it. */
	bool                       backend_failed;
	bool                       presentation_thread_started;
	bool                       presentation_requested;
	bool                       repaint_requested;
	bool                       presentation_stop;
	bool                       presentation_animation_initialized;
	enum st_console_blink_mode presentation_blink_mode;
	uint64_t                   presentation_retry_deadline;
	uint64_t                   presentation_animation_epoch;
	uint64_t                   presentation_animation_deadline;
};

struct st_console_snapshot {
	const st_console_t             *owner;
	uint32_t                        columns;
	uint32_t                        rows;
	uint32_t                        palette[UINT16_MAX + 1U];
	struct st_console_style         style;
	struct st_console_rect          window;
	uint32_t                        cursor_x;
	uint32_t                        cursor_y;
	bool                            wrap;
	bool                            cursor_visible;
	enum st_console_cursor_shape    cursor_shape;
	bool                            cursor_custom;
	struct st_console_custom_cursor custom_cursor;
	struct st_console_pixel_cursor  pixel_cursor;
	enum st_console_blink_mode      blink_mode;
	uint32_t                        video_flags;
	bool                            blocky_scaling;
	bool                            color_emoji;
	uint32_t                        pixel_top;
	bool                            full_surface_scroll;
	bool                           *view_text;
	struct st_console_cell         *cells;
	struct st_pixel_buffer         *pixels;
};

struct st_console {
	struct st_console_indexed_pointer indexed_pointer;
	struct st_console_rect pointer_rect;
	unsigned pointer_skip_x, pointer_skip_y;
	st_rwlock_t state_lock;
	/* Positioned text views share their parent's raster and state lock. Their
	 * own lock is initialized/destroyed normally but is never the raster lock. */
	st_console_t *raster_parent;
	uint64_t      raster_x, raster_y;
	uint32_t      view_columns, view_rows, view_left, view_top;
	uint32_t      view_capacity_columns;
	size_t        raster_children;
	bool          raster_enabled;
	bool          view_hidden, view_follow_x, view_follow_y, view_repaint;
	uint32_t      view_cursor_x, view_cursor_y;
	bool          raster_transient;
	bool          raster_parent_palette;
	st_console_view_output *view_output;
	void                   *view_output_context;
	bool          view_pixels_written;
	/* Only text painted since adoption can be recomposited on this shared surface. */
	uint32_t              view_batch;
	uint32_t             *view_text_batch;
	st_mutex_t            event_mutex;
	st_condition_t        event_ready;
	st_event_t           *activity_waiter; /* Borrowed under event_mutex during the owner wait. */
	st_console_display_t *display;
	st_console_t         *next_display;
	bool                  owns_display;
#if ST_CONSOLE_TEST
	uint64_t test_identifier; /* Assigned only by the optional test driver. */
#endif
	uint32_t                columns;
	uint32_t                rows;
	struct st_console_cell *cells;
	/* Physical row corresponding to logical row zero. */
	uint32_t cell_top;
	/* Whole-row presentation damage; end == 0 means no row damage. */
	uint32_t                dirty_row_first;
	uint32_t                dirty_row_end;
	uint32_t                cell_width;
	uint32_t                cell_height;
	uint32_t                pixel_extra_width;
	uint32_t                pixel_extra_height;
	uint32_t                pixel_top;
	bool                    full_surface_scroll;
	struct st_pixel_buffer *pixels;
	uint32_t                origin_rows, origin_x, origin_y;
	struct st_pixel_buffer *status_pixels;
	bool                    status_visible;
	/* Cell metadata represented by the current contents of pixels. */
	struct st_console_cell         *drawn_cells;
	uint8_t                        *raster_rows;
	uint32_t                        palette[UINT16_MAX + 1U];
	array_list_t                    fonts;
	uint32_t                        next_font_identifier;
	atomic_uint_fast64_t            font_clock;
	struct st_console_style         style;
	struct st_console_rect          window;
	uint32_t                        cursor_x;
	uint32_t                        cursor_y;
	uint32_t                        update_depth;
	bool                            wrap;
	bool                            cursor_visible;
	enum st_console_cursor_shape    cursor_shape;
	bool                            cursor_custom;
	struct st_console_custom_cursor default_cursor;
	struct st_console_custom_cursor custom_cursor;
	struct st_console_pixel_cursor  pixel_cursor;
	enum st_console_blink_mode      blink_mode;
	uint32_t                        video_flags;
	enum st_console_scaling_mode    scaling_mode;
	bool                            blocky_scaling;
	bool                            preserve_window_size;
	bool                            color_emoji;
	uint32_t                        aspect_width;
	uint32_t                        aspect_height;
	bool                            changed;
	bool                            pixel_surface_dirty;
	bool                            pixel_damage_valid;
	struct st_console_rect          pixel_damage;
	/* Text cells have been rasterized since the last cell/raster move. */
	bool                          raster_current;
	bool                          palette_dirty;
	struct st_console_event      *events;
	size_t                        event_capacity;
	size_t                        event_head;
	size_t                        event_count;
	char                         *title;
	bool                          pointer_visible;
	enum st_console_pointer_shape pointer_shape;
};

#if ST_CONSOLE_TEST
int  st_console_test_init(st_console_t *console);
void st_console_test_event_read(st_console_display_t *display);
bool st_console_test_control_pending(st_console_display_t *display);
#endif

static inline uint32_t
st_console_row_index(const st_console_t *console, uint32_t y)
{
	return y >= console->rows - console->cell_top ? y - (console->rows - console->cell_top)
	                                              : y + console->cell_top;
}

static inline size_t
st_console_cell_index(const st_console_t *console, size_t index)
{
	size_t offset = (size_t)console->cell_top * console->columns + index;
	size_t count  = (size_t)console->columns * console->rows;
	return offset >= count ? offset - count : offset;
}

static inline struct st_console_cell *
st_console_cell_at(const st_console_t *console, size_t index)
{
	return console->cells + st_console_cell_index(console, index);
}

static inline uint32_t
st_console_origin_height(const st_console_t *console)
{
	uint32_t rows = console->origin_rows < console->rows ? console->origin_rows : console->rows;
	return rows * console->cell_height;
}

static inline uint32_t
st_console_display_source_y(const st_console_t *console, uint32_t y)
{
	if (!console->origin_y)
		return y;
	uint32_t height = st_console_origin_height(console);
	if (y >= height)
		return y;
	return console->origin_y >= height - y ? UINT32_MAX : y + console->origin_y;
}

static inline const struct st_console_cell *
st_console_display_cell(const st_console_t *console, size_t index)
{
	if (console->origin_y) {
		uint32_t rows = console->origin_rows < console->rows ? console->origin_rows : console->rows;
		uint32_t skip = console->origin_y / console->cell_height;
		if (index < (size_t)rows * console->columns) {
			if (skip >= rows || index >= (size_t)(rows - skip) * console->columns) {
				static const struct st_console_cell blank = {.glyph = ' '};
				return &blank;
			}
			index += (size_t)skip * console->columns;
		}
	}
	return st_console_cell_at(console, index);
}

static inline uint32_t
st_console_display_cursor_y(const st_console_t *console)
{
	uint32_t y = console->cursor_y;
	if (!console->origin_y || y >= console->origin_rows)
		return y;
	uint32_t offset = console->origin_y / console->cell_height;
	return y < offset ? UINT32_MAX : y - offset;
}

static inline struct st_console_cell *
st_console_drawn_at(const st_console_t *console, size_t index)
{
	return console->drawn_cells + st_console_cell_index(console, index);
}

static inline st_rwlock_t *
st_console_state_lock(st_console_t *console)
{
	return console->raster_parent == NULL ? &console->state_lock : &console->raster_parent->state_lock;
}

static inline const struct st_pixel_buffer *
st_console_status_pixels_locked(const st_console_t *console)
{
	const struct st_pixel_buffer *row = console->status_pixels;
	return console->status_visible && row != NULL && row->width == console->columns * console->cell_width
	               && row->height == console->cell_height
		   ? row
		   : NULL;
}

/* Rectangle in the logical page whose pixels have storage on the display. */
static inline struct st_console_rect
st_console_visible_pixels(const st_console_t *console)
{
	if (!console->raster_parent)
		return (struct st_console_rect) {0, 0, console->pixels->width, console->pixels->height};
	const struct st_pixel_buffer *parent = console->raster_parent->pixels;
	if (console->view_hidden)
		return (struct st_console_rect) {0};
	uint32_t w      = console->raster_x < parent->width ? parent->width - (uint32_t)console->raster_x : 0;
	uint32_t h      = console->raster_y < parent->height ? parent->height - (uint32_t)console->raster_y : 0;
	uint32_t width  = console->view_columns * console->cell_width;
	uint32_t height = console->view_rows * console->cell_height;
	return (struct st_console_rect) {console->view_left * console->cell_width,
	    console->view_top * console->cell_height, w < width ? w : width, h < height ? h : height};
}

static inline uint32_t
st_console_raster_colour(const st_console_t *console, uint32_t colour)
{
	if (console->raster_parent == NULL || console->raster_parent_palette)
		return colour;
	uint32_t rgb = ST_CONSOLE_COLOR_IS_RGB(colour) ? colour & UINT32_C(0xffffff)
	               : colour <= UINT16_MAX          ? console->palette[colour]
	                                               : 0;
	return ST_CONSOLE_COLOR_RGB_TAG | ST_CONSOLE_COLOR_VIEW_RGB | rgb;
}

static inline void
st_console_raster_cell_clip(const st_console_t *console, uint32_t x, uint32_t y, uint32_t *width, uint32_t *height)
{
	*width  = console->cell_width;
	*height = console->cell_height;
	if (console->raster_parent == NULL)
		return;
	struct st_console_rect v = st_console_visible_pixels(console);
	x                        = x * console->cell_width - v.x;
	y                        = y * console->cell_height - v.y;
	if (x >= v.width || y >= v.height) {
		*width = *height = 0;
		return;
	}
	if (*width > v.width - x)
		*width = v.width - x;
	if (*height > v.height - y)
		*height = v.height - y;
}

static inline bool
st_console_row_dirty(const st_console_t *console, uint32_t y)
{
	return y >= console->dirty_row_first && y < console->dirty_row_end;
}

static inline void
st_console_dirty_rows(st_console_t *console, uint32_t first, uint32_t end)
{
	if (console->dirty_row_end == 0 || first < console->dirty_row_first)
		console->dirty_row_first = first;
	if (end > console->dirty_row_end)
		console->dirty_row_end = end;
	console->changed = true;
}

static inline bool
st_console_cells_equal(const struct st_console_cell *left, const struct st_console_cell *right)
{
	return left->glyph == right->glyph && left->foreground == right->foreground
	       && left->background == right->background
	       && ((left->flags ^ right->flags) & ST_CONSOLE_CELL_PUBLIC_FLAGS) == 0 && left->font == right->font
	       && left->glyph_font == right->glyph_font && left->hyperlink == right->hyperlink;
}

static inline bool
st_console_colour_rgb_locked(const st_console_t *console, uint32_t colour, uint32_t *rgb)
{
	if (ST_CONSOLE_COLOR_IS_RGB(colour)) {
		*rgb = colour & UINT32_C(0xffffff);
		return true;
	}
	if (colour <= UINT16_MAX) {
		*rgb = console->palette[colour];
		return true;
	}
	*rgb = 0;
	return false;
}

static inline void
st_console_effective_cell_locked(const st_console_t *console, const struct st_console_cell *source,
    struct st_console_cell *effective)
{
	*effective = *source;
	/* Reveal overrides conceal for every presenter, not just bitmap glyphs.
	 * Keep the retained flags intact so conceal can be restored later. */
	if ((source->flags & ST_CONSOLE_CELL_REVEAL) != 0)
		effective->flags &= ~ST_CONSOLE_CELL_CONCEAL;
	if ((source->flags & ST_CONSOLE_CELL_LEGACY_FOREGROUND) != 0
	    && !ST_CONSOLE_COLOR_IS_RGB(source->foreground) && source->foreground < 8
	    && (source->flags & ST_CONSOLE_CELL_BRIGHT) != 0
	    && (console->video_flags & ST_CONSOLE_VIDEO_NO_BRIGHT) == 0)
		effective->foreground = source->foreground + 8;
	if ((source->flags & ST_CONSOLE_CELL_LEGACY_BACKGROUND) != 0
	    && !ST_CONSOLE_COLOR_IS_RGB(source->background) && source->background < 8
	    && (source->flags & ST_CONSOLE_CELL_BLINK) != 0
	    && (console->video_flags & ST_CONSOLE_VIDEO_BRIGHT_BACKGROUND) != 0)
		effective->background = source->background + 8;
	if ((console->video_flags & ST_CONSOLE_VIDEO_NO_BLINK) != 0)
		effective->flags &= ~ST_CONSOLE_CELL_BLINK;
}

static inline uint16_t
st_console_cell_glyph_font(const struct st_console_cell *cell)
{
	return cell->glyph_font;
}

static inline bool
st_console_rects_equal(const struct st_console_rect *left, const struct st_console_rect *right)
{
	return left->x == right->x && left->y == right->y && left->width == right->width
	       && left->height == right->height;
}

static inline void
st_console_rect_accumulate(struct st_console_rect *damage, bool *valid, const struct st_console_rect *rectangle)
{
	uint32_t right  = rectangle->x + rectangle->width;
	uint32_t bottom = rectangle->y + rectangle->height;

	if (!*valid) {
		*damage = *rectangle;
		*valid  = true;
		return;
	}
	if (rectangle->x < damage->x) {
		damage->width += damage->x - rectangle->x;
		damage->x = rectangle->x;
	}
	if (rectangle->y < damage->y) {
		damage->height += damage->y - rectangle->y;
		damage->y = rectangle->y;
	}
	if (right > damage->x + damage->width)
		damage->width = right - damage->x;
	if (bottom > damage->y + damage->height)
		damage->height = bottom - damage->y;
}

int st_console_ansi_init(st_console_t *console, const struct st_console_ansi_io *io);
/* Posts a native physical edge and returns whether numeric entry owns it.
 * A consumed edge must not also trigger native shortcuts or translated input. */
bool st_console_display_post_key_event(st_console_display_t *display, const struct st_console_event *event);
bool st_console_control_key(uint32_t character, uint16_t *key);
unsigned char st_console_ascii_character(st_console_t *console, const struct st_console_cell *cell,
    bool *alternate);
unsigned char st_console_cp437_character(uint32_t codepoint);
bool          st_console_cell_codepoint_locked(st_console_t *console, const struct st_console_cell *cell,
    uint32_t *codepoint);
int           st_console_ncurses_init(st_console_t *console);
int           st_console_sdl_init(st_console_t *console);
int           st_console_x11_init(st_console_t *console);
int           st_console_wayland_init(st_console_t *console);
int           st_console_win32_init(st_console_t *console);
int           st_console_gdi_init(st_console_t *console);
int           st_console_quartz_init(st_console_t *console);
int           st_console_libretro_init(st_console_t *console);
int           st_console_sdl_run_application(int argc, char **argv, int (*application_main)(int, char **));
int           st_console_quartz_run_application(int argc, char **argv, int (*application_main)(int, char **));
void          st_console_quartz_pump(void);
int           st_console_pull_frame(st_console_display_t *display);
int           st_console_present_locked(st_console_t *console, bool forced);
st_console_t *st_console_display_active_console(st_console_t *console);
int           st_console_start_presentation_worker(st_console_t *console);
void          st_console_display_request_repaint(st_console_display_t *display);
void          st_console_display_reset_animation(st_console_display_t *display, enum st_console_blink_mode mode,
    bool restart);
void          st_console_follow_view_locked(st_console_t *, uint32_t x, uint32_t y);
void          st_console_mark_all_locked(st_console_t *console);
void          st_console_cursor_span_locked(const st_console_t *console, uint32_t *x, uint32_t *columns);
bool          st_console_rect_valid(const st_console_t *console, const struct st_console_rect *rect);
struct st_console_cell st_console_blank_locked(const st_console_t *console, uint32_t codepoint);
int                    st_console_finish_change_locked(st_console_t *console);
int                    st_console_publish_raster_view_locked(st_console_t *console);
void st_console_store_cell_locked(st_console_t *console, size_t index, const struct st_console_cell *source);
void st_console_view_pixels_written_locked(st_console_t *console, uint32_t x, uint32_t y, uint32_t width,
    uint32_t height);
void st_console_set_cell_locked(st_console_t *console, size_t index, const struct st_console_cell *source);
const struct st_console_font        *st_console_font_locked(st_console_t *console, uint16_t identifier);
const struct st_console_color_glyph *st_console_font_color_glyph(const struct st_console_font *font,
    uint32_t                                                                                   glyph);
const struct st_console_font *st_console_resolve_font_raster_locked(st_console_t *console,
    uint16_t identifier, const struct st_console_font **source);
bool   st_console_font_remap_glyph(const struct st_console_font *source, const struct st_console_font *destination,
    uint32_t *glyph);
void   st_console_invalidate_text_locked(st_console_t *console);
bool   st_console_font_glyph(const struct st_console_font *font, uint32_t glyph, const uint8_t **bitmap,
    uint32_t *width, size_t *row_stride);
bool   st_console_font_glyph_locked(st_console_t *console, uint16_t identifier, uint32_t glyph,
    const uint8_t **bitmap, uint32_t *width, size_t *row_stride);
bool   st_console_font_lookup_locked(st_console_t *console, uint16_t identifier, uint32_t codepoint,
    uint16_t *glyph_font, uint32_t *glyph, uint8_t *columns);
bool   st_console_font_lookup_cluster_locked(st_console_t *console, uint16_t identifier, const char *text,
    size_t text_size, uint8_t columns, uint16_t *glyph_font, uint32_t *glyph, bool *missing);
bool   st_console_font_glyph_text_locked(st_console_t *console, uint16_t identifier, uint32_t glyph, char *text,
    size_t text_size, size_t *required);
int    st_console_rasterize_locked(st_console_t *console, bool force);
size_t st_console_pixel_position_locked(const st_console_t *console, uint32_t x, uint32_t y);
void st_console_copy_pixels_locked(const st_console_t *console, uint32_t *destination, uint32_t destination_stride,
    bool alternate, const struct st_console_rect *rectangle);
int  st_console_normalize_raster_locked(st_console_t *console);
void st_console_move_raster_locked(st_console_t *console, const struct st_console_rect *source,
    uint32_t destination_x, uint32_t destination_y);
void st_console_move_pixels_locked(st_console_t *console, const struct st_console_rect *source,
    uint32_t destination_x, uint32_t destination_y);

struct st_console_bitmap_presenter;

struct st_console_bitmap_frame {
	const struct st_rgb_buffer  *pixels;
	const void                  *image_identity;
	uint64_t                     generation;
	struct st_console_rect       destination;
	uint32_t                     source_width;
	uint32_t                     source_height;
	uint32_t                     cell_width;
	uint32_t                     cell_height;
	enum st_console_scaling_mode scaling_mode;
	bool                         changed;
};

typedef int (*st_console_bitmap_upload_t)(void *context, const struct st_rgb_buffer *pixels,
    const struct st_console_rect *rectangle, const void *image_identity, uint64_t generation);

int st_console_aspect_correct_size(uint32_t source_width, uint32_t source_height, uint32_t aspect_width,
    uint32_t aspect_height, uint32_t *width, uint32_t *height);
int st_console_bitmap_validate_geometry(const st_console_t *console, uint32_t maximum_dimension);
int st_console_scaled_window_size(uint32_t source_width, uint32_t source_height, uint32_t aspect_width,
    uint32_t aspect_height, double scaling, uint32_t *width, uint32_t *height);

/* Raster geometry and window-sizing policy represented by the backend. */
struct st_console_bitmap_geometry {
	uint32_t width, height;
	uint32_t aspect_width, aspect_height;
	bool     preserve_window_size;
};

int st_console_snap_geometry_size(const struct st_console_bitmap_geometry *geometry, uint32_t current_width,
    uint32_t current_height, bool larger, uint32_t maximum_width, uint32_t maximum_height, uint32_t *width,
    uint32_t *height);
int st_console_preserve_geometry_scale(const struct st_console_bitmap_geometry *previous,
    const struct st_console_bitmap_geometry *next, uint32_t current_width, uint32_t current_height,
    uint32_t *width, uint32_t *height, uint32_t *minimum_width, uint32_t *minimum_height);
/* Mode changes retain approximate physical size, as opposed to the
 * continuous-resize fitting helper above. Zero limits mean unrestricted. */
int  st_console_mode_change_size(const struct st_console_bitmap_geometry *previous,
    const struct st_console_bitmap_geometry *next, uint32_t current_width, uint32_t current_height,
    uint32_t maximum_width, uint32_t maximum_height, uint32_t *width, uint32_t *height, uint32_t *minimum_width,
    uint32_t *minimum_height);
int  st_console_bitmap_geometry_scaling(const struct st_console_bitmap_geometry *geometry, uint32_t output_width,
    uint32_t output_height, double *scaling);
int  st_console_bitmap_presenter_create(struct st_console_bitmap_presenter **result, const st_console_t *console);
void st_console_bitmap_presenter_destroy(struct st_console_bitmap_presenter *presenter);
/* Set before the first frame; unavailable acceleration uses software scaling. */
void st_console_bitmap_presenter_set_external_scaling_available(struct st_console_bitmap_presenter *presenter,
    bool                                                                                            available);
void st_console_bitmap_presenter_set_local_animation_clock(struct st_console_bitmap_presenter *presenter,
    bool                                                                                       local);
int  st_console_bitmap_prepare_window(st_console_t *console);
bool st_console_bitmap_retained_frame(struct st_console_bitmap_presenter *presenter, bool repaint);
int  st_console_bitmap_prepare_frame(struct st_console_bitmap_presenter *presenter, const st_console_t *console,
    uint32_t now);
/*
 * console may be NULL for a phase selection or host-only resize.  The image
 * identity is stable for the presenter's lifetime; generations change only
 * when composed image content changes.
 */
int  st_console_bitmap_presenter_frame(struct st_console_bitmap_presenter *presenter, const st_console_t *console,
    uint32_t output_width, uint32_t output_height, uint32_t now, st_console_bitmap_upload_t upload, void *context,
    struct st_console_bitmap_frame *frame);
void st_console_bitmap_mouse_position(const struct st_console_bitmap_frame *frame, int64_t x, int64_t y,
    struct st_console_event *event);

#endif

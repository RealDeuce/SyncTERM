/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_CONSOLE_INTERNAL_H_
#define ST_CONSOLE_INTERNAL_H_

#include "st_console.h"
#include "st_threads.h"

#include "array_list.h"

#include <stdatomic.h>

struct st_console_font_registration {
	uint16_t identifier;
	struct st_console_font font;
	bool owned;
	atomic_uint_fast64_t last_used;
};

struct st_console_backend_ops {
	bool presents_pixels;
	bool deferred_present;
	bool animated_present;
	/* Per-console preparation; must not change the physical display. */
	int (*attach)(st_console_t *console);
	int (*present)(st_console_t *console);
	/* columns/rows specify the requested grid. Console cell dimensions,
	 * raster margins and aspect ratio are staged before resize; reconfigure
	 * has not yet replaced the console's grid, cells or pixels.
	 * The caller holds the console state lock and display backend mutex. */
	int (*resize)(st_console_t *console, uint32_t columns, uint32_t rows);
	void (*palette_changed)(st_console_t *console, uint16_t first,
	    const uint32_t *colours, uint32_t count);
	uint32_t (*capabilities)(const st_console_t *console);
	int (*set_title)(st_console_display_t *display, const char *title);
	int (*set_clipboard_text)(st_console_display_t *display, const char *text);
	int (*get_clipboard_text)(st_console_display_t *display, char **result);
	int (*open_url)(st_console_display_t *display, const char *url);
	int (*set_pointer_visible)(st_console_display_t *display, bool visible);
	int (*set_pointer_shape)(st_console_display_t *display,
	    enum st_console_pointer_shape shape);
	int (*set_fullscreen)(st_console_display_t *display, bool fullscreen);
	int (*get_scaling)(st_console_display_t *display, double *scaling);
	/* Synchronously transfer the backend thread's retained console pointer. */
	int (*activate)(st_console_t *console);
	void (*destroy)(st_console_display_t *display);
};

struct st_console_display {
	/* Serializes attachment, activation, and removal, before console locks. */
	st_mutex_t lifecycle_mutex;
	st_mutex_t backend_mutex;
	st_mutex_t input_mutex;
	st_mutex_t presentation_mutex;
	st_condition_t presentation_ready;
	st_thread_t presentation_thread;
	/* Pinned by the worker under presentation_mutex until a frame finishes. */
	st_console_t *presentation_active;
	_Atomic(st_console_t *) active;
	/* Published selection; unchanged until activation finishes successfully. */
	_Atomic(st_console_t *) input_active;
	st_console_t *consoles;
	/* Removed consoles retained while a failed backend may still reference them. */
	st_console_t *retired_consoles;
	struct st_console_display_config config;
	struct st_console_ansi_io ansi_io;
	uint32_t initial_window_width;
	uint32_t initial_window_height;
	const struct st_console_backend_ops *backend_ops;
	void *backend_state;
	enum st_console_backend backend;
	uint8_t physical_keys[(UINT16_MAX + 1U) / 8U];
	size_t console_count;
	bool backend_initialized;
	/* Backend state could not be restored; only activation may recover it. */
	bool backend_failed;
	bool presentation_thread_started;
	bool presentation_requested;
	bool repaint_requested;
	bool presentation_stop;
	bool presentation_animation_initialized;
	enum st_console_blink_mode presentation_blink_mode;
	uint64_t presentation_content_deadline;
	uint64_t presentation_animation_epoch;
	uint64_t presentation_animation_deadline;
};

struct st_console_snapshot {
	const st_console_t *owner;
	uint32_t columns;
	uint32_t rows;
	uint32_t palette[UINT16_MAX + 1U];
	struct st_console_style style;
	struct st_console_rect window;
	uint32_t cursor_x;
	uint32_t cursor_y;
	bool wrap;
	bool cursor_visible;
	enum st_console_cursor_shape cursor_shape;
	bool cursor_custom;
	struct st_console_custom_cursor custom_cursor;
	enum st_console_blink_mode blink_mode;
	uint32_t video_flags;
	bool blocky_scaling;
	uint32_t pixel_top;
	bool full_surface_scroll;
	struct st_console_cell *cells;
	struct st_pixel_buffer *pixels;
};

struct st_console {
	st_rwlock_t state_lock;
	st_mutex_t event_mutex;
	st_condition_t event_ready;
	st_console_display_t *display;
	st_console_t *next_display;
	bool owns_display;
	uint32_t columns;
	uint32_t rows;
	struct st_console_cell *cells;
	/* Physical row corresponding to logical row zero. */
	uint32_t cell_top;
	/* Whole-row presentation damage; end == 0 means no row damage. */
	uint32_t dirty_row_first;
	uint32_t dirty_row_end;
	uint32_t cell_width;
	uint32_t cell_height;
	uint32_t pixel_extra_width;
	uint32_t pixel_extra_height;
	uint32_t pixel_top;
	bool full_surface_scroll;
	struct st_pixel_buffer *pixels;
	/* Cell metadata represented by the current contents of pixels. */
	struct st_console_cell *drawn_cells;
	uint8_t *raster_rows;
	uint32_t palette[UINT16_MAX + 1U];
	array_list_t fonts;
	uint32_t next_font_identifier;
	atomic_uint_fast64_t font_clock;
	struct st_console_style style;
	struct st_console_rect window;
	uint32_t cursor_x;
	uint32_t cursor_y;
	uint32_t update_depth;
	bool wrap;
	bool cursor_visible;
	enum st_console_cursor_shape cursor_shape;
	bool cursor_custom;
	struct st_console_custom_cursor default_cursor;
	struct st_console_custom_cursor custom_cursor;
	enum st_console_blink_mode blink_mode;
	uint32_t video_flags;
	enum st_console_scaling_mode scaling_mode;
	bool blocky_scaling;
	uint32_t aspect_width;
	uint32_t aspect_height;
	bool changed;
	bool pixel_surface_dirty;
	/* Text cells have been rasterized since the last cell/raster move. */
	bool raster_current;
	bool palette_dirty;
	struct st_console_event *events;
	size_t event_capacity;
	size_t event_head;
	size_t event_count;
	char *title;
	bool pointer_visible;
	enum st_console_pointer_shape pointer_shape;
};

static inline uint32_t
st_console_row_index(const st_console_t *console, uint32_t y)
{
	return y >= console->rows - console->cell_top
	    ? y - (console->rows - console->cell_top) : y + console->cell_top;
}

static inline size_t
st_console_cell_index(const st_console_t *console, size_t index)
{
	size_t offset = (size_t)console->cell_top * console->columns + index;
	size_t count = (size_t)console->columns * console->rows;
	return offset >= count ? offset - count : offset;
}

static inline struct st_console_cell *
st_console_cell_at(const st_console_t *console, size_t index)
{
	return console->cells + st_console_cell_index(console, index);
}

static inline struct st_console_cell *
st_console_drawn_at(const st_console_t *console, size_t index)
{
	return console->drawn_cells + st_console_cell_index(console, index);
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
st_console_cells_equal(const struct st_console_cell *left,
	const struct st_console_cell *right)
{
	return left->glyph == right->glyph
	    && left->foreground == right->foreground
	    && left->background == right->background
	    && ((left->flags ^ right->flags) & ST_CONSOLE_CELL_PUBLIC_FLAGS) == 0
	    && left->font == right->font
	    && left->glyph_font == right->glyph_font
	    && left->hyperlink == right->hyperlink;
}

static inline bool
st_console_colour_rgb_locked(const st_console_t *console, uint32_t colour,
	uint32_t *rgb)
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
st_console_effective_cell_locked(const st_console_t *console,
	const struct st_console_cell *source, struct st_console_cell *effective)
{
	*effective = *source;
	if ((source->flags & ST_CONSOLE_CELL_LEGACY_FOREGROUND) != 0
	    && !ST_CONSOLE_COLOR_IS_RGB(source->foreground)
	    && source->foreground < 8
	    && (source->flags & ST_CONSOLE_CELL_BRIGHT) != 0
	    && (console->video_flags & ST_CONSOLE_VIDEO_NO_BRIGHT) == 0)
		effective->foreground = source->foreground + 8;
	if ((source->flags & ST_CONSOLE_CELL_LEGACY_BACKGROUND) != 0
	    && !ST_CONSOLE_COLOR_IS_RGB(source->background)
	    && source->background < 8
	    && (source->flags & ST_CONSOLE_CELL_BLINK) != 0
	    && (console->video_flags
	    & ST_CONSOLE_VIDEO_BRIGHT_BACKGROUND) != 0)
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
st_console_rects_equal(const struct st_console_rect *left,
	const struct st_console_rect *right)
{
	return left->x == right->x && left->y == right->y
	    && left->width == right->width && left->height == right->height;
}

static inline void
st_console_rect_accumulate(struct st_console_rect *damage, bool *valid,
	const struct st_console_rect *rectangle)
{
	uint32_t right = rectangle->x + rectangle->width;
	uint32_t bottom = rectangle->y + rectangle->height;

	if (!*valid) {
		*damage = *rectangle;
		*valid = true;
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

int st_console_ansi_init(st_console_t *console,
	const struct st_console_ansi_io *io);
bool st_console_control_key(uint32_t character, uint16_t *key);
unsigned char st_console_ascii_character(st_console_t *console,
	const struct st_console_cell *cell, bool *alternate);
unsigned char st_console_cp437_character(uint32_t codepoint);
bool st_console_cell_codepoint_locked(st_console_t *console,
	const struct st_console_cell *cell, uint32_t *codepoint);
int st_console_ncurses_init(st_console_t *console);
int st_console_sdl_init(st_console_t *console);
int st_console_x11_init(st_console_t *console);
int st_console_wayland_init(st_console_t *console);
int st_console_win32_init(st_console_t *console);
int st_console_gdi_init(st_console_t *console);
int st_console_quartz_init(st_console_t *console);
int st_console_libretro_init(st_console_t *console);
int st_console_sdl_run_application(int argc, char **argv,
	int (*application_main)(int, char **));
int st_console_quartz_run_application(int argc, char **argv,
	int (*application_main)(int, char **));
void st_console_quartz_pump(void);
int st_console_present_locked(st_console_t *console, bool forced);
st_console_t *st_console_display_active_console(st_console_t *console);
int st_console_start_presentation_worker(st_console_t *console);
void st_console_display_request_repaint(st_console_display_t *display);
void st_console_display_reset_animation(st_console_display_t *display,
	enum st_console_blink_mode mode);
void st_console_mark_all_locked(st_console_t *console);
void st_console_cursor_span_locked(const st_console_t *console,
	uint32_t *x, uint32_t *columns);
bool st_console_rect_valid(const st_console_t *console,
	const struct st_console_rect *rect);
struct st_console_cell st_console_blank_locked(const st_console_t *console,
	uint32_t codepoint);
int st_console_finish_change_locked(st_console_t *console);
void st_console_set_cell_locked(st_console_t *console, size_t index,
	const struct st_console_cell *source);
const struct st_console_font *st_console_font_locked(st_console_t *console,
	uint16_t identifier);
bool st_console_font_glyph(const struct st_console_font *font,
	uint32_t glyph, const uint8_t **bitmap, uint32_t *width,
	size_t *row_stride);
bool st_console_font_glyph_locked(st_console_t *console, uint16_t identifier,
	uint32_t glyph, const uint8_t **bitmap, uint32_t *width,
	size_t *row_stride);
bool st_console_font_lookup_locked(st_console_t *console,
	uint16_t identifier, uint32_t codepoint, uint16_t *glyph_font,
	uint32_t *glyph, uint8_t *columns);
bool st_console_font_lookup_cluster_locked(st_console_t *console,
	uint16_t identifier, const char *text, size_t text_size, uint8_t columns,
	uint16_t *glyph_font, uint32_t *glyph);
bool st_console_font_glyph_text_locked(st_console_t *console,
	uint16_t identifier, uint32_t glyph, char *text, size_t text_size,
	size_t *required);
int st_console_rasterize_locked(st_console_t *console, bool force);
size_t st_console_pixel_position_locked(const st_console_t *console,
	uint32_t x, uint32_t y);
void st_console_copy_pixels_locked(const st_console_t *console,
	uint32_t *destination, uint32_t destination_stride, bool alternate,
	const struct st_console_rect *rectangle);
int st_console_normalize_raster_locked(st_console_t *console);
void st_console_move_raster_locked(st_console_t *console,
	const struct st_console_rect *source, uint32_t destination_x,
	uint32_t destination_y);
void st_console_move_pixels_locked(st_console_t *console,
	const struct st_console_rect *source, uint32_t destination_x,
	uint32_t destination_y);

struct st_console_bitmap_presenter;

struct st_console_bitmap_frame {
	const struct st_rgb_buffer *pixels;
	const void *image_identity;
	uint64_t generation;
	struct st_console_rect destination;
	uint32_t source_width;
	uint32_t source_height;
	uint32_t cell_width;
	uint32_t cell_height;
	enum st_console_scaling_mode scaling_mode;
	bool changed;
};

typedef int (*st_console_bitmap_upload_t)(void *context,
	const struct st_rgb_buffer *pixels,
	const struct st_console_rect *rectangle, const void *image_identity,
	uint64_t generation);

int st_console_aspect_correct_size(uint32_t source_width,
	uint32_t source_height, uint32_t aspect_width, uint32_t aspect_height,
	uint32_t *width, uint32_t *height);
int st_console_bitmap_validate_geometry(const st_console_t *console,
	uint32_t maximum_dimension);
int st_console_scaled_window_size(uint32_t source_width,
	uint32_t source_height, uint32_t aspect_width, uint32_t aspect_height,
	double scaling, uint32_t *width, uint32_t *height);
/* Geometry represented by the current window size, independent of selection. */
struct st_console_bitmap_geometry {
	uint32_t width, height;
	uint32_t aspect_width, aspect_height;
};
int st_console_snap_geometry_size(const struct st_console_bitmap_geometry *geometry,
	uint32_t current_width, uint32_t current_height, bool larger,
	uint32_t maximum_width, uint32_t maximum_height,
	uint32_t *width, uint32_t *height);
int st_console_preserve_geometry_scale(
	const struct st_console_bitmap_geometry *previous,
	const struct st_console_bitmap_geometry *next,
	uint32_t current_width, uint32_t current_height,
	uint32_t *width, uint32_t *height,
	uint32_t *minimum_width, uint32_t *minimum_height);
int st_console_bitmap_geometry_scaling(
	const struct st_console_bitmap_geometry *geometry,
	uint32_t output_width, uint32_t output_height, double *scaling);
int st_console_bitmap_presenter_create(
	struct st_console_bitmap_presenter **result, const st_console_t *console);
void st_console_bitmap_presenter_destroy(
	struct st_console_bitmap_presenter *presenter);
/* Set before the first frame; unavailable acceleration uses software scaling. */
void st_console_bitmap_presenter_set_external_scaling_available(
	struct st_console_bitmap_presenter *presenter, bool available);
/*
 * console may be NULL for a phase selection or host-only resize.  The image
 * identity is stable for the presenter's lifetime; generations change only
 * when composed image content changes.
 */
int st_console_bitmap_presenter_frame(
	struct st_console_bitmap_presenter *presenter,
	const st_console_t *console, uint32_t output_width,
	uint32_t output_height, uint32_t now,
	st_console_bitmap_upload_t upload, void *context,
	struct st_console_bitmap_frame *frame);
void st_console_bitmap_mouse_position(
	const struct st_console_bitmap_frame *frame, int64_t x, int64_t y,
	struct st_console_event *event);

#endif

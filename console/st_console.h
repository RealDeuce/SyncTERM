/* Character-generator and host-console abstraction. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_CONSOLE_H_
#define ST_CONSOLE_H_

#include "st_pixels.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

typedef struct st_console st_console_t;
typedef struct st_console_snapshot st_console_snapshot_t;

/* Indexed colours use values 0 through 255. RGB colours have this tag. */
#define ST_CONSOLE_COLOR_RGB_TAG UINT32_C(0x80000000)
#define ST_CONSOLE_COLOR_RGB_VALUE(red, green, blue) \
	(ST_CONSOLE_COLOR_RGB_TAG | (((uint32_t)(red) & UINT32_C(0xff)) << 16) \
	    | (((uint32_t)(green) & UINT32_C(0xff)) << 8) \
	    | ((uint32_t)(blue) & UINT32_C(0xff)))
#define ST_CONSOLE_COLOR_IS_RGB(colour) \
	(((colour) & ST_CONSOLE_COLOR_RGB_TAG) != 0)

/*
 * Cell flags are integer masks rather than C bit-fields so their layout is
 * independent of the implementation.  DIRTY is maintained by the library.
 * SAA5050_CONTROL records the source control character separately from the
 * glyph in codepoint; the displayed glyph can therefore remain a font index.
 */
#define ST_CONSOLE_CELL_DIRTY             (UINT32_C(1) << 0)
#define ST_CONSOLE_CELL_BLINK             (UINT32_C(1) << 1)
#define ST_CONSOLE_CELL_CONCEAL           (UINT32_C(1) << 2)
#define ST_CONSOLE_CELL_PIXEL_GRAPHICS    (UINT32_C(1) << 3)
#define ST_CONSOLE_CELL_DOUBLE_HEIGHT     (UINT32_C(1) << 4)
#define ST_CONSOLE_CELL_SAA5050           (UINT32_C(1) << 5)
#define ST_CONSOLE_CELL_REVEAL            (UINT32_C(1) << 6)
#define ST_CONSOLE_CELL_SEPARATED         (UINT32_C(1) << 7)
#define ST_CONSOLE_CELL_TERMINAL_FONT     (UINT32_C(1) << 8)
#define ST_CONSOLE_CELL_SAA5050_CONTROL_SHIFT 9
#define ST_CONSOLE_CELL_SAA5050_CONTROL_MASK \
	(UINT32_C(0x7f) << ST_CONSOLE_CELL_SAA5050_CONTROL_SHIFT)
#define ST_CONSOLE_CELL_SAA5050_CONTROL(value) \
	(((uint32_t)(value) & UINT32_C(0x7f)) \
	    << ST_CONSOLE_CELL_SAA5050_CONTROL_SHIFT)
#define ST_CONSOLE_CELL_UNDERLINE         (UINT32_C(1) << 16)
#define ST_CONSOLE_CELL_BOLD              (UINT32_C(1) << 17)
#define ST_CONSOLE_CELL_REVERSE           (UINT32_C(1) << 18)
#define ST_CONSOLE_CELL_CONTINUATION      (UINT32_C(1) << 19)
#define ST_CONSOLE_CELL_EXPLICIT_GLYPH    (UINT32_C(1) << 20)
/* Font zero supplies glyph; font still records the selected registration. */
#define ST_CONSOLE_CELL_FALLBACK_FONT     (UINT32_C(1) << 21)
#define ST_CONSOLE_CELL_PUBLIC_FLAGS \
	(ST_CONSOLE_CELL_BLINK | ST_CONSOLE_CELL_CONCEAL \
	    | ST_CONSOLE_CELL_PIXEL_GRAPHICS \
	    | ST_CONSOLE_CELL_DOUBLE_HEIGHT | ST_CONSOLE_CELL_SAA5050 \
	    | ST_CONSOLE_CELL_REVEAL | ST_CONSOLE_CELL_SEPARATED \
	    | ST_CONSOLE_CELL_TERMINAL_FONT \
	    | ST_CONSOLE_CELL_SAA5050_CONTROL_MASK \
	    | ST_CONSOLE_CELL_UNDERLINE | ST_CONSOLE_CELL_BOLD \
	    | ST_CONSOLE_CELL_REVERSE | ST_CONSOLE_CELL_CONTINUATION \
	    | ST_CONSOLE_CELL_EXPLICIT_GLYPH \
	    | ST_CONSOLE_CELL_FALLBACK_FONT)

struct st_console_cell {
	uint32_t codepoint;
	uint32_t glyph;
	uint32_t foreground;
	uint32_t background;
	uint32_t flags;
	uint16_t font;
	uint16_t hyperlink;
};

struct st_console_font_mapping {
	uint32_t codepoint;
	uint32_t glyph;
};

struct st_console_rect {
	uint32_t x;
	uint32_t y;
	uint32_t width;
	uint32_t height;
};

struct st_console_style {
	uint32_t foreground;
	uint32_t background;
	uint32_t flags;
	uint16_t font;
	uint16_t hyperlink;
};

/*
 * Legacy bright and blink attributes may select one of four 256-glyph fonts.
 * A disabled selector bit leaves that attribute available purely for styling.
 */
struct st_console_font_selection {
	uint16_t normal;
	uint16_t bright;
	uint16_t blink;
	uint16_t bright_blink;
	bool bright_selects_font;
	bool blink_selects_font;
};

/*
 * One-bit glyph rows are MSB-first.  The bitmap is borrowed and immutable;
 * it must remain valid until replacement, removal, or console destruction.
 */
struct st_console_font {
	const uint8_t *bitmap;
	size_t bitmap_size;
	uint32_t width;
	uint32_t height;
	uint32_t glyph_count;
	size_t row_stride;
	size_t glyph_stride;
	const uint8_t *wide_bitmap;
	size_t wide_bitmap_size;
	uint32_t wide_glyph_count;
	size_t wide_row_stride;
	size_t wide_glyph_stride;
	const struct st_console_font_mapping *mapping;
	size_t mapping_count;
	/* Replacement glyphs must belong to the matching bitmap bank. */
	uint32_t fallback_glyph;
	uint32_t wide_fallback_glyph;
};

#define ST_CONSOLE_FONT_NO_WIDE_FALLBACK UINT32_C(0)

enum st_console_cursor_shape {
	ST_CONSOLE_CURSOR_BLOCK,
	ST_CONSOLE_CURSOR_UNDERLINE,
	ST_CONSOLE_CURSOR_BAR,
};

/* Bitmap cursor scan lines are inclusive and expressed in range units. */
struct st_console_custom_cursor {
	uint32_t start;
	uint32_t end;
	uint32_t range;
	bool visible;
	bool blink;
};

enum st_console_blink_mode {
	ST_CONSOLE_BLINK_PC,
	ST_CONSOLE_BLINK_PRESTEL,
	ST_CONSOLE_BLINK_COMMODORE,
	ST_CONSOLE_BLINK_ATARI,
	ST_CONSOLE_BLINK_ATARI_ST,
};

enum st_console_scaling_mode {
	/* Scale the composed frame in software before handing it to the host. */
	ST_CONSOLE_SCALING_INTERNAL,
	/* Hand the native frame to the host and let it perform final scaling. */
	ST_CONSOLE_SCALING_EXTERNAL,
};

enum st_console_backend {
	ST_CONSOLE_BACKEND_AUTO,
	ST_CONSOLE_BACKEND_ANSI,
	ST_CONSOLE_BACKEND_NCURSES,
	ST_CONSOLE_BACKEND_WIN32,
	ST_CONSOLE_BACKEND_GDI,
	ST_CONSOLE_BACKEND_SDL,
	ST_CONSOLE_BACKEND_X11,
	ST_CONSOLE_BACKEND_WAYLAND,
	ST_CONSOLE_BACKEND_QUARTZ,
	ST_CONSOLE_BACKEND_LIBRETRO,
	ST_CONSOLE_BACKEND_HEADLESS,
};

enum st_console_build_feature {
	ST_CONSOLE_BUILD_ANSI = UINT32_C(1) << 0,
	ST_CONSOLE_BUILD_NCURSES = UINT32_C(1) << 1,
	ST_CONSOLE_BUILD_WIN32 = UINT32_C(1) << 2,
	ST_CONSOLE_BUILD_GDI = UINT32_C(1) << 3,
	ST_CONSOLE_BUILD_SDL = UINT32_C(1) << 4,
	ST_CONSOLE_BUILD_X11 = UINT32_C(1) << 5,
	ST_CONSOLE_BUILD_XRENDER = UINT32_C(1) << 6,
	ST_CONSOLE_BUILD_XRANDR = UINT32_C(1) << 7,
	ST_CONSOLE_BUILD_XINERAMA = UINT32_C(1) << 8,
	ST_CONSOLE_BUILD_WAYLAND = UINT32_C(1) << 9,
	ST_CONSOLE_BUILD_QUARTZ = UINT32_C(1) << 10,
	ST_CONSOLE_BUILD_LIBRETRO = UINT32_C(1) << 11,
};

/* Returns the active backend selected during console creation. */
int st_console_get_backend(st_console_t *console,
	enum st_console_backend *backend);
const char *st_console_backend_name(enum st_console_backend backend);
/* Returns facilities compiled into the console library, not runtime presence. */
uint32_t st_console_build_features(void);

typedef int (*st_console_ansi_write_t)(void *context, const void *data,
	size_t size);

struct st_console_ansi_io {
	st_console_ansi_write_t write;
	void *context;
	/* Zero selects the basic 16-colour ANSI palette; values above 256 fail. */
	uint16_t indexed_colours;
	/* Independent of the indexed palette size. */
	bool direct_colour;
};

enum st_console_text_presentation {
	ST_CONSOLE_TEXT_UNICODE,
	ST_CONSOLE_TEXT_CP437,
	ST_CONSOLE_TEXT_ASCII,
};

struct st_console_config {
	uint32_t columns;
	uint32_t rows;
	/* Both zero disables the software pixel planes. */
	uint32_t cell_width;
	uint32_t cell_height;
	/*
	 * Both zero derives the raster size from the cell grid.  Explicit raster
	 * dimensions may include scanlines or columns outside the text image.
	 */
	uint32_t pixel_width;
	uint32_t pixel_height;
	/* A zero range selects the console's generic cell-height default. */
	struct st_console_custom_cursor default_cursor;
	enum st_console_backend backend;
	/* Selects how a text-terminal backend represents cell characters. */
	enum st_console_text_presentation text_presentation;
	bool wrap;
	const struct st_console_ansi_io *ansi_io;
	enum st_console_blink_mode blink_mode;
	enum st_console_scaling_mode scaling_mode;
	/* Zero initialization selects SyncTERM's traditional blocky default. */
	enum st_pixel_scaling_policy scaling_policy;
	/* Zero in either member selects the native bitmap aspect ratio. */
	uint32_t aspect_width;
	uint32_t aspect_height;
	/* Zero selects one-times display scaling.  Values below one are clamped. */
	double initial_scaling;
	/* Both nonzero override initial_scaling, in drawable pixel units. */
	uint32_t initial_window_width;
	uint32_t initial_window_height;
};

enum st_console_event_type {
	ST_CONSOLE_EVENT_KEY_CODE,
	ST_CONSOLE_EVENT_PHYSICAL_KEY,
	ST_CONSOLE_EVENT_TEXT,
	/* DoorWay's DOS extended-key encoding received by ANSI backends. */
	ST_CONSOLE_EVENT_DOORWAY_KEY,
	ST_CONSOLE_EVENT_MOUSE,
	ST_CONSOLE_EVENT_RESIZE,
	ST_CONSOLE_EVENT_FOCUS,
	ST_CONSOLE_EVENT_CLOSE,
};

/* Normalized non-text keys; ASCII control keys retain their byte values. */
enum st_console_key_code {
	ST_CONSOLE_KEY_UP = 0x0100,
	ST_CONSOLE_KEY_DOWN,
	ST_CONSOLE_KEY_LEFT,
	ST_CONSOLE_KEY_RIGHT,
	ST_CONSOLE_KEY_INSERT,
	ST_CONSOLE_KEY_DELETE,
	ST_CONSOLE_KEY_HOME,
	ST_CONSOLE_KEY_END,
	ST_CONSOLE_KEY_PAGE_UP,
	ST_CONSOLE_KEY_PAGE_DOWN,
	ST_CONSOLE_KEY_F1,
	ST_CONSOLE_KEY_F2,
	ST_CONSOLE_KEY_F3,
	ST_CONSOLE_KEY_F4,
	ST_CONSOLE_KEY_F5,
	ST_CONSOLE_KEY_F6,
	ST_CONSOLE_KEY_F7,
	ST_CONSOLE_KEY_F8,
	ST_CONSOLE_KEY_F9,
	ST_CONSOLE_KEY_F10,
	ST_CONSOLE_KEY_F11,
	ST_CONSOLE_KEY_F12,
};

#define ST_CONSOLE_MODIFIER_SHIFT (UINT32_C(1) << 0)
#define ST_CONSOLE_MODIFIER_CONTROL (UINT32_C(1) << 1)
#define ST_CONSOLE_MODIFIER_ALT (UINT32_C(1) << 2)
#define ST_CONSOLE_MODIFIER_GUI (UINT32_C(1) << 3)
#define ST_CONSOLE_MODIFIER_CAPS_LOCK (UINT32_C(1) << 4)
#define ST_CONSOLE_MODIFIER_NUM_LOCK (UINT32_C(1) << 5)

#define ST_CONSOLE_MOUSE_BUTTON_LEFT (UINT32_C(1) << 0)
#define ST_CONSOLE_MOUSE_BUTTON_MIDDLE (UINT32_C(1) << 1)
#define ST_CONSOLE_MOUSE_BUTTON_RIGHT (UINT32_C(1) << 2)
#define ST_CONSOLE_MOUSE_BUTTON_X1 (UINT32_C(1) << 3)
#define ST_CONSOLE_MOUSE_BUTTON_X2 (UINT32_C(1) << 4)

enum st_console_mouse_action {
	ST_CONSOLE_MOUSE_MOVE,
	ST_CONSOLE_MOUSE_BUTTON,
	ST_CONSOLE_MOUSE_WHEEL,
};

struct st_console_event {
	enum st_console_event_type type;
	union {
		struct {
			uint16_t code;
			uint32_t modifiers;
		} key;
		struct {
			uint16_t evdev;
			uint32_t modifiers;
			bool pressed;
			bool repeat;
		} physical_key;
		struct {
			uint32_t codepoint;
			uint32_t modifiers;
		} text;
		struct {
			uint8_t scan_code;
			bool enhanced;
		} doorway_key;
		struct {
			int32_t x;
			int32_t y;
			int32_t wheel_x;
			int32_t wheel_y;
			uint32_t buttons;
			enum st_console_mouse_action action;
		} mouse;
		struct {
			uint32_t columns;
			uint32_t rows;
		} resize;
		struct {
			bool focused;
		} focus;
	} data;
};

#define ST_CONSOLE_CAP_LOADABLE_FONTS    (UINT32_C(1) << 0)
#define ST_CONSOLE_CAP_PALETTE_CHANGE    (UINT32_C(1) << 1)
#define ST_CONSOLE_CAP_PIXELS            (UINT32_C(1) << 2)
#define ST_CONSOLE_CAP_CUSTOM_CURSOR     (UINT32_C(1) << 3)
#define ST_CONSOLE_CAP_WINDOW_TITLE      (UINT32_C(1) << 4)
#define ST_CONSOLE_CAP_EXTENDED_PALETTE  (UINT32_C(1) << 5)
#define ST_CONSOLE_CAP_BLOCKY_SCALING    (UINT32_C(1) << 6)
#define ST_CONSOLE_CAP_EXTERNAL_SCALING  (UINT32_C(1) << 7)
#define ST_CONSOLE_CAP_TEXT_TERMINAL     (UINT32_C(1) << 8)
#define ST_CONSOLE_CAP_CLIPBOARD         (UINT32_C(1) << 9)
#define ST_CONSOLE_CAP_OPEN_URL          (UINT32_C(1) << 10)
#define ST_CONSOLE_CAP_LOCAL_CONTROL_KEYS (UINT32_C(1) << 11)

/* result is unchanged on failure. */
int st_console_create(st_console_t **result,
	const struct st_console_config *config);
/* The caller must prevent new operations and let active operations return. */
void st_console_destroy(st_console_t *console);

int st_console_size(st_console_t *console, uint32_t *columns,
	uint32_t *rows);
int st_console_cell_pixel_size(st_console_t *console, uint32_t *width,
	uint32_t *height);
int st_console_capabilities(st_console_t *console, uint32_t *capabilities);
int st_console_resize(st_console_t *console, uint32_t columns,
	uint32_t rows);

int st_console_write_cells(st_console_t *console,
	const struct st_console_rect *destination,
	const struct st_console_cell *cells, size_t stride);
/* Writes already-resolved font/glyph cells without Unicode lookup. */
int st_console_write_complete_cells(st_console_t *console,
	const struct st_console_rect *destination,
	const struct st_console_cell *cells, size_t stride);
int st_console_read_cells(st_console_t *console,
	const struct st_console_rect *source, struct st_console_cell *cells,
	size_t stride);
int st_console_fill(st_console_t *console,
	const struct st_console_rect *destination,
	const struct st_console_cell *cell);

/* Updates nest.  The outermost end makes the batch eligible for presentation. */
int st_console_begin_update(st_console_t *console);
int st_console_end_update(st_console_t *console);
/* Forces a synchronous presentation, including changes in an open batch. */
int st_console_present(st_console_t *console);

int st_console_set_style(st_console_t *console,
	const struct st_console_style *style);
int st_console_get_style(st_console_t *console,
	struct st_console_style *style);
int st_console_set_position(st_console_t *console, uint32_t x, uint32_t y);
int st_console_get_position(st_console_t *console, uint32_t *x,
	uint32_t *y);
int st_console_set_cursor(st_console_t *console, bool visible,
	enum st_console_cursor_shape shape);
int st_console_get_cursor(st_console_t *console, bool *visible,
	enum st_console_cursor_shape *shape);
int st_console_get_default_cursor(st_console_t *console,
	struct st_console_custom_cursor *cursor);
int st_console_set_custom_cursor(st_console_t *console,
	const struct st_console_custom_cursor *cursor);
int st_console_get_custom_cursor(st_console_t *console,
	struct st_console_custom_cursor *cursor);
int st_console_set_blink_mode(st_console_t *console,
	enum st_console_blink_mode mode);
int st_console_get_blink_mode(st_console_t *console,
	enum st_console_blink_mode *mode);
int st_console_set_scaling_mode(st_console_t *console,
	enum st_console_scaling_mode mode);
int st_console_get_scaling_mode(st_console_t *console,
	enum st_console_scaling_mode *mode);
int st_console_set_blocky_scaling(st_console_t *console, bool blocky);
int st_console_get_blocky_scaling(st_console_t *console, bool *blocky);
int st_console_set_aspect_ratio(st_console_t *console, uint32_t width,
	uint32_t height);
int st_console_get_aspect_ratio(st_console_t *console, uint32_t *width,
	uint32_t *height);
int st_console_set_window(st_console_t *console,
	const struct st_console_rect *window);
int st_console_get_window(st_console_t *console,
	struct st_console_rect *window);
int st_console_set_wrap(st_console_t *console, bool wrap);
int st_console_put_codepoint(st_console_t *console, uint32_t codepoint);
int st_console_write_utf8(st_console_t *console, const char *text,
	size_t length);
int st_console_clear_window(st_console_t *console, uint32_t codepoint);
int st_console_clear_line(st_console_t *console, uint32_t codepoint);
int st_console_move_cells(st_console_t *console,
	const struct st_console_rect *source, uint32_t destination_x,
	uint32_t destination_y);
int st_console_move_cells_clear(st_console_t *console,
	const struct st_console_rect *source, uint32_t destination_x,
	uint32_t destination_y, const struct st_console_cell *fill);
int st_console_scroll(st_console_t *console, int32_t lines,
	uint32_t codepoint);
int st_console_insert_lines(st_console_t *console, uint32_t count,
	uint32_t codepoint);
int st_console_delete_lines(st_console_t *console, uint32_t count,
	uint32_t codepoint);

int st_console_set_palette(st_console_t *console, uint16_t first,
	const uint32_t *rgb, uint16_t count);
int st_console_get_palette(st_console_t *console, uint16_t first,
	uint32_t *rgb, uint16_t count);
int st_console_register_static_font(st_console_t *console,
	const struct st_console_font *font, uint16_t *identifier);
int st_console_register_font_copy(st_console_t *console,
	const struct st_console_font *font, uint16_t *identifier);
int st_console_font_registered(st_console_t *console, uint16_t identifier,
	bool *registered);
uint16_t st_console_font_for_flags(
	const struct st_console_font_selection *selection, uint32_t cell_flags);
/*
 * Resolve legacy selector flags before passing the resulting identifier here.
 * glyph_font receives the registration that actually supplies the glyph.
 */
int st_console_font_lookup(st_console_t *console, uint16_t identifier,
	uint32_t codepoint, uint16_t *glyph_font, uint32_t *glyph,
	uint8_t *columns);
int st_console_font_glyph_columns(st_console_t *console,
	uint16_t identifier, uint32_t glyph, uint8_t *columns);

/*
 * Pixel coordinates are zero-based and independent of the text window.
 * Pixel colours and pixel-buffer planes are packed 0x00RRGGBB, never indexed.
 */
int st_console_pixel_size(st_console_t *console, uint32_t *width,
	uint32_t *height);
int st_console_set_pixel(st_console_t *console, uint32_t x, uint32_t y,
	uint32_t colour);
int st_console_get_pixels(struct st_pixel_buffer **result,
	st_console_t *console, const struct st_console_rect *source,
	bool force_text);
int st_console_blit_pixels(st_console_t *console,
	const struct st_pixel_buffer *source, const struct st_pixel_mask *mask,
	const struct st_pixel_blit *blit);

int st_console_snapshot_create(st_console_snapshot_t **result,
	st_console_t *console);
int st_console_snapshot_restore(st_console_t *console,
	const st_console_snapshot_t *snapshot);
void st_console_snapshot_destroy(st_console_snapshot_t *snapshot);

/*
 * Backends feed their complete input semantics into the ordered queue.
 * A physical press may be followed by either a normalized key code or the
 * text committed by the host input method, but not both for the same result.
 * One input-method commit may contain more than one text event.
 */
int st_console_post_event(st_console_t *console,
	const struct st_console_event *event);
int st_console_event_try(st_console_t *console,
	struct st_console_event *event);
int st_console_event_wait(st_console_t *console,
	struct st_console_event *event);
int st_console_event_timedwait(st_console_t *console,
	struct st_console_event *event, const struct timespec *deadline);
int st_console_events_clear(st_console_t *console);
int st_console_physical_key_state(st_console_t *console, uint16_t evdev,
	bool *pressed);

/* Desktop/host operations can be independently unsupported by a backend. */
int st_console_set_title(st_console_t *console, const char *title);
int st_console_set_clipboard_text(st_console_t *console, const char *text);
/* result is unchanged on failure; release it with st_console_free_text. */
int st_console_get_clipboard_text(st_console_t *console, char **result);
void st_console_free_text(char *text);
int st_console_open_url(st_console_t *console, const char *url);
int st_console_set_pointer_visible(st_console_t *console, bool visible);
int st_console_set_fullscreen(st_console_t *console, bool fullscreen);

/* Libretro's run callback pulls a stable completed frame from its backend. */
int st_console_libretro_frame_size(st_console_t *console, uint32_t *width,
	uint32_t *height);
int st_console_libretro_frame_copy(st_console_t *console,
	uint32_t *destination, size_t destination_stride);

#endif

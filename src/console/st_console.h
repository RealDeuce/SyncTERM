/* Character-generator and host-console abstraction. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_CONSOLE_H_
#define ST_CONSOLE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "st_controller_event.h"
#include "st_pixels.h"

typedef struct st_console          st_console_t;
typedef struct st_console_display  st_console_display_t;
typedef struct st_console_snapshot st_console_snapshot_t;

/* Indexed colours use values 0 through 65535. RGB colours have this tag. */
#define ST_CONSOLE_COLOR_RGB_TAG ST_PIXEL_COLOR_RGB_TAG
/* Resolved RGB written by a raster view keeps its local palette origin in
 * shared pixels. The renderer still reads only the low 24 color bits. */
#define ST_CONSOLE_COLOR_VIEW_RGB UINT32_C(0x40000000)
/* Resolved RIP3 graphics retain their origin when terminal text paints the
 * same RGB value in the shared raster. */
#define ST_CONSOLE_COLOR_RIP3_RGB                    UINT32_C(0x20000000)
#define ST_CONSOLE_COLOR_RGB_VALUE(red, green, blue) ST_PIXEL_COLOR_RGB_VALUE((red), (green), (blue))
#define ST_CONSOLE_COLOR_IS_RGB(colour)              ST_PIXEL_COLOR_IS_RGB(colour)

/*
 * Cell flags are integer masks rather than C bit-fields so their layout is
 * independent of the implementation.  DIRTY is maintained by the library.
 * SAA5050_CONTROL records the source control character separately from the
 * glyph in codepoint; the displayed glyph can therefore remain a font index.
 */
#define ST_CONSOLE_CELL_DIRTY                 (UINT32_C(1) << 0)
#define ST_CONSOLE_CELL_BLINK                 (UINT32_C(1) << 1)
#define ST_CONSOLE_CELL_CONCEAL               (UINT32_C(1) << 2)
#define ST_CONSOLE_CELL_PIXEL_GRAPHICS        (UINT32_C(1) << 3)
#define ST_CONSOLE_CELL_DOUBLE_HEIGHT         (UINT32_C(1) << 4)
#define ST_CONSOLE_CELL_SAA5050               (UINT32_C(1) << 5)
#define ST_CONSOLE_CELL_REVEAL                (UINT32_C(1) << 6)
#define ST_CONSOLE_CELL_SEPARATED             (UINT32_C(1) << 7)
#define ST_CONSOLE_CELL_TERMINAL_FONT         (UINT32_C(1) << 8)
#define ST_CONSOLE_CELL_SAA5050_CONTROL_SHIFT 9
#define ST_CONSOLE_CELL_SAA5050_CONTROL_MASK  (UINT32_C(0x7f) << ST_CONSOLE_CELL_SAA5050_CONTROL_SHIFT)
#define ST_CONSOLE_CELL_SAA5050_CONTROL(value) \
	(((uint32_t)(value) & UINT32_C(0x7f)) << ST_CONSOLE_CELL_SAA5050_CONTROL_SHIFT)
#define ST_CONSOLE_CELL_UNDERLINE    (UINT32_C(1) << 16)
#define ST_CONSOLE_CELL_BOLD         (UINT32_C(1) << 17)
#define ST_CONSOLE_CELL_REVERSE      (UINT32_C(1) << 18)
#define ST_CONSOLE_CELL_CONTINUATION (UINT32_C(1) << 19)
#define ST_CONSOLE_CELL_BRIGHT       (UINT32_C(1) << 22)
/* Colour was supplied by a legacy attribute and follows VideoFlags. */
#define ST_CONSOLE_CELL_LEGACY_FOREGROUND (UINT32_C(1) << 23)
#define ST_CONSOLE_CELL_LEGACY_BACKGROUND (UINT32_C(1) << 24)
#define ST_CONSOLE_CELL_PUBLIC_FLAGS                                                                           \
	(ST_CONSOLE_CELL_BLINK | ST_CONSOLE_CELL_CONCEAL | ST_CONSOLE_CELL_PIXEL_GRAPHICS                      \
	    | ST_CONSOLE_CELL_DOUBLE_HEIGHT | ST_CONSOLE_CELL_SAA5050 | ST_CONSOLE_CELL_REVEAL                 \
	    | ST_CONSOLE_CELL_SEPARATED | ST_CONSOLE_CELL_TERMINAL_FONT | ST_CONSOLE_CELL_SAA5050_CONTROL_MASK \
	    | ST_CONSOLE_CELL_UNDERLINE | ST_CONSOLE_CELL_BOLD | ST_CONSOLE_CELL_REVERSE                       \
	    | ST_CONSOLE_CELL_CONTINUATION | ST_CONSOLE_CELL_BRIGHT | ST_CONSOLE_CELL_LEGACY_FOREGROUND        \
	    | ST_CONSOLE_CELL_LEGACY_BACKGROUND)

/* The source attribute bits retain these independently composable meanings. */
#define ST_CONSOLE_VIDEO_ALT_CHARS            (UINT32_C(1) << 0)
#define ST_CONSOLE_VIDEO_NO_BRIGHT            (UINT32_C(1) << 1)
#define ST_CONSOLE_VIDEO_BRIGHT_BACKGROUND    (UINT32_C(1) << 2)
#define ST_CONSOLE_VIDEO_BLINK_ALT_CHARS      (UINT32_C(1) << 3)
#define ST_CONSOLE_VIDEO_NO_BLINK             (UINT32_C(1) << 4)
#define ST_CONSOLE_VIDEO_EXPAND               (UINT32_C(1) << 5)
#define ST_CONSOLE_VIDEO_LINE_GRAPHICS_EXPAND (UINT32_C(1) << 6)
#define ST_CONSOLE_VIDEO_FLAGS                                                                        \
	(ST_CONSOLE_VIDEO_ALT_CHARS | ST_CONSOLE_VIDEO_NO_BRIGHT | ST_CONSOLE_VIDEO_BRIGHT_BACKGROUND \
	    | ST_CONSOLE_VIDEO_BLINK_ALT_CHARS | ST_CONSOLE_VIDEO_NO_BLINK | ST_CONSOLE_VIDEO_EXPAND  \
	    | ST_CONSOLE_VIDEO_LINE_GRAPHICS_EXPAND)

/*
 * Retained cells contain resolved font glyphs, never Unicode codepoints.
 * Resolve semantic text with st_console_font_lookup_cluster() before
 * constructing a cell.  font is the selected font and glyph_font is the font
 * which actually supplies glyph after fallback.
 */
struct st_console_cell {
	uint32_t glyph;
	uint32_t foreground;
	uint32_t background;
	uint32_t flags;
	uint16_t font;
	uint16_t glyph_font;
	uint16_t hyperlink;
	/*
	 * Reserved bytes make the serialized cell rectangle deterministic.
	 * Callers must initialize this to zero; the console also clears it when
	 * accepting cells so stack padding or future fields cannot affect a raw
	 * rectangle checksum.
	 */
	uint16_t reserved;
};

struct st_console_font_mapping {
	uint32_t codepoint;
	uint32_t glyph;
};

struct st_console_font_cluster_mapping {
	const char *text;
	size_t      text_size;
	uint32_t    glyph;
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
	bool     bright_selects_font;
	bool     blink_selects_font;
};

/* Optional intrinsic artwork: sorted unique glyph IDs, top-down straight RGBA.
 * A zero row_stride instead selects packed ARGB RLL: nonzero alpha followed by
 * R, G, B is one pixel; zero followed by n skips n + 1 transparent pixels.
 * A little-endian 16-bit byte offset from rgba to the right half precedes two
 * independent row-major half-width streams. The left half has width / 2
 * columns; the right has the remainder. Runs may cross rows within a half,
 * but never the half boundary.
 * size is the encoded byte count in this case; rgba points to the stream.
 * Height follows the monochrome bank; width may differ. Pixels are immutable and
 * borrowed, except when registered with st_console_register_font_copy(). */
struct st_console_color_glyph {
	uint32_t       glyph;
	const uint8_t *rgba;
	size_t         size;
	size_t         row_stride;
	/* Zero uses the monochrome bank width; otherwise scale this source width. */
	uint32_t width;
	/* Optional independent artwork for pixels 6/5 as tall as wide. Uses the
	 * same dimensions and encoding; absent artwork keeps the square version. */
	const uint8_t *tall_rgba;
	size_t         tall_size;
};

/*
 * One-bit glyph rows are MSB-first.  The bitmap is borrowed and immutable;
 * it must remain valid until replacement, removal, or console destruction.
 */
struct st_console_font {
	const uint8_t                                *bitmap;
	size_t                                        bitmap_size;
	uint32_t                                      width;
	uint32_t                                      height;
	uint32_t                                      glyph_count;
	size_t                                        row_stride;
	size_t                                        glyph_stride;
	const uint8_t                                *wide_bitmap;
	size_t                                        wide_bitmap_size;
	uint32_t                                      wide_glyph_count;
	size_t                                        wide_row_stride;
	size_t                                        wide_glyph_stride;
	const struct st_console_font_mapping         *mapping;
	size_t                                        mapping_count;
	const struct st_console_font_cluster_mapping *cluster_mapping;
	size_t                                        cluster_mapping_count;
	const struct st_console_color_glyph          *color_glyphs;
	size_t                                        color_glyph_count;
	/* Replacement glyphs must belong to the matching bitmap bank. */
	uint32_t fallback_glyph;
	uint32_t wide_fallback_glyph;
	uint32_t decode_error_glyph;
	/* Optional process-lifetime identity shared by sizes of the same font.
	 * The bitmap renderer selects a registered variant matching the cell
	 * geometry without changing retained cells' font/glyph identities.
	 * This many initial narrow glyph indexes have identical meanings in
	 * every variant; other glyphs are translated through their mappings. */
	const void *raster_family;
	uint32_t    raster_common_glyphs;
};

#define ST_CONSOLE_FONT_NO_WIDE_FALLBACK UINT32_C(0)

enum st_console_cursor_shape {
	ST_CONSOLE_CURSOR_BLOCK,
	ST_CONSOLE_CURSOR_UNDERLINE,
	ST_CONSOLE_CURSOR_BAR,
};

enum st_console_pointer_shape {
	ST_CONSOLE_POINTER_DEFAULT,
	ST_CONSOLE_POINTER_TEXT,
	ST_CONSOLE_POINTER_LINK,
	ST_CONSOLE_POINTER_WATCH,
	ST_CONSOLE_POINTER_CROSSHAIR,
	ST_CONSOLE_POINTER_STOP,
	ST_CONSOLE_POINTER_WAIT,
	ST_CONSOLE_POINTER_PROHIBITED,
	ST_CONSOLE_POINTER_COUNT,
};

/* Bitmap cursor scan lines are inclusive and expressed in range units. */
struct st_console_custom_cursor {
	uint32_t start;
	uint32_t end;
	uint32_t range;
	bool     visible;
	bool     blink;
	/* With a nonzero mask, complement indices within this palette bank. */
	uint16_t index_base;
	uint16_t index_xor;
};

/* A separately composed bitmap cursor. It never changes cells or raster
 * capture. Standard cursor setters disable it; snapshots retain it. */
struct st_console_pixel_cursor {
	struct st_console_rect rectangle;
	uint32_t               colour;
	bool                   enabled;
	bool                   visible;
};

/* A 16 by 16 pointer with per-pixel masks on the first 256 palette entries. */
struct st_console_indexed_pointer {
	uint8_t and_mask[16][16], xor_mask[16][16];
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
	ST_CONSOLE_BACKEND_TEST,
};

enum st_console_build_feature {
	ST_CONSOLE_BUILD_ANSI     = UINT32_C(1) << 0,
	ST_CONSOLE_BUILD_NCURSES  = UINT32_C(1) << 1,
	ST_CONSOLE_BUILD_WIN32    = UINT32_C(1) << 2,
	ST_CONSOLE_BUILD_GDI      = UINT32_C(1) << 3,
	ST_CONSOLE_BUILD_SDL      = UINT32_C(1) << 4,
	ST_CONSOLE_BUILD_X11      = UINT32_C(1) << 5,
	ST_CONSOLE_BUILD_XRENDER  = UINT32_C(1) << 6,
	ST_CONSOLE_BUILD_XRANDR   = UINT32_C(1) << 7,
	ST_CONSOLE_BUILD_XINERAMA = UINT32_C(1) << 8,
	ST_CONSOLE_BUILD_WAYLAND  = UINT32_C(1) << 9,
	ST_CONSOLE_BUILD_QUARTZ   = UINT32_C(1) << 10,
	ST_CONSOLE_BUILD_LIBRETRO = UINT32_C(1) << 11,
	ST_CONSOLE_BUILD_TEST     = UINT32_C(1) << 12,
};

/* Returns the active backend selected during console creation. */
int         st_console_get_backend(st_console_t *console, enum st_console_backend *backend);
int         st_console_display_get_backend(st_console_display_t *display, enum st_console_backend *backend);
const char *st_console_backend_name(enum st_console_backend backend);
/* Returns facilities compiled into the console library, not runtime presence. */
uint32_t st_console_build_features(void);

typedef int (*st_console_ansi_write_t)(void *context, const void *data, size_t size);

struct st_console_ansi_io {
	st_console_ansi_write_t write;
	void                   *context;
	/* Zero selects the basic 16-colour ANSI palette; values above 256 fail. */
	uint16_t indexed_colours;
	/* Independent of the indexed palette size. */
	bool direct_colour;
};

/* Rows are top-to-bottom, with four bytes per pixel in RGBA order. */
struct st_console_icon {
	const unsigned char *pixels;
	size_t               stride;
	uint32_t             width;
	uint32_t             height;
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
	bool                            wrap;
	enum st_console_blink_mode      blink_mode;
	uint32_t                        video_flags;
	enum st_console_scaling_mode    scaling_mode;
	/* Zero initialization selects SyncTERM's traditional blocky default. */
	enum st_pixel_scaling_policy scaling_policy;
	bool                         color_emoji;
	/* Zero in either member selects the native bitmap aspect ratio. */
	uint32_t aspect_width;
	uint32_t aspect_height;
	/* Keep the physical window on mode changes and fit the complete raster,
	 * permitting scale below one. Zero retains normal nearest-scale sizing. */
	bool preserve_window_size;
};

/* Physical output settings shared by every console on one display. */
struct st_console_display_config {
	enum st_console_backend backend;
	/* Selects how a text-terminal backend represents cell characters. */
	enum st_console_text_presentation text_presentation;
	/* Copied at display creation; callback and context must outlive the display. */
	const struct st_console_ansi_io *ansi_io;
	/* Borrowed for the display lifetime; ignored by non-window backends. */
	const struct st_console_icon *icon;
	/* Zero selects one-times display scaling.  Values below one are clamped. */
	double initial_scaling;
	/* Both nonzero override initial_scaling, in drawable pixel units. */
	uint32_t initial_window_width;
	uint32_t initial_window_height;
	/* Text terminals centre a smaller logical console within the host grid.
	 * Left justification affects the horizontal position only. */
	bool text_left_justify;
	/* Cell colour used for the surrounding area. */
	uint32_t text_margin_colour;
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
	/* Host window closure or permanent terminal-input disconnection. */
	ST_CONSOLE_EVENT_CLOSE,
	/* Direct numeric entry, resolved by the consumer's current encoding. */
	ST_CONSOLE_EVENT_CODEPAGE_BYTE,
	ST_CONSOLE_EVENT_CONTROLLER,
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

#define ST_CONSOLE_MODIFIER_SHIFT     (UINT32_C(1) << 0)
#define ST_CONSOLE_MODIFIER_CONTROL   (UINT32_C(1) << 1)
#define ST_CONSOLE_MODIFIER_ALT       (UINT32_C(1) << 2)
#define ST_CONSOLE_MODIFIER_GUI       (UINT32_C(1) << 3)
#define ST_CONSOLE_MODIFIER_CAPS_LOCK (UINT32_C(1) << 4)
#define ST_CONSOLE_MODIFIER_NUM_LOCK  (UINT32_C(1) << 5)

#define ST_CONSOLE_MOUSE_BUTTON_LEFT   (UINT32_C(1) << 0)
#define ST_CONSOLE_MOUSE_BUTTON_MIDDLE (UINT32_C(1) << 1)
#define ST_CONSOLE_MOUSE_BUTTON_RIGHT  (UINT32_C(1) << 2)
#define ST_CONSOLE_MOUSE_BUTTON_X1     (UINT32_C(1) << 3)
#define ST_CONSOLE_MOUSE_BUTTON_X2     (UINT32_C(1) << 4)

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
			bool     pressed;
			bool     repeat;
		} physical_key;

		struct {
			uint32_t codepoint;
			uint32_t modifiers;
		} text;

		struct st_controller_event controller;
		uint8_t codepage_byte;

		struct {
			uint8_t scan_code;
			bool    enhanced;
		} doorway_key;

		struct {
			/* Cell coordinates, zero-based. */
			int32_t x;
			int32_t y;
			/* Internal-raster coordinates, or -1 when unavailable. */
			int32_t                      pixel_x;
			int32_t                      pixel_y;
			int32_t                      wheel_x;
			int32_t                      wheel_y;
			uint32_t                     buttons;
			uint32_t                     modifiers;
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

#define ST_CONSOLE_CAP_LOADABLE_FONTS     (UINT32_C(1) << 0)
#define ST_CONSOLE_CAP_PALETTE_CHANGE     (UINT32_C(1) << 1)
#define ST_CONSOLE_CAP_PIXELS             (UINT32_C(1) << 2)
#define ST_CONSOLE_CAP_CUSTOM_CURSOR      (UINT32_C(1) << 3)
#define ST_CONSOLE_CAP_WINDOW_TITLE       (UINT32_C(1) << 4)
#define ST_CONSOLE_CAP_EXTENDED_PALETTE   (UINT32_C(1) << 5)
#define ST_CONSOLE_CAP_BLOCKY_SCALING     (UINT32_C(1) << 6)
#define ST_CONSOLE_CAP_EXTERNAL_SCALING   (UINT32_C(1) << 7)
#define ST_CONSOLE_CAP_TEXT_TERMINAL      (UINT32_C(1) << 8)
#define ST_CONSOLE_CAP_CLIPBOARD          (UINT32_C(1) << 9)
#define ST_CONSOLE_CAP_OPEN_URL           (UINT32_C(1) << 10)
#define ST_CONSOLE_CAP_LOCAL_CONTROL_KEYS (UINT32_C(1) << 11)
#define ST_CONSOLE_CAP_BRIGHT_BACKGROUND  (UINT32_C(1) << 12)

/* Single-console convenience constructor; owns its display.  result is
 * unchanged on failure.  Use an explicit display for multiple consoles.
 */
int st_console_create(st_console_t **result, const struct st_console_display_config *display_config,
    const struct st_console_config *config);
/*
 * Explicit multi-console lifecycle.  The display must outlive every console
 * created on it.  Its backend is opened when the first console is created.
 */
int  st_console_display_create(st_console_display_t **result, const struct st_console_display_config *config);
void st_console_display_destroy(st_console_display_t *display);
int  st_console_create_on_display(st_console_t **result, st_console_display_t *display,
    const struct st_console_config *config);
/* Activation includes full presentation.  Failure restores the previous
 * console's backend state.  If restoration also fails, backend operations
 * return IO_ERROR_IO until a later activation succeeds.  Input continues to
 * target the previous selection until a switch succeeds.
 */
int st_console_display_activate(st_console_display_t *display, st_console_t *console);
/* Returns a borrowed selection, or NULL for an empty display.  The caller
 * must coordinate destruction; this query does not extend console lifetime.
 */
int st_console_display_active(st_console_display_t *display, st_console_t **console);
/* The caller must prevent new operations on this console and let its active
 * calls return.  The library drains its own presentation and input references.
 * Removing the selected console selects a remaining peer; if that fails, the
 * peer remains selected with backend operations suspended until activation.
 * Display destruction requires all callers using that display to have stopped.
 * Invalidates the caller's handle immediately.  If a failed backend cannot
 * detach, storage is retained internally until recovery or backend shutdown.
 */
void st_console_destroy(st_console_t *console);

int st_console_get_config(st_console_t *console, struct st_console_config *config);
int st_console_size(st_console_t *console, uint32_t *columns, uint32_t *rows);
int st_console_cell_pixel_size(st_console_t *console, uint32_t *width, uint32_t *height);
int st_console_capabilities(st_console_t *console, uint32_t *capabilities);
int st_console_resize(st_console_t *console, uint32_t columns, uint32_t rows);
/*
 * Replace the text grid and cell dimensions without changing the pixel
 * surface, palette, or registered fonts. Pending text is rasterized first;
 * the new cells are blank and existing pixels remain visible until overwritten.
 * Requires an existing pixel surface and a grid that fits inside it.
 */
int st_console_regrid_preserve_pixels(st_console_t *console, uint32_t columns, uint32_t rows, uint32_t cell_width,
    uint32_t cell_height);
/*
 * Replaces the complete logical/raster configuration and clears the console.
 * All storage is prepared before the active backend is resized; failure leaves
 * the existing console state intact.
 */
int st_console_reconfigure(st_console_t *console, const struct st_console_config *config);
/* A positioned text grid over an existing console's raster. Owns cells/font
 * registrations, but no pixel planes. Parent must outlive it; parent raster
 * replacement is busy while views exist. Initial text setup is hidden until
 * adopt, which retains the visible pixels and enables native rendering. */
int st_console_create_raster_view(st_console_t **result, st_console_t *parent, uint32_t columns, uint32_t rows,
    uint32_t cell_width, uint32_t cell_height, uint64_t x, uint64_t y);
int st_console_raster_view_adopt(st_console_t *console);
/* The view and graphics page use the same palette. Preserve native pixel
 * indices so palette edits recolour text already painted on the page. */
int st_console_raster_view_adopt_indexed(st_console_t *console);
/* For a modal view on a paused parent. Report pixel damage while preserving
 * the parent's cell ownership. Restore the two
 * planes from the capture of the view rectangle before resuming the parent. */
int st_console_raster_view_adopt_transient(st_console_t *console);
int st_console_raster_view_restore_transient(st_console_t *console, const struct st_pixel_buffer *source);

/* Writes resolved cells.  Wide glyphs require matching continuation cells. */
int st_console_write_cells(st_console_t *console, const struct st_console_rect *destination,
    const struct st_console_cell *cells, size_t stride);
/*
 * Bulk-copies a complete rectangle of resolved cells without per-cell pair
 * synthesis.  Wide glyphs require matching continuation cells.
 */
int st_console_write_complete_cells(st_console_t *console, const struct st_console_rect *destination,
    const struct st_console_cell *cells, size_t stride);
/* Returns resolved cell state; source Unicode text is not retained. */
int st_console_read_cells(st_console_t *console, const struct st_console_rect *source,
    struct st_console_cell *cells, size_t stride);
/* The resolved fill glyph must occupy one column. */
int st_console_fill(st_console_t *console, const struct st_console_rect *destination,
    const struct st_console_cell *cell);

/* Updates nest.  The outermost end makes the batch eligible for presentation. */
int st_console_begin_update(st_console_t *console);
int st_console_end_update(st_console_t *console);
/* Forces a synchronous presentation, including changes in an open batch. */
int st_console_present(st_console_t *console);

int st_console_set_style(st_console_t *console, const struct st_console_style *style);
int st_console_get_style(st_console_t *console, struct st_console_style *style);
int st_console_set_position(st_console_t *console, uint32_t x, uint32_t y);
int st_console_get_position(st_console_t *console, uint32_t *x, uint32_t *y);
int st_console_set_cursor(st_console_t *console, bool visible, enum st_console_cursor_shape shape);
int st_console_get_cursor(st_console_t *console, bool *visible, enum st_console_cursor_shape *shape);
int st_console_get_default_cursor(st_console_t *console, struct st_console_custom_cursor *cursor);
/* Replaces the default cursor and immediately selects it. */
int st_console_set_default_cursor(st_console_t *console, const struct st_console_custom_cursor *cursor);
int st_console_set_custom_cursor(st_console_t *console, const struct st_console_custom_cursor *cursor);
int st_console_get_custom_cursor(st_console_t *console, struct st_console_custom_cursor *cursor);
int st_console_set_pixel_cursor(st_console_t *console, const struct st_console_pixel_cursor *cursor);
int st_console_get_pixel_cursor(st_console_t *console, struct st_console_pixel_cursor *cursor);
int st_console_set_blink_mode(st_console_t *console, enum st_console_blink_mode mode);
int st_console_get_blink_mode(st_console_t *console, enum st_console_blink_mode *mode);
int st_console_set_video_flags(st_console_t *console, uint32_t flags);
int st_console_get_video_flags(st_console_t *console, uint32_t *flags);
int st_console_set_scaling_mode(st_console_t *console, enum st_console_scaling_mode mode);
int st_console_get_scaling_mode(st_console_t *console, enum st_console_scaling_mode *mode);
/* Changes intrinsic glyph rendering immediately, preserving layout and graphics.
 * Snapshots restore text under the current preference rather than reverting it. */
int st_console_set_color_emoji(st_console_t *console, bool enabled);
int st_console_get_color_emoji(st_console_t *console, bool *enabled);
int st_console_set_blocky_scaling(st_console_t *console, bool blocky);
int st_console_get_blocky_scaling(st_console_t *console, bool *blocky);
int st_console_set_aspect_ratio(st_console_t *console, uint32_t width, uint32_t height);
int st_console_get_aspect_ratio(st_console_t *console, uint32_t *width, uint32_t *height);
int st_console_set_window(st_console_t *console, const struct st_console_rect *window);
int st_console_get_window(st_console_t *console, struct st_console_rect *window);
int st_console_set_wrap(st_console_t *console, bool wrap);
/* Semantic codepoint operations reject values which are not Unicode scalars. */
int st_console_put_codepoint(st_console_t *console, uint32_t codepoint);
int st_console_put_decode_error(st_console_t *console);
int st_console_write_utf8(st_console_t *console, const char *text, size_t length);
/* Fill codepoints for clear, scroll and line edits must occupy one column. */
int st_console_clear_window(st_console_t *console, uint32_t codepoint);
int st_console_clear_line(st_console_t *console, uint32_t codepoint);
int st_console_move_cells(st_console_t *console, const struct st_console_rect *source, uint32_t destination_x,
    uint32_t destination_y);
/* The optional clearing cell must contain one resolved narrow glyph. */
int st_console_move_cells_clear(st_console_t *console, const struct st_console_rect *source,
    uint32_t destination_x, uint32_t destination_y, const struct st_console_cell *fill);
/* Include non-text pixel rows in full-width row scrolling.  Switching modes
 * preserves visible pixels by normalizing the current ring offset.  Requires
 * a bitmap console; a text-only console returns IO_ERROR_NOT_SUPPORTED. */
int st_console_set_full_surface_scroll(st_console_t *console, bool enabled);
int st_console_scroll(st_console_t *console, int32_t lines, uint32_t codepoint);
int st_console_insert_lines(st_console_t *console, uint32_t count, uint32_t codepoint);
int st_console_delete_lines(st_console_t *console, uint32_t count, uint32_t codepoint);

/* Palette entries are packed 0x00RRGGBB.  count may be 65,536 when first is
 * zero; otherwise the range must end at or before entry 65,535.
 */
int st_console_set_palette(st_console_t *console, uint16_t first, const uint32_t *rgb, uint32_t count);
int st_console_get_palette(st_console_t *console, uint16_t first, uint32_t *rgb, uint32_t count);
/* Returns canonical startup values without consulting mutable console state. */
int st_console_get_default_palette(uint16_t first, uint32_t *rgb, uint32_t count);
/* Registration preserves retained cell content. A matching raster-family
 * variant also invalidates retained text for the bitmap renderer. */
int st_console_register_static_font(st_console_t *console, const struct st_console_font *font,
    uint16_t *identifier);
int st_console_register_font_copy(st_console_t *console, const struct st_console_font *font, uint16_t *identifier);
/* Use a copied strike to present an existing identifier and its retained text.
 * Passing NULL restores the registered strike. Logical glyphs stay intact. */
int st_console_set_font_strike(st_console_t *, uint16_t identifier, const struct st_console_font *strike);
/* Copy one registered font into another console with independent ownership.
 * No simultaneous console locks; source replacement/destruction after return
 * cannot invalidate the destination. Failure preserves identifier. */
int st_console_copy_font(st_console_t *destination, st_console_t *source, uint16_t identifier, uint16_t *result);
/* Explicit definition backups may outlive the source console. This owned
 * immutable font copy contains glyph/mapping resources only, no cells or screen pixels,
 * registry identity or console state. Registering returns a target-local ID.
 * Failures preserve caller outputs; each successful capture is destroyed once. */
struct st_console_font_snapshot;
int  st_console_font_snapshot_create(struct st_console_font_snapshot **result, st_console_t *console,
    uint16_t identifier);
int  st_console_font_snapshot_register(st_console_t *console, const struct st_console_font_snapshot *snapshot,
    uint16_t *identifier);
void st_console_font_snapshot_destroy(struct st_console_font_snapshot *snapshot);
int  st_console_font_registered(st_console_t *console, uint16_t identifier, bool *registered);
/* Designate one registered fallback per pixel size. Later lookups search
 * it after the selected font. Without one, compatible font zero is used. */
int st_console_set_fallback_font(st_console_t *console, uint16_t identifier);
/* Query an explicitly designated fallback; output unchanged on failure. */
int st_console_get_fallback_font(st_console_t *console, uint32_t width, uint32_t height, uint16_t *identifier);
uint16_t st_console_font_for_flags(const struct st_console_font_selection *selection, uint32_t cell_flags);
/*
 * Resolve legacy selector flags before passing the resulting identifier here.
 * glyph_font receives the registration that actually supplies the glyph.
 */
int st_console_font_lookup(st_console_t *console, uint16_t identifier, uint32_t codepoint, uint16_t *glyph_font,
    uint32_t *glyph, uint8_t *columns);
/* Resolve one UTF-8 grapheme cluster before constructing retained cells. */
int st_console_font_lookup_cluster(st_console_t *console, uint16_t identifier, const char *text, size_t text_size,
    uint8_t columns, uint16_t *glyph_font, uint32_t *glyph);
/* As above, with optional missing=true only when .noglyph was selected,
 * after searching both the requested font and its size-matched fallback.
 * A mapped glyph whose index equals the fallback is not missing. */
int st_console_font_lookup_cluster_ex(st_console_t *console, uint16_t identifier, const char *text,
    size_t text_size, uint8_t columns, uint16_t *glyph_font, uint32_t *glyph, bool *missing);
/* Select tofu without mapping lookup. columns must be 1 or 2. Uses a
 * size-matched fallback, otherwise the selected font. Outputs unchanged on
 * failure (INVALID_ARGUMENT, IO, or NOT_FOUND); returns zero on success.
 */
int st_console_font_lookup_fallback(st_console_t *console, uint16_t identifier, uint8_t columns,
    uint16_t *glyph_font, uint32_t *glyph);
/* Resolve font zero's narrow invalid-input glyph without moving the cursor.
 * Output is unchanged on failure. */
int st_console_font_lookup_decode_error(st_console_t *console, uint32_t *glyph);
/*
 * Returns a representative mapping, not the text which originally selected
 * the glyph.
 */
int st_console_font_glyph_text(st_console_t *console, uint16_t identifier, uint32_t glyph, char *text,
    size_t text_size, size_t *required);
int st_console_font_glyph_columns(st_console_t *console, uint16_t identifier, uint32_t glyph, uint8_t *columns);

/*
 * Pixel coordinates are zero-based and independent of the text window.
 * Pixel colours use the same indexed/direct representation as cell colours.
 */
int st_console_pixel_size(st_console_t *console, uint32_t *width, uint32_t *height);
/* Display-only status cells on the last grid row of a bitmap console.
 * NULL with a count of zero removes the buffered row and hides it.
 * Screen reads, snapshots and scrolling use the underlying cells and pixels. */
int st_console_write_status_row(st_console_t *console, const struct st_console_cell *cells, size_t count);
int st_console_show_status_row(st_console_t *console, bool visible);
int st_console_set_pixel(st_console_t *console, uint32_t x, uint32_t y, uint32_t colour);
/*
 * Edit the native indexed/RGB raster under the console write lock. Pending
 * text and circular row offsets are resolved before edit is called. The
 * borrowed buffer must not be retained, resized or freed, and edit must not
 * call console APIs. Set changed to the bounding rectangle of all writes
 * (initially empty), including writes made before returning an error.
 * Static graphics must write both planes; this API does not synthesize an
 * alternate plane from primary writes or track per-plane modifications.
 * Changes are published even when edit fails; there is no rollback.
 */
/* Invert the resolved RGB of both native planes, clipped to the raster. Uses
 * existing cell damage and row mapping; no snapshot or separate pixel plane. */
int st_console_invert_pixels(st_console_t *console, const struct st_console_rect *area);
/* Restore an independent native pixel-region capture without flattening the
 * console's circular raster. Both planes and palette indexes are retained. */
int st_console_restore_pixel_region(st_console_t *console, const struct st_pixel_buffer *source, uint32_t x,
    uint32_t y);
/* An optional mask picks source pixels to restore and limits graphics damage
 * to the cells those pixels touch. */
int st_console_restore_masked_pixel_region(st_console_t *console, const struct st_pixel_buffer *source,
    const struct st_pixel_mask *mask, uint32_t x, uint32_t y);
int st_console_modify_pixels(st_console_t *console,
    int   (*edit)(void *context, const struct st_pixel_buffer *pixels, struct st_console_rect *changed),
    void *context);
/* Same edit scope, with the borrowed native palette for indexed pixels. */
int st_console_modify_pixels_with_palette(st_console_t *console,
    int   (*edit)(void *context, const struct st_pixel_buffer *pixels, const uint32_t *palette,
	struct st_console_rect *changed),
    void *context);
int st_console_get_pixels(struct st_pixel_buffer **result, st_console_t *console,
    const struct st_console_rect *source, bool force_text);
int st_console_blit_pixels(st_console_t *console, const struct st_pixel_buffer *source,
    const struct st_pixel_mask *mask, const struct st_pixel_blit *blit);

int st_console_snapshot_create(st_console_snapshot_t **result, st_console_t *console);
/* restored, when non-NULL, reports application of saved state even when the
 * subsequent presentation fails. Rejected restores leave it false. */
int  st_console_snapshot_restore(st_console_t *console, const st_console_snapshot_t *snapshot, bool *restored);
void st_console_snapshot_destroy(st_console_snapshot_t *snapshot);

/*
 * Backends feed their complete input semantics into the ordered queue.
 * A physical press may be followed by either a normalized key code or the
 * text committed by the host input method, but not both for the same result.
 * One input-method commit may contain more than one text event.
 */
/* Target this console's queue, even when inactive. */
int st_console_post_event(st_console_t *console, const struct st_console_event *event);
/* Host input targets whichever console is active when delivery is locked.
 * Physical-key state belongs to the display with either posting entry point.
 */
int st_console_display_post_event(st_console_display_t *display, const struct st_console_event *event);
/* Whether polling the event queue has pending work for its owner. */
bool st_console_events_pending(st_console_t *console);
struct st_event;
/* Single consumer. Wait for either queued console input or a producer
 * signalling activity. Input stays in FIFO order for normal dispatch. */
int st_console_wait_activity(st_console_t *console, struct st_event *activity, const struct timespec *deadline);
int st_console_event_try(st_console_t *console, struct st_console_event *event);
int st_console_event_wait(st_console_t *console, struct st_console_event *event);
int st_console_event_timedwait(st_console_t *console, struct st_console_event *event,
    const struct timespec *deadline);
int st_console_events_clear(st_console_t *console);
int st_console_physical_key_state(st_console_t *console, uint16_t evdev, bool *pressed);
/* Take one display-input snapshot for all requested physical keys. */
int st_console_physical_key_states(st_console_t *console, const uint16_t *evdev, bool *pressed, size_t count);
/* Queue a synthetic edge only when it changes the display's held-key state. */
int st_console_synthesize_physical_key(st_console_t *console, uint16_t evdev, bool pressed);
int st_console_display_physical_key_state(st_console_display_t *display, uint16_t evdev, bool *pressed);

/* Desktop/host operations can be independently unsupported by a backend. */
int st_console_set_title(st_console_t *console, const char *title);
int st_console_set_clipboard_text(st_console_t *console, const char *text);
/* result is unchanged on failure; release it with st_console_free_text. */
int  st_console_get_clipboard_text(st_console_t *console, char **result);
void st_console_free_text(char *text);
int  st_console_open_url(st_console_t *console, const char *url);
/* Reports mouse-equivalent input for a bitmap console, using a native presence
 * signal where one is available and frontend input capability otherwise. */
int st_console_mouse_present(st_console_t *console, bool *present);
/* Set the pointer's top-left raster coordinate. NULL removes the image.
 * Composition is presentation-only and independent of host pointer visibility. */
int st_console_set_indexed_pointer(st_console_t *, int32_t x, int32_t y, const struct st_console_indexed_pointer *);
uint8_t st_console_palette_index256(const uint32_t *palette, uint32_t pixel);
int st_console_set_pointer_visible(st_console_t *console, bool visible);
int st_console_get_pointer_visible(st_console_t *console, bool *visible);
int st_console_set_pointer_shape(st_console_t *console, enum st_console_pointer_shape shape);
int st_console_set_fullscreen(st_console_t *console, bool fullscreen);
int st_console_display_set_clipboard_text(st_console_display_t *display, const char *text);
int st_console_display_get_clipboard_text(st_console_display_t *display, char **result);
int st_console_display_open_url(st_console_display_t *display, const char *url);
int st_console_display_set_fullscreen(st_console_display_t *display, bool fullscreen);
/* Returns the physical window scale; result is unchanged on failure. */
int st_console_display_get_scaling(st_console_display_t *display, double *result);

/* Libretro pulls the display's last completed frame, not a logical console.
 * Size is advisory: a switch may change it before copy.  Copy validates both
 * destination dimensions and leaves the destination untouched if too small.
 */
int st_console_display_libretro_frame_size(st_console_display_t *display, uint32_t *width, uint32_t *height);
int st_console_display_libretro_frame_copy(st_console_display_t *display, uint32_t *destination,
    size_t destination_stride, size_t destination_rows);
/* Convert coordinates in the retained frontend frame to console mouse cells
 * and internal raster positions without borrowing an active console. */
int st_console_display_libretro_map_pointer(st_console_display_t *display, int32_t x, int32_t y,
    struct st_console_event *event);

#endif

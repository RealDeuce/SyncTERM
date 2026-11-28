/* Incremental terminal-emulation parser and semantic console renderer. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cterm.h"
#include "cterm_apc.h"
#include "cterm_dec.h"
#include "cterm_internal.h"
#include "cterm_key.h"
#include "cterm_vt52.h"
#include "filesystem.h"
#include "ini.h"
#include "io_error.h"
#include "st_builtin_fonts.h"
#include "st_cterm.h"
#include "st_session_history.h"
#include "st_threads.h"
#include "st_time.h"
#include "st_unicode_text.h"
#include "st_utf8.h"
#include "string_compat.h"

static int write_plain(st_cterm_t *terminal, const void *bytes, size_t size, size_t *consumed, bool semantic_utf8);
static int filter_input_scalar(st_cterm_t *terminal, uint32_t scalar, bool *consumed, char *replacement,
    size_t *replacement_size);
static int present_filtered_scalar(void *context, uint32_t scalar, bool *consumed);
static int capture_scalar(st_cterm_t *terminal, uint32_t scalar);
static void status_display_changed(struct cterminal *terminal, int old_type, int new_type, void *context);
static void input_cleanup(st_cterm_t *owner);

static void
emit_response(const char *bytes, size_t size, void *context)
{
	st_cterm_t *terminal = context;

	if (terminal->api_output != NULL)
		terminal->api_output(terminal->api_output_context, ST_CTERM_OUTPUT_RESPONSE,
		    (const uint8_t *)bytes, size);
}

static void
emit_user(const char *bytes, size_t size, void *context)
{
	st_cterm_t *terminal = context;

	if (terminal->api_output != NULL)
		terminal->api_output(terminal->api_output_context, ST_CTERM_OUTPUT_USER, (const uint8_t *)bytes,
		    size);
}

static void
emit_size(struct cterminal *terminal, int columns, int rows, int pixel_width, int pixel_height, void *context)
{
	st_cterm_t *owner = context;
	uint32_t    cell_width;
	uint32_t    cell_height;

	if ((pixel_width < 0 || pixel_height < 0)
	    && st_console_cell_pixel_size(terminal->screen.console, &cell_width, &cell_height) == 0
	    && cell_width != 0 && cell_height != 0 && columns >= 0 && rows >= 0
	    && (uint32_t)columns <= UINT32_MAX / cell_width && (uint32_t)rows <= UINT32_MAX / cell_height) {
		pixel_width  = columns * (int)cell_width;
		pixel_height = rows * (int)cell_height;
	}

	if (owner->api_size_changed != NULL)
		owner->api_size_changed(owner->api_size_context, (uint32_t)columns, (uint32_t)rows,
		    pixel_width < 0 ? 0 : (uint32_t)pixel_width, pixel_height < 0 ? 0 : (uint32_t)pixel_height);
}

enum {
	MOUSE_X10        = UINT16_C(1) << 0,
	MOUSE_NORMAL     = UINT16_C(1) << 1,
	MOUSE_BUTTON     = UINT16_C(1) << 2,
	MOUSE_ANY        = UINT16_C(1) << 3,
	MOUSE_FOCUS      = UINT16_C(1) << 4,
	MOUSE_UTF8       = UINT16_C(1) << 5,
	MOUSE_SGR        = UINT16_C(1) << 6,
	MOUSE_ALT_SCROLL = UINT16_C(1) << 7,
	MOUSE_URXVT      = UINT16_C(1) << 8,
	MOUSE_SGR_PIXELS = UINT16_C(1) << 9,
};

static uint16_t
mouse_mode_bit(int parameter)
{
	switch (parameter) {
		case 9:
			return MOUSE_X10;
		case 1000:
			return MOUSE_NORMAL;
		case 1002:
			return MOUSE_BUTTON;
		case 1003:
			return MOUSE_ANY;
		case 1004:
			return MOUSE_FOCUS;
		case 1005:
			return MOUSE_UTF8;
		case 1006:
			return MOUSE_SGR;
		case 1007:
			return MOUSE_ALT_SCROLL;
		case 1015:
			return MOUSE_URXVT;
		case 1016:
			return MOUSE_SGR_PIXELS;
		default:
			return 0;
	}
}

static void
mouse_mode_changed(int parameter, int enabled, void *context)
{
	st_cterm_t *terminal = context;
	uint16_t    bit      = mouse_mode_bit(parameter);

	if (enabled)
		terminal->api_mouse_modes |= bit;
	else
		terminal->api_mouse_modes &= (uint16_t)~bit;
}

static int
mouse_mode_query(int parameter, void *context)
{
	st_cterm_t *terminal = context;

	return (terminal->api_mouse_modes & mouse_mode_bit(parameter)) != 0;
}

static void
destroy_status_sub(st_cterm_t *terminal)
{
	if (terminal->status_sub != NULL) {
		(void)cterm_render_set_hold_update(terminal->status_sub, 0);
		cterm_end(terminal->status_sub, false);
		terminal->status_sub = NULL;
	}
}

static void
clear_reserved_status(st_cterm_t *terminal)
{
	struct st_console_cell blank = {
	    .foreground = ST_CONSOLE_COLOR_RGB_VALUE(170, 170, 170),
	    .background = ST_CONSOLE_COLOR_RGB_VALUE(0, 0, 0),
	    .font       = terminal->screen.fonts.normal,
	    .glyph_font = terminal->screen.fonts.normal,
	};
	struct st_console_rect row;
	uint8_t                width;
	int                    status;

	row = (struct st_console_rect) {
	    .x      = (uint32_t)(terminal->x - 1),
	    .y      = (uint32_t)(terminal->y + terminal->height - 1),
	    .width  = (uint32_t)terminal->width,
	    .height = 1,
	};
	status = st_console_font_lookup(terminal->screen.console, blank.font, ' ', &blank.glyph_font, &blank.glyph,
	    &width);
	cterm_render_record_error(terminal, status);
	if (status != 0)
		return;
	cterm_render_record_error(terminal, st_console_fill(terminal->screen.console, &row, &blank));
}

static void
status_display_changed(struct cterminal *terminal, int old_type, int new_type, void *context)
{
	struct cterm_render_state screen;
	uint32_t                  columns;
	uint32_t                  rows;
	int                       available;
	int                       new_rows;
	int                       saved_x;
	int                       saved_y;
	(void)context;

	if (terminal->device.fixed_status_rows && (old_type >= 1) != (new_type >= 1)) {
		terminal->status_display_type = old_type;
		return;
	}
	if (st_console_size(terminal->screen.console, &columns, &rows) != 0) {
		terminal->status_display_type = old_type;
		cterm_render_record_error(terminal, IO_ERROR_IO);
		return;
	}
	available = terminal->height + (old_type >= 1 ? 1 : 0);
	new_rows  = available - (new_type >= 1 ? 1 : 0);
	if (new_rows < 1 || columns > INT_MAX
	    || (uint32_t)(terminal->y - 1 + new_rows + (new_type >= 1 ? 1 : 0)) > rows) {
		terminal->status_display_type = old_type;
		cterm_render_record_error(terminal, IO_ERROR_OUT_OF_RANGE);
		return;
	}
	cterm_resize_rows(terminal, new_rows);
	if (new_type != 2) {
		/* A subterminal may currently be bubbling this callback. */
		terminal->api_status_destroy_pending = terminal->status_sub != NULL;
		if (new_type == 1 && terminal->status_sub == NULL)
			clear_reserved_status(terminal);
		return;
	}
	terminal->api_status_destroy_pending = false;
	if (terminal->status_sub != NULL)
		return;
	cterm_coord_get_xy(terminal, CTERM_COORD_TERM, &saved_x, &saved_y);
	screen                               = terminal->screen;
	screen.addressing_window             = (struct st_console_rect) {0};
	screen.unicode                       = NULL;
	screen.provisional_columns           = 0;
	screen.provisional_discarded         = false;
	screen.hold_update                   = 0;
	screen.error                         = 0;
	screen.hyperlink_root_registered     = false;
	const struct st_cterm_config initial = {
	    .yellow_is_yellow = terminal->yellow_is_yellow,
	    .palette_override = terminal->has_palette_override ? terminal->palette_override : NULL,
	};
	terminal->status_sub = cterm_init(1, terminal->width, terminal->x, terminal->y + terminal->height, 0,
	    terminal->width, NULL, terminal->emulation, &screen, &initial);
	if (terminal->status_sub == NULL) {
		terminal->status_display_type = old_type;
		cterm_resize_rows(terminal, available - (old_type >= 1 ? 1 : 0));
		cterm_render_record_error(terminal, IO_ERROR_OUT_OF_MEMORY);
		return;
	}
	terminal->status_sub->parent           = terminal;
	terminal->status_sub->wire_encoding    = terminal->wire_encoding;
	terminal->status_sub->quiet            = terminal->quiet;
	terminal->status_sub->response_cb      = terminal->response_cb;
	terminal->status_sub->response_cbdata  = terminal->response_cbdata;
	terminal->status_sub->keystroke_cb     = terminal->keystroke_cb;
	terminal->status_sub->keystroke_cbdata = terminal->keystroke_cbdata;
	cterm_start(terminal->status_sub);
	cterm_apply_window(terminal);
	cterm_set_position(terminal, saved_x, saved_y);
}

static bool
utf8_supported(enum st_cterm_emulation emulation)
{
	return emulation == ST_CTERM_ANSI_BBS || emulation == ST_CTERM_ATARI_ST_VT52;
}

static int
mark_hyperlinks(void *context, st_hyperlink_mark_fn mark, void *mark_context)
{
	st_cterm_t             *terminal = context;
	struct st_console_cell *cells;
	struct st_console_rect  rect;
	uint32_t                columns;
	uint32_t                rows;
	size_t                  count;
	int                     status;

	status = mark(mark_context, terminal->screen.current_hyperlink);
	if (status != 0)
		return status;
	if (terminal->status_sub != NULL) {
		status = mark(mark_context, terminal->status_sub->screen.current_hyperlink);
		if (status != 0)
			return status;
	}
	if (terminal->backlines > 0 && terminal->scrollback != NULL) {
		count = (size_t)terminal->backlines * (size_t)terminal->backwidth;
		for (size_t i = 0; i < count; ++i) {
			status = mark(mark_context, terminal->scrollback[i].hyperlink);
			if (status != 0)
				return status;
		}
	}
	status = st_console_size(terminal->screen.console, &columns, &rows);
	if (status != 0)
		return status;
	if (columns == 0 || rows == 0)
		return 0;
	if ((size_t)columns > SIZE_MAX / rows || (size_t)columns * rows > SIZE_MAX / sizeof(*cells))
		return IO_ERROR_OUT_OF_RANGE;
	count = (size_t)columns * rows;
	cells = malloc(count * sizeof(*cells));
	if (cells == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	rect   = (struct st_console_rect) {0, 0, columns, rows};
	status = st_console_read_cells(terminal->screen.console, &rect, cells, columns);
	for (size_t i = 0; status == 0 && i < count; ++i)
		status = mark(mark_context, cells[i].hyperlink);
	free(cells);
	return status;
}

static void
default_palette_mapping(enum st_cterm_emulation emulation, enum st_cterm_petscii_machine petscii_machine,
    uint32_t mapping[16])
{
	static const uint16_t colour[16] = {
	    0,
	    4,
	    2,
	    6,
	    1,
	    5,
	    3,
	    7,
	    8,
	    12,
	    10,
	    14,
	    9,
	    13,
	    11,
	    15,
	};
	static const uint16_t c64[16] = {
	    256,
	    257,
	    258,
	    259,
	    260,
	    261,
	    262,
	    263,
	    264,
	    265,
	    266,
	    267,
	    268,
	    269,
	    270,
	    271,
	};
	static const uint16_t atari[16] = {
	    272,
	    273,
	    273,
	    273,
	    273,
	    273,
	    273,
	    273,
	    273,
	    273,
	    273,
	    273,
	    273,
	    273,
	    273,
	    273,
	};
	static const uint16_t prestel[16] = {
	    0,
	    21,
	    46,
	    51,
	    196,
	    201,
	    226,
	    15,
	    0,
	    21,
	    46,
	    51,
	    196,
	    201,
	    226,
	    15,
	};
	static const uint16_t atari_st[16] = {
	    15,
	    196,
	    46,
	    226,
	    21,
	    201,
	    51,
	    274,
	    275,
	    276,
	    277,
	    278,
	    279,
	    280,
	    281,
	    0,
	};
	const uint16_t *source = colour;

	switch (emulation) {
		case ST_CTERM_PETSCII:
			source = petscii_machine == ST_CTERM_PETSCII_C128_80 ? colour : c64;
			break;
		case ST_CTERM_ATASCII:
			source = atari;
			break;
		case ST_CTERM_PRESTEL:
		case ST_CTERM_BBC:
			source = prestel;
			break;
		case ST_CTERM_ATARI_ST_VT52:
			source = atari_st;
			break;
		default:
			break;
	}
	for (size_t i = 0; i < 16; ++i)
		mapping[i] = source[i];
}

static int
initialize_render_state(struct cterm_render_state *screen, const struct st_cterm_config *config)
{
	uint32_t capabilities;
	int      mode = CTERM_MODE_IBM;
	int      default_attribute =
	    config->emulation == ST_CTERM_PETSCII || config->emulation == ST_CTERM_ATARI_ST_VT52 ? 15 : 7;
	int status;

	status = st_console_capabilities(config->console, &capabilities);
	if (status != 0)
		return status;
	if (config->emulation == ST_CTERM_PETSCII) {
		switch (config->petscii_machine) {
			case ST_CTERM_PETSCII_C64:
				mode = CTERM_MODE_C64_40;
				break;
			case ST_CTERM_PETSCII_C128_40:
				mode = CTERM_MODE_C128_40;
				break;
			case ST_CTERM_PETSCII_C128_80:
				mode = CTERM_MODE_C128_80;
				break;
			default:
				return IO_ERROR_INVALID_ARGUMENT;
		}
	}
	screen->mode              = mode;
	screen->default_attribute = (uint8_t)default_attribute;
	if (config->palette_mapping != NULL) {
		for (size_t i = 0; i < 16; ++i)
			screen->mode_palette[i] = config->palette_mapping[i];
	}
	else
		default_palette_mapping(config->emulation, config->petscii_machine, screen->mode_palette);
	for (unsigned i = 0; i < 8; ++i) {
		unsigned attr = cterm_ansi_colour_order[i];
		uint32_t normal = config->palette_override != NULL ? config->palette_override[attr]
		    : cterm_default_colour((uint16_t)screen->mode_palette[attr], config->yellow_is_yellow);
		screen->faint_defaults[i] = (normal & 0xfefefe) >> 1;
	}
	screen->capabilities = capabilities & ~config->disabled_capabilities;
	screen->wscroll      = 1;
	return 0;
}

int
st_cterm_create(st_cterm_t **result, const struct st_cterm_config *config)
{
	struct cterm_render_state screen     = {0};
	struct st_console_cell   *scrollback = NULL;
	st_cterm_t               *terminal;
	uint16_t                  normal;
	uint32_t                  columns;
	uint32_t                  rows;
	uint32_t                  terminal_rows;
	uint32_t                  status_rows;
	int                       status;

	if (result == NULL || config == NULL || config->console == NULL || config->fonts == NULL
	    || st_font_catalog_console(config->fonts) != config->console || config->emulation < ST_CTERM_ANSI_BBS
	    || config->emulation > ST_CTERM_ATARI_ST_VT52 || config->encoding < ST_CTERM_ENCODING_UTF8
	    || config->encoding > ST_CTERM_ENCODING_CODEPAGE
	    || (config->graphics_mode != ST_CTERM_GRAPHICS_OFF && config->graphics_mode != ST_CTERM_GRAPHICS_RIP_3)
	    || (config->graphics_mode != ST_CTERM_GRAPHICS_OFF && config->emulation != ST_CTERM_ANSI_BBS)
	    || config->codepage >= ST_CODEPAGE_COUNT
	    || (config->encoding == ST_CTERM_ENCODING_UTF8 && !utf8_supported(config->emulation))
	    || !st_font_catalog_identifier(config->fonts, config->font_selector, &normal)
	    || st_console_size(config->console, &columns, &rows) != 0 || config->terminal_rows > rows
	    || config->scrollback_lines > INT_MAX)
		return IO_ERROR_INVALID_ARGUMENT;
	if (columns > INT_MAX || rows > INT_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	status = initialize_render_state(&screen, config);
	if (status != 0)
		return status;
	status_rows = config->status_line ? 1 : 0;
	if (rows <= status_rows)
		return IO_ERROR_INVALID_ARGUMENT;
	terminal_rows = config->terminal_rows == 0 ? rows - status_rows : config->terminal_rows;
	if (terminal_rows + status_rows > rows)
		return IO_ERROR_INVALID_ARGUMENT;
	if (config->scrollback_lines != 0) {
		if ((size_t)columns > SIZE_MAX / config->scrollback_lines
		    || (size_t)columns * config->scrollback_lines > SIZE_MAX / sizeof(*scrollback))
			return IO_ERROR_OUT_OF_RANGE;
		scrollback = calloc((size_t)columns * config->scrollback_lines, sizeof(*scrollback));
		if (scrollback == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
	}
	screen.console    = config->console;
	screen.audio      = config->audio;
	screen.hyperlinks = config->hyperlinks;
	screen.codepage   = config->codepage;
	screen.fonts      = (struct st_console_font_selection) {
	    .normal       = normal,
	    .bright       = normal,
	    .blink        = normal,
	    .bright_blink = normal,
	};
	for (size_t selector = 0; selector < UINT8_MAX + 1U; ++selector) {
		uint16_t identifier;

		if (st_font_catalog_identifier(config->fonts, (uint8_t)selector, &identifier)) {
			screen.font_identifiers[selector] = identifier;
			screen.font_valid[selector]       = true;
			screen.font_codepages[selector] =
			    st_font_catalog_codepage(config->fonts, (uint8_t)selector);
		}
	}
	for (size_t slot = 0; slot <= ST_CTERM_FONT_COUNT; ++slot)
		screen.selectors[slot] = config->font_selector;
	terminal = cterm_init((int)terminal_rows, (int)columns, 1, 1, (int)config->scrollback_lines, (int)columns,
	    scrollback, config->emulation, &screen, config);
	if (terminal == NULL) {
		free(scrollback);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	status = cterm_render_take_error(terminal);
	if (status != 0) {
		cterm_end(terminal, false);
		free(scrollback);
		return status;
	}
	terminal->graphics_mode = config->graphics_mode;
	if (config->graphics_mode == ST_CTERM_GRAPHICS_RIP_3) {
		terminal->screen.addressing_window = (struct st_console_rect) {0, 0, columns, terminal_rows};
		terminal->device.pixel_history     = true;
		terminal->last_column_flag         = CTERM_LCF_FORCED | CTERM_LCF_ENABLED;
	}
	terminal->escape_extension = config->escape_extension;
	terminal->escape_context   = config->escape_context;

	terminal->quiet                   = config->quiet;
	terminal->atari_st_key_clicks     = true;
	terminal->lf_expand               = config->expand_lf;
	terminal->status_display_type      = config->status_line ? 1 : 0;
	terminal->api_reserved_rows        = rows - terminal_rows - status_rows;
	terminal->wire_encoding            = config->encoding;
	terminal->api_input_filter         = config->input_filter;
	terminal->api_input_filter_active  = config->input_filter_active;
	terminal->api_input_filter_context = config->input_filter_context;
	terminal->api_capture              = config->capture;
	terminal->api_capture_active       = config->capture_active;
	terminal->api_capture_context      = config->capture_context;
	terminal->api_output               = config->output;
	terminal->api_output_context       = config->output_context;
	terminal->api_apc                  = config->apc;
	terminal->api_apc_context          = config->apc_context;
	if (config->apc_directory != NULL) {
		terminal->apc = calloc(1, sizeof(*terminal->apc));
		if (terminal->apc == NULL) {
			st_cterm_destroy(&terminal);
			return IO_ERROR_OUT_OF_MEMORY;
		}
		terminal->apc->directory = config->apc_directory;
		terminal->apc->version   = config->application_version;
	}
	terminal->history_display           = config->history_display;
	terminal->api_size_changed          = config->size_changed;
	terminal->api_size_context          = config->size_context;
	terminal->api_speed_query           = config->speed_query;
	terminal->api_speed_changed         = config->speed_changed;
	terminal->api_speed_context         = config->speed_context;
	terminal->response_cb               = emit_response;
	terminal->response_cbdata           = terminal;
	terminal->keystroke_cb              = emit_user;
	terminal->keystroke_cbdata          = terminal;
	terminal->size_change_cb            = config->size_changed == NULL ? NULL : emit_size;
	terminal->size_change_cbdata        = terminal;
	terminal->status_display_cb         = status_display_changed;
	terminal->status_display_cbdata     = terminal;
	terminal->mouse_state_change        = mouse_mode_changed;
	terminal->mouse_state_change_cbdata = terminal;
	terminal->mouse_state_query         = mouse_mode_query;
	terminal->mouse_state_query_cbdata  = terminal;
	if (config->status_line) {
		clear_reserved_status(terminal);
		status = cterm_render_take_error(terminal);
		if (status != 0) {
			st_cterm_destroy(&terminal);
			return status;
		}
	}
	{
		status = cterm_text_create(terminal);
		if (status != 0) {
			st_cterm_destroy(&terminal);
			return status;
		}
	}
	if (config->hyperlinks != NULL) {
		status = st_hyperlink_registry_register_root(config->hyperlinks, mark_hyperlinks, terminal);
		if (status != 0) {
			st_cterm_destroy(&terminal);
			return status;
		}
		terminal->screen.hyperlink_root_registered = true;
	}
	*result = terminal;
	return 0;
}

void
st_cterm_destroy(st_cterm_t **terminal)
{
	struct st_console_cell *scrollback;

	if (terminal == NULL || *terminal == NULL)
		return;
	scrollback = (*terminal)->scrollback;
	(void)st_cterm_finish(*terminal);
	st_unicode_presentation_destroy(&(*terminal)->screen.unicode);
	destroy_status_sub(*terminal);
	input_cleanup(*terminal);
	(void)cterm_render_set_hold_update(*terminal, 0);
	if ((*terminal)->screen.hyperlink_root_registered) {
		st_hyperlink_registry_unregister_root((*terminal)->screen.hyperlinks, mark_hyperlinks, *terminal);
		(*terminal)->screen.hyperlink_root_registered = false;
	}
	cterm_end(*terminal, false);
	free(scrollback);
	*terminal = NULL;
}

static bool
input_filter_enabled(const st_cterm_t *terminal)
{
	return !terminal->api_input_filter_suppressed && terminal->api_input_filter != NULL
	       && (terminal->api_input_filter_active == NULL
	           || terminal->api_input_filter_active(terminal->api_input_filter_context));
}

static bool
capture_enabled(const st_cterm_t *terminal)
{
	return terminal->api_capture != NULL
	       && (terminal->api_capture_active == NULL
	           || terminal->api_capture_active(terminal->api_capture_context));
}

static int
capture_scalar(st_cterm_t *terminal, uint32_t scalar)
{
	char      utf8[4];
	ptrdiff_t size;

	if (!capture_enabled(terminal))
		return 0;
	size = st_utf8_encode_one(utf8, sizeof(utf8), scalar);
	if (size < 0)
		return IO_ERROR_INVALID_ENCODING;
	return terminal->api_capture(terminal->api_capture_context, utf8, (size_t)size, false);
}

static int
filter_input_scalar(st_cterm_t *terminal, uint32_t scalar, bool *consumed, char *replacement,
    size_t *replacement_size)
{
	int status;

	*consumed         = false;
	*replacement_size = 0;
	if (terminal->api_input_filter_suppressed || terminal->api_input_filter == NULL)
		return 0;
	status = terminal->api_input_filter(terminal->api_input_filter_context, scalar, consumed, replacement,
	    ST_CTERM_INPUT_REPLACEMENT_CAPACITY, replacement_size);
	if (status != 0)
		return status;
	return *replacement_size > ST_CTERM_INPUT_REPLACEMENT_CAPACITY || (!*consumed && *replacement_size != 0)
		   ? IO_ERROR_INVALID_FORMAT
		   : 0;
}

static int
present_filtered_scalar(void *context, uint32_t scalar, bool *consumed)
{
	st_cterm_t *terminal = context;
	char        replacement[ST_CTERM_INPUT_REPLACEMENT_CAPACITY];
	size_t      replacement_size = 0;
	size_t      written;
	int         status = 0;

	*consumed = false;
	if (input_filter_enabled(terminal))
		status = filter_input_scalar(terminal, scalar, consumed, replacement, &replacement_size);

	if (status != 0)
		return status;
	if (!*consumed)
		return capture_scalar(terminal, scalar);
	if (replacement_size == 0)
		return 0;
	status = st_cterm_write_utf8(terminal, replacement, replacement_size, &written);
	return status == 0 && written != replacement_size ? IO_ERROR_IO : status;
}

static st_cterm_t *
input_owner(st_cterm_t *terminal)
{
	while (terminal->parent != NULL)
		terminal = terminal->parent;
	return terminal;
}

static st_cterm_t *
input_display(st_cterm_t *owner)
{
	return owner->status_sub != NULL && owner->status_display_active == 1 ? owner->status_sub : owner;
}

bool
st_cterm_escape_pending(st_cterm_t *terminal)
{
	if (terminal == NULL)
		return false;
	st_cterm_t *owner = input_owner(terminal);
	return input_display(owner)->accumulator != NULL;
}

/* Own only incomplete syntax. Completed settings, text, macros and graphics
 * stay on the terminal. A scene can replace the current text window, so a
 * suspended input object cannot borrow buffers from the previous window. */
struct partial_input {
	cterm_accumulator_fn    accumulator, saved_accumulator;
	int                     sequence, string;
	char                   *strbuf;
	size_t                  strbuflen, strbufsize, escbufsz;
	char                    escbuf[sizeof(((st_cterm_t *)0)->escbuf)];
	uint32_t                seq_key;
	enum seq_phase          seq_phase;
	char                    seq_params[sizeof(((st_cterm_t *)0)->seq_params)];
	uint64_t                seq_param_int[MAX_SEQ_PARAMS];
	int                     seq_param_len, seq_param_count;
	uint64_t                seq_cur_value;
	bool                    seq_cur_has_digits, seq_cur_has_sub, seq_overflow;
	void                    (*seq_trailing_handler)(st_cterm_t *, int *);
	uint8_t                 seq_trailing_remain;
	int                     doorway_char, music, font_read, font_slot, font_size;
	char                    musicbuf[sizeof(((st_cterm_t *)0)->musicbuf)];
	char                    fontbuf[sizeof(((st_cterm_t *)0)->fontbuf)];
	int                     macro, macro_num, macro_del, macro_encoding;
	int                     sixel, sx_iv, sx_ih, sx_trans, sx_repeat, sx_left, sx_x, sx_y;
	uint32_t                sx_fg, sx_bg;
	int                     sx_pixels_sent, sx_first_pass, sx_start_x, sx_start_y, sx_row_max_x;
	struct st_pixel_buffer *sx_pixels;
	int                     sx_width, sx_height;
	struct st_pixel_mask   *sx_mask;
};

struct st_cterm_suspended_input {
	struct partial_input main, status;
};

static void
swap_partial_input(st_cterm_t *terminal, struct partial_input *saved)
{
	/* The same exchange moves owned pointers and inline arrays in both
	 * directions. Handler scratch pointers are rebuilt on sequence completion. */
#define SWAP_FIELD(field)                                                                            \
	do {                                                                                         \
		unsigned char previous[sizeof(terminal->field)];                                     \
		_Static_assert(sizeof(terminal->field) == sizeof(saved->field), "input field size"); \
		memcpy(previous, &terminal->field, sizeof(previous));                                \
		memcpy(&terminal->field, &saved->field, sizeof(previous));                           \
		memcpy(&saved->field, previous, sizeof(previous));                                   \
	} while (0)
	SWAP_FIELD(accumulator);
	SWAP_FIELD(saved_accumulator);
	SWAP_FIELD(sequence);
	SWAP_FIELD(string);
	SWAP_FIELD(strbuf);
	SWAP_FIELD(strbuflen);
	SWAP_FIELD(strbufsize);
	SWAP_FIELD(escbuf);
	SWAP_FIELD(escbufsz);
	SWAP_FIELD(seq_key);
	SWAP_FIELD(seq_phase);
	SWAP_FIELD(seq_params);
	SWAP_FIELD(seq_param_int);
	SWAP_FIELD(seq_param_len);
	SWAP_FIELD(seq_param_count);
	SWAP_FIELD(seq_cur_value);
	SWAP_FIELD(seq_cur_has_digits);
	SWAP_FIELD(seq_cur_has_sub);
	SWAP_FIELD(seq_overflow);
	SWAP_FIELD(seq_trailing_handler);
	SWAP_FIELD(seq_trailing_remain);
	SWAP_FIELD(doorway_char);
	SWAP_FIELD(music);
	SWAP_FIELD(musicbuf);
	SWAP_FIELD(font_read);
	SWAP_FIELD(font_slot);
	SWAP_FIELD(font_size);
	SWAP_FIELD(fontbuf);
	SWAP_FIELD(macro);
	SWAP_FIELD(macro_num);
	SWAP_FIELD(macro_del);
	SWAP_FIELD(macro_encoding);
	SWAP_FIELD(sixel);
	SWAP_FIELD(sx_iv);
	SWAP_FIELD(sx_ih);
	SWAP_FIELD(sx_trans);
	SWAP_FIELD(sx_repeat);
	SWAP_FIELD(sx_left);
	SWAP_FIELD(sx_x);
	SWAP_FIELD(sx_y);
	SWAP_FIELD(sx_fg);
	SWAP_FIELD(sx_bg);
	SWAP_FIELD(sx_pixels_sent);
	SWAP_FIELD(sx_first_pass);
	SWAP_FIELD(sx_start_x);
	SWAP_FIELD(sx_start_y);
	SWAP_FIELD(sx_row_max_x);
	SWAP_FIELD(sx_pixels);
	SWAP_FIELD(sx_width);
	SWAP_FIELD(sx_height);
	SWAP_FIELD(sx_mask);
#undef SWAP_FIELD
	memset(terminal->seq_param_strs, 0, sizeof(terminal->seq_param_strs));
}

static bool
partial_input_pending(const st_cterm_t *terminal)
{
	return terminal != NULL
	       && (terminal->accumulator != NULL || terminal->strbuf != NULL || terminal->sixel != SIXEL_INACTIVE);
}

static void
suspend_partial_input(st_cterm_t *terminal, struct partial_input *saved)
{
	if (!partial_input_pending(terminal))
		return;
	if (terminal->sixel == SIXEL_STARTED) {
		(void)cterm_render_set_hold_update(terminal, terminal->sx_hold_update);
		terminal->cursor = terminal->sx_orig_cursor;
		cterm_render_set_cursor(terminal, terminal->cursor);
	}
	swap_partial_input(terminal, saved);
}

int
st_cterm_suspend_input(st_cterm_t *terminal, struct st_cterm_suspended_input **result)
{
	if (terminal == NULL || result == NULL || terminal->input_depth != 0
	    || (terminal->status_sub != NULL && terminal->status_sub->input_depth != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	struct st_cterm_suspended_input *saved = NULL;
	if (partial_input_pending(terminal) || partial_input_pending(terminal->status_sub)) {
		saved = calloc(1, sizeof(*saved));
		if (saved == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		suspend_partial_input(terminal, &saved->main);
		suspend_partial_input(terminal->status_sub, &saved->status);
	}
	*result = saved;
	return 0;
}

static void
resume_partial_input(st_cterm_t *terminal, struct partial_input *saved)
{
	if (terminal != NULL) {
		cterm_dec_sixel_cancel(terminal);
		CTERM_FREE_AND_CLEAR(terminal->strbuf);
		terminal->strbuflen = terminal->strbufsize = 0;
		terminal->accumulator = terminal->saved_accumulator = NULL;
		terminal->sequence = terminal->string = 0;
		terminal->seq_phase                   = SEQ_PHASE_IDLE;
		terminal->escbufsz                    = 0;
		terminal->seq_param_len = terminal->seq_param_count = 0;
		terminal->seq_trailing_remain                       = 0;
		terminal->font_read = terminal->font_size = terminal->music = terminal->doorway_char = 0;
		terminal->macro = MACRO_INACTIVE;
		if (saved != NULL)
			swap_partial_input(terminal, saved);
		if (terminal->sixel == SIXEL_STARTED) {
			terminal->sx_hold_update = cterm_render_hold_update(terminal);
			terminal->sx_orig_cursor = terminal->cursor;
			terminal->cursor         = CTERM_CURSOR_HIDDEN;
			cterm_render_set_cursor(terminal, terminal->cursor);
			(void)cterm_render_set_hold_update(terminal, 1);
		}
	}
	if (saved != NULL) {
		free(saved->strbuf);
		st_pixel_buffer_destroy(saved->sx_pixels);
		st_pixel_mask_destroy(saved->sx_mask);
	}
}

void
st_cterm_resume_input(st_cterm_t *terminal, struct st_cterm_suspended_input *saved)
{
	resume_partial_input(terminal, saved == NULL ? NULL : &saved->main);
	resume_partial_input(terminal == NULL ? NULL : terminal->status_sub,
	    saved == NULL ? NULL : &saved->status);
	free(saved);
}

static void
input_cleanup(st_cterm_t *owner)
{
	if (owner->api_status_destroy_pending) {
		st_cterm_t *sub = owner->status_sub;
		if (sub != NULL && sub->input_depth != 0) {
			sub->retired_status_next = owner->retired_status;
			owner->retired_status    = sub;
			owner->status_sub        = NULL;
		}
		else
			destroy_status_sub(owner);
		owner->api_status_destroy_pending = false;
		if (owner->status_display_type != 0)
			clear_reserved_status(owner);
	}
	st_cterm_t **link = &owner->retired_status;
	while (*link != NULL) {
		st_cterm_t *sub = *link;
		if (sub->input_depth != 0) {
			link = &sub->retired_status_next;
			continue;
		}
		*link = sub->retired_status_next;
		cterm_render_record_error(owner, cterm_text_boundary(sub));
		cterm_render_record_error(owner, cterm_render_take_error(sub));
		cterm_end(sub, false);
	}
}

int
cterm_input_decode_error(struct cterminal *terminal)
{
	return terminal->screen.capture_decode_error ? capture_scalar(input_owner(terminal), UINT32_C(0xfffd)) : 0;
}

static int
write_input(struct cterminal *terminal, const void *bytes, size_t size, size_t *consumed,
    enum cterm_input_source source, const struct st_utf8_decode_event *decoded)
{
	const uint8_t *input = bytes;
	st_cterm_t    *owner;
	bool           utf8;
	bool           hooks;
	bool           restore;
	int            status;
	int            speed = 0;

	if (consumed == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	*consumed = 0;
	if (terminal == NULL || (bytes == NULL && size != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	owner   = input_owner(terminal);
	restore = source == CTERM_INPUT_REPLAY && terminal->input_depth != 0;
	utf8    = source == CTERM_INPUT_SEMANTIC_UTF8 || owner->wire_encoding == ST_CTERM_ENCODING_UTF8;
	hooks   = source != CTERM_INPUT_REPLAY;
	if (source == CTERM_INPUT_WIRE && owner->log == CTERM_LOG_RAW && owner->logfile != NULL && size != 0)
		fwrite(bytes, size, 1, owner->logfile);
	status = cterm_render_take_error(owner);
	if (status == 0 && owner->status_sub != NULL)
		status = cterm_render_take_error(owner->status_sub);
	while (*consumed < size && status == 0) {
		st_cterm_t *target = input_display(owner);
		if (target->deferred_escape != 0)
			break;
		uint8_t byte      = input[*consumed];
		bool    text_mode = utf8 || target->emulation == CTERM_EMULATION_ANSI_BBS
		                    || target->emulation == CTERM_EMULATION_ATARIST_VT52;
		bool    parser    = !text_mode || target->accumulator != NULL || byte < 0x20 || byte == 0x7f;

		/* A non-ASCII scalar cannot finish a single-byte ESC command. */
		if (utf8 && byte >= 0x80 && target->accumulator == cterm_accumulate_ecma_seq
		    && target->seq_phase == SEQ_PHASE_C1 && target->seq_key == SEQ_ESC
		    && target->saved_accumulator == NULL) {
			target->accumulator = NULL;
			target->seq_phase   = SEQ_PHASE_IDLE;
			target->sequence    = 0;
			cterm_text_literal(target, 0x1b);
			parser = false;
		}
		if (parser) {
			bool boundary = target->accumulator != NULL || byte < 0x20 || byte == 0x7f;
			if (target->ctrl_bitmap != NULL)
				boundary = (target->ctrl_bitmap[byte >> 3] & (1u << (byte & 7))) != 0;
			if (target->emulation == CTERM_EMULATION_PRESTEL
			    || target->emulation == CTERM_EMULATION_BEEB)
				boundary =
				    byte < 0x20 || (byte >= 0x7f && byte < 0xa0) || target->accumulator != NULL;
			status = boundary ? cterm_text_boundary(target) : 0;
			if (status != 0)
				break;
			if (input_display(owner) != target) {
				input_cleanup(owner);
				continue;
			}
			/* Protocol accumulators own their original bytes, including
			 * binary uploads and stored macro definitions. */
			if (hooks && target->accumulator == NULL) {
				bool     filtered;
				uint32_t scalar =
				    utf8 ? byte : st_codepage_to_unicode(target->screen.codepage, byte, false);
				++target->input_depth;
				status = present_filtered_scalar(owner, scalar, &filtered);
				--target->input_depth;
				if (status != 0)
					break;
				if (filtered) {
					++*consumed;
					input_cleanup(owner);
					continue;
				}
				target = input_display(owner);
			}
			bool protocol = target->accumulator != NULL;
			++target->input_depth;
			bool used = target->device.control != NULL && target->accumulator == NULL
			            && target->device.control(target->device.context, byte);
			if (!used)
				used = cterm_parse_byte(target, byte, &speed);
			if (used) {
				++*consumed;
				if (hooks && protocol && capture_enabled(owner))
					status = owner->api_capture(owner->api_capture_context,
					    (const char *)&byte, 1, true);
			}
			--target->input_depth;
			if (status == 0)
				status = cterm_render_take_error(target);
			if (status == 0 && used && source == CTERM_INPUT_WIRE && byte == 8
			    && target->device.wire_backspace != NULL)
				status = target->device.wire_backspace(target->device.context);
		}
		else {
			struct st_utf8_decode_event        cp;
			const struct st_utf8_decode_event *character = *consumed == 0 ? decoded : NULL;
			if (character == NULL && !utf8) {
				/* Hooks see the connection encoding. Only surviving display bytes
				 * take their meaning from the currently selected bitmap font. */
				if (hooks && (input_filter_enabled(owner) || capture_enabled(owner))) {
					bool filtered;
					uint32_t scalar = st_codepage_to_unicode(target->screen.codepage, byte, true);
					++target->input_depth;
					status = present_filtered_scalar(owner, scalar, &filtered);
					--target->input_depth;
					if (status != 0)
						break;
					if (filtered) {
						++*consumed;
						input_cleanup(owner);
						continue;
					}
					target = input_display(owner);
				}
				cp.kind        = ST_UTF8_DECODE_EVENT_SCALAR;
				cp.data.scalar = st_codepage_to_unicode(cterm_render_byte_codepage(target), byte, true);
				character      = &cp;
			}
			status = cterm_text_create(target);
			if (status != 0)
				break;
			target->screen.capture_decode_error = hooks;
			st_unicode_presentation_set_scalar_filter(target->screen.unicode,
			    hooks && character != &cp ? present_filtered_scalar : NULL, owner);
			++target->input_depth;
			if (character != NULL) {
				status = st_unicode_presentation_decoded(target->screen.unicode, character);
				*consumed += character == decoded ? size : 1;
			}
			else {
				size_t used = 0;
				status      = cterm_text_feed(target, &byte, 1, &used);
				*consumed += used;
			}
			st_unicode_presentation_set_scalar_filter(target->screen.unicode, NULL, NULL);
			--target->input_depth;
		}
		input_cleanup(owner);
		if (status == 0)
			status = cterm_render_take_error(owner);
	}
	input_cleanup(owner);
	if (restore) {
		/* Nested replay may select another display. Its caller's parser
		 * still needs its own window and cursor while unwinding. */
		cterm_apply_window(terminal);
		int x = terminal->xpos, y = terminal->ypos;
		cterm_coord_conv_xy(terminal, CTERM_COORD_ABSTERM, CTERM_COORD_CURR, &x, &y);
		cterm_render_set_position(terminal, x, y);
	}
	return status;
}

bool
st_cterm_input_bytes_pending(st_cterm_t *terminal)
{
	if (terminal == NULL)
		return false;
	st_cterm_t *target = input_display(input_owner(terminal));
	/* A lone ESC rejects a high byte and replays it as ordinary UTF-8. */
	bool bare_escape = target->accumulator == cterm_accumulate_ecma_seq && target->seq_phase == SEQ_PHASE_C1
	                   && target->seq_key == SEQ_ESC && target->saved_accumulator == NULL;
	return target->accumulator != NULL && !bare_escape;
}

int
st_cterm_write_decoded(st_cterm_t *terminal, const struct st_utf8_decode_event *event, const uint8_t *original,
    size_t size, size_t *consumed)
{
	if (event == NULL || event->kind == ST_UTF8_DECODE_EVENT_NONE || size == 0 || size > 4)
		return IO_ERROR_INVALID_ARGUMENT;
	return write_input(terminal, original, size, consumed, CTERM_INPUT_WIRE, event);
}

int
cterm_input_write(struct cterminal *terminal, const void *bytes, size_t size, size_t *consumed,
    enum cterm_input_source source)
{
	return write_input(terminal, bytes, size, consumed, source, NULL);
}

static int
write_plain(st_cterm_t *terminal, const void *bytes, size_t size, size_t *consumed, bool semantic_utf8)
{
	return cterm_input_write(terminal, bytes, size, consumed,
	    semantic_utf8 ? CTERM_INPUT_SEMANTIC_UTF8 : CTERM_INPUT_WIRE);
}

uint8_t
st_cterm_take_deferred_escape(st_cterm_t *terminal)
{
	if (terminal == NULL || terminal->input_depth != 0)
		return 0;
	uint8_t final = terminal->deferred_escape;
	terminal->deferred_escape = 0;
	return final;
}

int
st_cterm_write(st_cterm_t *terminal, const void *bytes, size_t size, size_t *consumed)
{
	if (consumed != NULL)
		*consumed = 0;
	if (terminal == NULL || consumed == NULL || (bytes == NULL && size != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	return write_plain(terminal, bytes, size, consumed, false);
}

bool
st_cterm_needs_poll(st_cterm_t *terminal)
{
	if (terminal == NULL)
		return false;
	if (terminal->apc != NULL) {
		for (size_t i = 0; i < sizeof(terminal->apc->notify) / sizeof(terminal->apc->notify[0]); ++i)
			if (terminal->apc->notify[i])
				return true;
	}
	return false;
}

int
st_cterm_poll(st_cterm_t *terminal)
{
	if (terminal == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	cterm_apc_poll(terminal);
	return 0;
}

int
st_cterm_write_utf8_input(st_cterm_t *terminal, const void *bytes, size_t size, size_t *consumed)
{
	int status;

	if (consumed != NULL)
		*consumed = 0;
	if (terminal == NULL || consumed == NULL || (bytes == NULL && size != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	if (terminal->screen.unicode == NULL) {
		status = cterm_text_create(terminal);
		if (status != 0)
			return status;
	}
	return write_plain(terminal, bytes, size, consumed, true);
}

int
st_cterm_write_utf8(st_cterm_t *terminal, const void *bytes, size_t size, size_t *consumed)
{
	bool suppressed;
	int  status;

	if (terminal == NULL)
		return st_cterm_write_utf8_input(terminal, bytes, size, consumed);
	suppressed                            = terminal->api_input_filter_suppressed;
	terminal->api_input_filter_suppressed = true;
	status                                = st_cterm_write_utf8_input(terminal, bytes, size, consumed);
	terminal->api_input_filter_suppressed = suppressed;
	return status;
}

int
st_cterm_finish(st_cterm_t *terminal)
{
	int status;

	if (terminal == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = cterm_text_boundary(terminal);
	if (status == 0 && terminal->status_sub != NULL)
		status = cterm_text_boundary(terminal->status_sub);
	input_cleanup(terminal);
	if (status == 0)
		status = cterm_render_take_error(terminal);
	if (status == 0 && terminal->status_sub != NULL)
		status = cterm_render_take_error(terminal->status_sub);
	return status;
}

int
st_cterm_prepare_disconnect(st_cterm_t *terminal)
{
	int status = st_cterm_finish(terminal);
	if (status == 0 && terminal->device.cursor_mode != NULL) {
		terminal->device.cursor_mode(terminal->device.context, true);
		status = cterm_render_take_error(terminal);
	}
	if (status != 0 || terminal->status_display_type != 0)
		return status;
	/* The local notice needs a new row, even if the host left a smaller
	 * scroll region. Keep the terminal dimensions and host margins intact. */
	int top = terminal->top_margin, bottom = terminal->bottom_margin;
	int left = terminal->left_margin, right = terminal->right_margin;
	terminal->top_margin = terminal->left_margin = 1;
	terminal->bottom_margin                      = terminal->height;
	terminal->right_margin                       = terminal->width;
	cterm_scrollup(terminal);
	terminal->top_margin    = top;
	terminal->bottom_margin = bottom;
	terminal->left_margin   = left;
	terminal->right_margin  = right;
	return cterm_render_take_error(terminal);
}

int
st_cterm_text_boundary(st_cterm_t *terminal)
{
	if (terminal == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	int status = cterm_text_boundary(terminal);
	if (status == 0 && terminal->status_sub != NULL)
		status = cterm_text_boundary(terminal->status_sub);
	return status;
}

int
st_cterm_start_text(st_cterm_t *terminal)
{
	if (terminal == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	cterm_start(terminal);
	return cterm_render_take_error(terminal);
}

int
st_cterm_position_text(st_cterm_t *terminal, uint32_t column, uint32_t row)
{
	if (terminal == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	cterm_start(terminal);
	int status = cterm_text_boundary(terminal);
	if (status != 0)
		return status;
	bool origin = terminal->screen.addressing_window.width == 0 && (terminal->extattr & CTERM_EXTATTR_ORIGINMODE);
	uint32_t columns =
	    (uint32_t)(origin ? terminal->right_margin - terminal->left_margin + 1 : terminal->width);
	uint32_t rows = (uint32_t)(origin ? terminal->bottom_margin - terminal->top_margin + 1 : terminal->height);
	if (column >= columns)
		column = columns - 1;
	if (row >= rows)
		row = rows - 1;
	cterm_clear_lcf(terminal);
	++terminal->cursor_position_changes;
	int x = (int)column + 1, y = (int)row + 1;
	if (terminal->screen.addressing_window.width != 0)
		cterm_coord_conv_xy(terminal, CTERM_COORD_ABSTERM, CTERM_COORD_CURR, &x, &y);
	cterm_render_set_position(terminal, x, y);
	cterm_coord_get_xy(terminal, CTERM_COORD_ABSTERM, &terminal->xpos, &terminal->ypos);
	return cterm_render_take_error(terminal);
}

int
st_cterm_set_text_cursor_visible(st_cterm_t *terminal, bool visible)
{
	if (terminal == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (terminal->screen.addressing_window.width == 0)
		return IO_ERROR_NOT_SUPPORTED;
	cterm_start(terminal);
	int status = cterm_text_boundary(terminal);
	if (status != 0)
		return status;
	cterm_set_cursor_visible(terminal, visible);
	return cterm_render_take_error(terminal);
}

int
st_cterm_present_text_cursor(st_cterm_t *terminal)
{
	if (terminal == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (terminal->screen.addressing_window.width == 0)
		return IO_ERROR_NOT_SUPPORTED;
	cterm_render_set_cursor(terminal, terminal->cursor);
	return cterm_render_take_error(terminal);
}

int
st_cterm_get_text_cursor_visible(const st_cterm_t *terminal, bool *visible)
{
	if (terminal == NULL || visible == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (terminal->screen.addressing_window.width == 0)
		return IO_ERROR_NOT_SUPPORTED;
	*visible = terminal->cursor != CTERM_CURSOR_HIDDEN;
	return 0;
}

static int
erase_text(st_cterm_t *terminal, bool whole, bool ansi, uint32_t rgb)
{
	if (terminal == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (terminal->screen.addressing_window.width == 0)
		return IO_ERROR_NOT_SUPPORTED;
	if (rgb > UINT32_C(0xffffff))
		return IO_ERROR_OUT_OF_RANGE;
	cterm_start(terminal);
	int status = cterm_text_boundary(terminal);
	if (status != 0)
		return status;
	struct st_console_rect area = {(uint32_t)(terminal->x - 1), (uint32_t)(terminal->y - 1),
	    (uint32_t)terminal->width, (uint32_t)terminal->height};
	if (!whole) {
		if (terminal->xpos < 1 || terminal->xpos > terminal->width || terminal->ypos < 1
		    || terminal->ypos > terminal->height)
			return IO_ERROR_OUT_OF_RANGE;
		area.x += (uint32_t)(terminal->xpos - 1);
		area.y += (uint32_t)(terminal->ypos - 1);
		area.width -= (uint32_t)(terminal->xpos - 1);
		area.height = 1;
	}
	struct st_console_cell blank;
	status = cterm_render_make_current_cell(terminal, &blank, ' ', 0);
	if (status != 0)
		return status;
	/* An erase is solid paint even when ANSI underline/inverse is selected.
	 * The terminal's current rendition and colors remain untouched. */
	blank.foreground = blank.background = ansi ? blank.background : ST_PIXEL_COLOR_RGB_TAG | rgb;
	blank.flags                         = 0;
	blank.hyperlink                     = 0;
	status                              = st_console_fill(terminal->screen.console, &area, &blank);
	if (status == 0 && whole)
		status = st_cterm_position_text(terminal, 0, 0);
	return status != 0 ? status : cterm_render_take_error(terminal);
}

int
st_cterm_erase_text(st_cterm_t *terminal, bool whole, uint32_t rgb)
{
	return erase_text(terminal, whole, false, rgb);
}

int
st_cterm_erase_ansi_text_eol(st_cterm_t *terminal)
{
	return erase_text(terminal, false, true, 0);
}

int
st_cterm_set_text_wrap(st_cterm_t *terminal, bool enabled)
{
	if (terminal == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (enabled)
		terminal->extattr |= CTERM_EXTATTR_AUTOWRAP;
	else {
		terminal->extattr &= ~CTERM_EXTATTR_AUTOWRAP;
		terminal->last_column_flag &= ~CTERM_LCF_SET;
	}
	return 0;
}

int
st_cterm_reset_terminal(st_cterm_t *terminal)
{
	int status = st_cterm_finish(terminal);

	if (status != 0)
		return status;
	cterm_reset(terminal);
	cterm_render_set_cursor(terminal, terminal->cursor);
	return cterm_render_take_error(terminal);
}

int
cterm_resize_scrollback(st_cterm_t *terminal, uint32_t columns, uint32_t cell_width, uint32_t cell_height,
    struct st_console_cell **result, struct st_pixel_buffer ***pixel_result)
{
	if (terminal->backlines == 0)
		return 0;
	if ((size_t)columns > SIZE_MAX / (size_t)terminal->backlines
	    || (size_t)columns * (size_t)terminal->backlines > SIZE_MAX / sizeof(**result))
		return IO_ERROR_OUT_OF_RANGE;
	struct st_console_cell  *resized = calloc((size_t)columns * terminal->backlines, sizeof(*resized));
	struct st_pixel_buffer **pixels =
	    terminal->backpixels == NULL ? NULL : calloc((size_t)terminal->backlines, sizeof(*pixels));
	if (resized == NULL || (terminal->backpixels != NULL && pixels == NULL)) {
		free(resized);
		free(pixels);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	size_t copy_columns = columns < (uint32_t)terminal->backwidth ? columns : (size_t)terminal->backwidth;
	for (int line = 0; line < terminal->backfilled; ++line) {
		int source = (terminal->backstart + line) % terminal->backlines;
		memcpy(resized + (size_t)line * columns,
		    terminal->scrollback + (size_t)source * terminal->backwidth, copy_columns * sizeof(*resized));
		if (pixels != NULL && terminal->backpixels[source] != NULL) {
			int status = st_history_resize_pixels(&pixels[line], terminal->backpixels[source],
			    (uint32_t)terminal->backwidth, columns, cell_width, cell_height);
			if (status != 0) {
				free(resized);
				st_history_free_pixels(pixels, (size_t)terminal->backlines);
				return status;
			}
		}
	}
	*result       = resized;
	*pixel_result = pixels;
	return 0;
}

void
cterm_resize_parser_geometry(st_cterm_t *terminal, int columns, int rows, bool preserve_cursor)
{
	int      old_columns = terminal->width;
	int      old_rows    = terminal->height;
	uint32_t last_x      = (uint32_t)(terminal->x - 1 + columns - 1);
	uint32_t last_y      = (uint32_t)(terminal->y - 1 + rows - 1);

	cterm_render_refresh_remote_fonts(terminal);

	terminal->width  = columns;
	terminal->height = rows;
	cterm_extend_tabstops(terminal, columns);
	if (terminal->right_margin == old_columns || terminal->right_margin > columns)
		terminal->right_margin = columns;
	if (terminal->left_margin > columns)
		terminal->left_margin = 1;
	if (terminal->bottom_margin == old_rows || terminal->bottom_margin > rows)
		terminal->bottom_margin = rows;
	if (terminal->top_margin > rows)
		terminal->top_margin = 1;
	if (terminal->screen.provisional_x > last_x)
		terminal->screen.provisional_x = last_x;
	if (terminal->screen.provisional_y > last_y)
		terminal->screen.provisional_y = last_y;
	cterm_apply_window(terminal);
	int x = 1, y = 1;
	if (preserve_cursor) {
		x = terminal->xpos > columns ? columns : terminal->xpos;
		y = terminal->ypos > rows ? rows : terminal->ypos;
		cterm_coord_conv_xy(terminal, CTERM_COORD_ABSTERM, CTERM_COORD_TERM, &x, &y);
	}
	cterm_set_position(terminal, x, y);
}

int
cterm_reconfigure_console(struct cterminal *t, const struct st_console_config *config)
{
	if (t == NULL || config == NULL || config->columns == 0 || config->columns > INT_MAX
	    || config->rows > INT_MAX
	    || config->rows <= t->api_reserved_rows + (t->status_display_type >= 1 ? 1u : 0u))
		return IO_ERROR_INVALID_ARGUMENT;
	struct st_console_cell  *history        = NULL;
	struct st_pixel_buffer **history_pixels = NULL;
	int status = t->backlines == 0 ? 0
	                               : cterm_resize_scrollback(t, config->columns, config->cell_width,
					     config->cell_height, &history, &history_pixels);
	if (status == 0)
		status = st_console_reconfigure(t->screen.console, config);
	if (status != 0) {
		free(history);
		st_history_free_pixels(history_pixels, (size_t)t->backlines);
		return status;
	}
	free(t->scrollback);
	st_history_free_pixels(t->backpixels, (size_t)t->backlines);
	t->scrollback = history;
	t->backpixels = history_pixels;
	t->backwidth  = (int)config->columns;
	t->backstart  = 0;
	t->backpos    = t->backfilled == t->backlines ? 0 : t->backfilled;
	cterm_resize_parser_geometry(t, (int)config->columns,
	    (int)(config->rows - t->api_reserved_rows - (t->status_display_type >= 1 ? 1u : 0u)), false);
	if (t->status_sub != NULL) {
		memcpy(t->status_sub->screen.mode_palette, t->screen.mode_palette, sizeof(t->screen.mode_palette));
		cterm_current_palette_colours(t->status_sub, &t->status_sub->fg_color, &t->status_sub->bg_color);
		t->status_sub->y = t->y + t->height;
		cterm_resize_parser_geometry(t->status_sub, (int)config->columns, 1, false);
		cterm_text_forget(t->status_sub);
		st_unicode_presentation_reset(t->status_sub->screen.unicode);
	}
	if (t->api_size_changed != NULL)
		t->api_size_changed(t->api_size_context, config->columns, (uint32_t)t->height, config->pixel_width,
		    config->pixel_height);
	return cterm_render_take_error(t);
}

int
st_cterm_resize(st_cterm_t *terminal, uint32_t columns, uint32_t rows)
{
	struct st_console_cell  *scrollback = NULL;
	struct st_console_cell  *old_scrollback;
	struct st_pixel_buffer **history_pixels = NULL;
	uint32_t                 cell_width, cell_height;
	uint32_t                 status_rows;
	uint32_t                 terminal_rows;
	int                      status;

	if (terminal == NULL || columns == 0 || rows == 0 || columns > INT_MAX || rows > INT_MAX)
		return IO_ERROR_INVALID_ARGUMENT;
	status_rows = terminal->status_display_type >= 1 ? 1 : 0;
	if (rows <= terminal->api_reserved_rows + status_rows)
		return IO_ERROR_OUT_OF_RANGE;
	terminal_rows = rows - terminal->api_reserved_rows - status_rows;
	if (terminal->fixed_geometry
	    && (columns != (uint32_t)terminal->width || terminal_rows != (uint32_t)terminal->height))
		return IO_ERROR_NOT_SUPPORTED;
	/* Geometry commits the old placement, but is not end-of-input for
	 * the UTF-8 decoder or the RIP command parser. */
	status = cterm_text_commit(terminal);
	if (status == 0 && terminal->status_sub != NULL)
		status = cterm_text_commit(terminal->status_sub);
	input_cleanup(terminal);
	if (status == 0)
		status = cterm_render_take_error(terminal);
	if (status == 0 && terminal->status_sub != NULL)
		status = cterm_render_take_error(terminal->status_sub);
	if (status != 0)
		return status;
	status = st_console_cell_pixel_size(terminal->screen.console, &cell_width, &cell_height);
	if (status != 0)
		return status;
	if (terminal->backlines != 0) {
		status = cterm_resize_scrollback(terminal, columns, cell_width, cell_height, &scrollback,
		    &history_pixels);
		if (status != 0)
			return status;
	}
	status = st_console_resize(terminal->screen.console, columns, rows);
	if (status != 0) {
		free(scrollback);
		st_history_free_pixels(history_pixels, (size_t)terminal->backlines);
		return status;
	}
	if (columns != (uint32_t)terminal->width || terminal_rows != (uint32_t)terminal->height) {
		cterm_dec_sixel_cancel(terminal);
		if (terminal->status_sub != NULL)
			cterm_dec_sixel_cancel(terminal->status_sub);
	}
	old_scrollback       = terminal->scrollback;
	terminal->scrollback = scrollback;
	st_history_free_pixels(terminal->backpixels, (size_t)terminal->backlines);
	terminal->backpixels = history_pixels;
	terminal->backwidth  = (int)columns;
	terminal->backstart  = 0;
	terminal->backpos    = terminal->backfilled == terminal->backlines ? 0 : terminal->backfilled;
	free(old_scrollback);
	/* The active parser must leave its window and cursor on the shared console. */
	struct cterminal *sub = terminal->status_sub;
	if (sub != NULL) {
		sub->x = terminal->x;
		sub->y = terminal->y + (int)terminal_rows;
		if (terminal->status_display_active == 0)
			cterm_resize_parser_geometry(sub, (int)columns, 1, true);
	}
	cterm_resize_parser_geometry(terminal, (int)columns, (int)terminal_rows, true);
	if (sub != NULL && terminal->status_display_active == 1)
		cterm_resize_parser_geometry(sub, (int)columns, 1, true);
	status = cterm_render_take_error(terminal);
	if (status == 0 && terminal->status_sub != NULL)
		status = cterm_render_take_error(terminal->status_sub);
	return status;
}

int
st_cterm_size(const st_cterm_t *terminal, uint32_t *columns, uint32_t *rows)
{
	if (terminal == NULL || columns == NULL || rows == NULL || terminal->width < 1 || terminal->height < 1)
		return IO_ERROR_INVALID_ARGUMENT;
	*columns = (uint32_t)terminal->width;
	*rows    = (uint32_t)terminal->height;
	return 0;
}

int
st_cterm_origin(const st_cterm_t *terminal, uint32_t *x, uint32_t *y)
{
	if (terminal == NULL || x == NULL || y == NULL || terminal->x < 1 || terminal->y < 1)
		return IO_ERROR_INVALID_ARGUMENT;
	*x = (uint32_t)terminal->x;
	*y = (uint32_t)terminal->y;
	return 0;
}

bool
st_cterm_play_fx_u8(st_cterm_t *terminal, const uint8_t *samples, size_t size)
{
	return cterm_play_fx_u8(terminal, samples, size);
}

int
st_cterm_state(const st_cterm_t *terminal, struct st_cterm_state *state)
{
	if (terminal == NULL || state == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	*state = (struct st_cterm_state) {
	    .x                    = terminal->xpos,
	    .y                    = terminal->ypos,
	    .top_margin           = terminal->top_margin,
	    .bottom_margin        = terminal->bottom_margin,
	    .left_margin          = terminal->left_margin,
	    .right_margin         = terminal->right_margin,
	    .attr                 = terminal->attr,
	    .fg_color             = terminal->fg_color & 0xFFFFFFu,
	    .bg_color             = terminal->bg_color & 0xFFFFFFu,
	    .has_palette_override = terminal->has_palette_override,
	    .font_slot            = terminal->font_slot,
	    .rip3_g0              = terminal->g0_designation,
	    .rip3_g1              = terminal->g1_designation,
	    .scrollback_lines     = terminal->backlines,
	    .scrollback_width     = terminal->backwidth,
	    .scrollback_pos       = terminal->backpos,
	    .scrollback_start     = terminal->backstart,
	    .emulation            = (enum st_cterm_emulation)terminal->emulation,
	    .started              = terminal->started != 0,
	    .skypix               = terminal->graphics_mode == ST_CTERM_GRAPHICS_SKYPIX_13
	                            || terminal->graphics_mode == ST_CTERM_GRAPHICS_SKYPIX_31,
	    .graphics_mode        = terminal->graphics_mode,
	    .ext_attr =
		{
			   .auto_wrap             = (terminal->extattr & CTERM_EXTATTR_AUTOWRAP) != 0,
			   .origin_mode           = (terminal->extattr & CTERM_EXTATTR_ORIGINMODE) != 0,
			   .sx_scroll             = (terminal->extattr & CTERM_EXTATTR_SXSCROLL) != 0,
			   .dec_lrmm              = (terminal->extattr & CTERM_EXTATTR_DECLRMM) != 0,
			   .bracket_paste         = (terminal->extattr & CTERM_EXTATTR_BRACKETPASTE) != 0,
			   .dec_bkm               = (terminal->extattr & CTERM_EXTATTR_DECBKM) != 0,
			   .prestel_mosaic        = (terminal->extattr & CTERM_EXTATTR_PRESTEL_MOSAIC) != 0,
			   .prestel_double_height = (terminal->extattr & CTERM_EXTATTR_PRESTEL_DOUBLE_HEIGHT) != 0,
			   .prestel_conceal       = (terminal->extattr & CTERM_EXTATTR_PRESTEL_CONCEAL) != 0,
			   .prestel_separated     = (terminal->extattr & CTERM_EXTATTR_PRESTEL_SEPARATED) != 0,
			   .prestel_hold          = (terminal->extattr & CTERM_EXTATTR_PRESTEL_HOLD) != 0,
			   .alternate_keypad      = (terminal->extattr & CTERM_EXTATTR_ALTERNATE_KEYPAD) != 0,
			   },
	    .last_column_flag =
		{
			   .set     = (terminal->last_column_flag & CTERM_LCF_SET) != 0,
			   .enabled = (terminal->last_column_flag & CTERM_LCF_ENABLED) != 0,
			   .forced  = (terminal->last_column_flag & CTERM_LCF_FORCED) != 0,
			   },
	};
	for (size_t i = 0; i < ST_CTERM_FONT_COUNT; ++i)
		state->alt_fonts[i] = terminal->screen.selectors[i + 1];
	if (terminal->has_palette_override) {
		for (size_t i = 0; i < 16; ++i)
			state->palette_override[i] = terminal->palette_override[i] & 0xFFFFFFu;
	}
	return 0;
}

int
st_cterm_set_fonts(st_cterm_t *terminal, uint8_t selector, unsigned mask)
{
	if (terminal == NULL || mask == 0 || (mask & ~ST_CTERM_FONT_ALL) != 0)
		return IO_ERROR_INVALID_ARGUMENT;
	if (!terminal->screen.font_valid[selector])
		return IO_ERROR_NOT_FOUND;
	int status = st_cterm_finish(terminal);
	if (status != 0)
		return status;
	for (size_t i = 0; i < ST_CTERM_FONT_COUNT; ++i) {
		if ((mask & (1U << i)) == 0)
			continue;
		terminal->screen.selectors[i + 1] = selector;
	}
	return 0;
}

bool
st_cterm_font_info(const st_cterm_t *terminal, uint8_t selector, uint16_t *identifier, enum st_codepage *codepage)
{
	if (terminal == NULL || identifier == NULL || codepage == NULL || !terminal->screen.font_valid[selector])
		return false;
	*identifier = terminal->screen.font_identifiers[selector];
	*codepage   = terminal->screen.font_codepages[selector];
	return true;
}

int
st_cterm_register_font(st_cterm_t *terminal, uint8_t selector, const char *path, enum st_codepage codepage,
    uint16_t *identifier, struct st_font_load_error *error)
{
	uint16_t registered;
	int      status;

	if (error != NULL)
		*error = (struct st_font_load_error) {0};
	if (terminal == NULL || path == NULL || *path == '\0' || identifier == NULL
	    || selector < ST_FONT_CATALOG_FIRST_CUSTOM)
		return IO_ERROR_INVALID_ARGUMENT;
	if (terminal->screen.font_valid[selector] || terminal->screen.remote_fonts[selector].present != 0)
		return IO_ERROR_ALREADY_EXISTS;
	status =
	    st_font_catalog_register_custom_codepage(terminal->screen.console, path, codepage, &registered, error);
	if (status != 0)
		return status;
	terminal->screen.font_identifiers[selector] = registered;
	terminal->screen.font_codepages[selector]   = codepage;
	terminal->screen.font_valid[selector]       = true;
	*identifier                                 = registered;
	return 0;
}

static void
copy_sauce_field(uint8_t *destination, size_t width, const char *text)
{
	size_t length = text == NULL ? 0 : strlen(text);

	memset(destination, ' ', width);
	if (length > width)
		length = width;
	if (length != 0)
		memcpy(destination, text, length);
}

int
st_cterm_save_binary(st_cterm_t *terminal, FILE *stream, bool with_sauce, const char *title, const char *author,
    int *native_error)
{
	struct st_console_cell *cells;
	uint8_t                *encoded;
	struct st_console_rect  row;
	size_t                  columns;
	size_t                  rows;
	int                     status = 0;

	if (terminal == NULL || stream == NULL || native_error == NULL || terminal->width < 1
	    || terminal->height < 1 || terminal->x < 1 || terminal->y < 1)
		return IO_ERROR_INVALID_ARGUMENT;
	*native_error = 0;
	columns       = (size_t)terminal->width;
	rows          = (size_t)terminal->height;
	if (columns > SIZE_MAX / 2 || columns > SIZE_MAX / sizeof(*cells)
	    || (with_sauce && (columns > UINT8_MAX * 2U || rows > UINT32_MAX / (columns * 2U))))
		return IO_ERROR_OUT_OF_RANGE;
	cells   = malloc(columns * sizeof(*cells));
	encoded = malloc(columns * 2U);
	if (cells == NULL || encoded == NULL) {
		free(cells);
		free(encoded);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	row = (struct st_console_rect) {
	    .x      = (uint32_t)(terminal->x - 1),
	    .y      = (uint32_t)(terminal->y - 1),
	    .width  = (uint32_t)columns,
	    .height = 1,
	};
	for (size_t y = 0; y < rows; ++y) {
		row.y  = (uint32_t)(terminal->y - 1) + (uint32_t)y;
		status = st_console_read_cells(terminal->screen.console, &row, cells, columns);
		if (status != 0)
			break;
		for (size_t x = 0; x < columns; ++x) {
			encoded[x * 2U]      = cterm_render_cell_byte(terminal, &cells[x]);
			encoded[x * 2U + 1U] = cterm_render_cell_attribute(terminal, &cells[x]);
		}
		if (fwrite(encoded, 1, columns * 2U, stream) != columns * 2U) {
			*native_error = errno == 0 ? EIO : errno;
			status        = IO_ERROR_IO;
			break;
		}
	}
	free(cells);
	free(encoded);
	if (status != 0 || !with_sauce)
		return status;
	{
		uint8_t    sauce[129] = {0};
		char       date[9]    = {0};
		time_t     now        = time(NULL);
		struct tm *local      = localtime(&now);
		long       length     = ftell(stream);
		uint32_t   file_size;

		if (length < 0 || (unsigned long)length > UINT32_MAX) {
			*native_error = errno == 0 ? ERANGE : errno;
			return IO_ERROR_OUT_OF_RANGE;
		}
		file_size = (uint32_t)length;
		sauce[0]  = 0x1a;
		memcpy(sauce + 1, "SAUCE00", 7);
		copy_sauce_field(sauce + 8, 35, title);
		copy_sauce_field(sauce + 43, 20, author);
		copy_sauce_field(sauce + 63, 20, NULL);
		if (local != NULL && strftime(date, sizeof(date), "%Y%m%d", local) == 8)
			memcpy(sauce + 83, date, 8);
		sauce[91] = (uint8_t)file_size;
		sauce[92] = (uint8_t)(file_size >> 8);
		sauce[93] = (uint8_t)(file_size >> 16);
		sauce[94] = (uint8_t)(file_size >> 24);
		sauce[95] = 5; /* BinaryText SAUCE data type. */
		sauce[96] = (uint8_t)(columns / 2U);
		if ((cterm_render_get_video_flags(terminal)
		        & (ST_CONSOLE_VIDEO_BRIGHT_BACKGROUND | ST_CONSOLE_VIDEO_NO_BLINK))
		    != 0)
			sauce[106] = 1;
		if (fwrite(sauce, 1, sizeof(sauce), stream) != sizeof(sauce)) {
			*native_error = errno == 0 ? EIO : errno;
			return IO_ERROR_IO;
		}
	}
	return 0;
}

enum st_cterm_encoding
st_cterm_encoding(const st_cterm_t *terminal)
{
	return terminal == NULL ? ST_CTERM_ENCODING_CODEPAGE : terminal->wire_encoding;
}

enum st_codepage
st_cterm_codepage(const st_cterm_t *terminal)
{
	return terminal == NULL ? ST_CODEPAGE_CP437 : terminal->screen.codepage;
}

int
st_cterm_status_display(const st_cterm_t *terminal)
{
	return terminal == NULL ? 0 : terminal->status_display_type;
}

bool
st_cterm_doorway_mode(const st_cterm_t *terminal)
{
	return terminal != NULL && terminal->doorway_mode != 0;
}

void
st_cterm_set_doorway_mode(st_cterm_t *terminal, bool enabled)
{
	if (terminal != NULL)
		terminal->doorway_mode = enabled ? 1 : 0;
}

int
st_cterm_music_mode(const st_cterm_t *terminal)
{
	return terminal == NULL ? CTERM_MUSIC_SYNCTERM : terminal->music_enable;
}

void
st_cterm_set_music_mode(st_cterm_t *terminal, int mode)
{
	if (terminal == NULL)
		return;
	if (mode < CTERM_MUSIC_SYNCTERM)
		mode = CTERM_MUSIC_SYNCTERM;
	if (mode > CTERM_MUSIC_ENABLED)
		mode = CTERM_MUSIC_ENABLED;
	terminal->music_enable = (cterm_music_t)mode;
}

void
st_cterm_set_atari_st_key_clicks(st_cterm_t *terminal, bool enabled)
{
	if (terminal != NULL)
		terminal->atari_st_key_clicks = enabled;
}

bool
st_cterm_atascii_inverse(const st_cterm_t *terminal)
{
	return terminal != NULL && terminal->atascii_inverse;
}

int
st_cterm_mouse_mode(const st_cterm_t *terminal)
{
	uint16_t modes;

	if (terminal == NULL)
		return 0;
	if (terminal->device.mouse_mode != NULL) {
		int mode = terminal->device.mouse_mode(terminal->device.context);
		if (mode != 0 || terminal->device.owns_mouse)
			return mode;
	}
	modes = terminal->api_mouse_modes;
	if ((modes & MOUSE_ANY) != 0)
		return 1003;
	if ((modes & MOUSE_BUTTON) != 0)
		return 1002;
	if ((modes & MOUSE_NORMAL) != 0)
		return 1000;
	if ((modes & MOUSE_X10) != 0)
		return 9;
	return 0;
}

bool
st_cterm_mouse_sgr_pixels(const st_cterm_t *terminal)
{
	return terminal != NULL && !terminal->device.owns_mouse
	       && (terminal->device.mouse_passthrough || terminal->device.mouse_mode == NULL
	           || terminal->device.mouse_mode(terminal->device.context) == 0)
	       && (terminal->api_mouse_modes & MOUSE_SGR_PIXELS) != 0;
}

bool
st_cterm_bracketed_paste(const st_cterm_t *terminal)
{
	return terminal != NULL && (terminal->extattr & CTERM_EXTATTR_BRACKETPASTE) != 0;
}

int
st_cterm_set_encoding(st_cterm_t *terminal, enum st_cterm_encoding encoding, enum st_codepage codepage)
{
	int status;

	if (terminal == NULL || encoding < ST_CTERM_ENCODING_UTF8 || encoding > ST_CTERM_ENCODING_CODEPAGE
	    || codepage >= ST_CODEPAGE_COUNT
	    || (encoding == ST_CTERM_ENCODING_UTF8
	        && !utf8_supported((enum st_cterm_emulation)terminal->emulation)))
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_cterm_finish(terminal);
	if (status != 0)
		return status;
	if (encoding == ST_CTERM_ENCODING_UTF8 && terminal->screen.unicode == NULL) {
		status = cterm_text_create(terminal);
		if (status != 0)
			return status;
	}
	terminal->wire_encoding   = encoding;
	terminal->screen.codepage = codepage;
	if (terminal->emulation == CTERM_EMULATION_PETASCII)
		terminal->petscii_connection_codepage = codepage;
	if (terminal->status_sub != NULL) {
		terminal->status_sub->screen.codepage = codepage;
		terminal->status_sub->wire_encoding   = encoding;
	}
	if (terminal->device.encoding != NULL)
		terminal->device.encoding(terminal->device.context, encoding, codepage);
	return 0;
}

int
st_cterm_copy_history(st_cterm_t *terminal, struct st_console_cell **cells, uint32_t *columns, size_t *rows)
{
	struct st_console_cell *copy;
	struct st_console_rect  visible;
	size_t                  line_count;
	size_t                  cell_count;
	int                     status;

	if (terminal == NULL || cells == NULL || columns == NULL || rows == NULL || terminal->width <= 0
	    || terminal->height <= 0 || terminal->backfilled < 0)
		return IO_ERROR_INVALID_ARGUMENT;
	size_t history_rows = cterm_history_rows(terminal);
	line_count          = history_rows + (size_t)terminal->height;
	if ((size_t)terminal->width > SIZE_MAX / line_count
	    || (size_t)terminal->width * line_count > SIZE_MAX / sizeof(*copy))
		return IO_ERROR_OUT_OF_RANGE;
	cell_count = (size_t)terminal->width * line_count;
	copy       = malloc(cell_count * sizeof(*copy));
	if (copy == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	for (size_t line = 0; line < history_rows; ++line)
		cterm_history_copy_cells(terminal, line, copy + line * terminal->width, (size_t)terminal->width);
	visible = (struct st_console_rect) {
	    .x      = (uint32_t)(terminal->x - 1),
	    .y      = (uint32_t)(terminal->y - 1),
	    .width  = (uint32_t)terminal->width,
	    .height = (uint32_t)terminal->height,
	};
	status = st_console_read_cells(terminal->screen.console, &visible, copy + history_rows * terminal->width,
	    (size_t)terminal->width);
	if (status != 0) {
		free(copy);
		return status;
	}
	*cells   = copy;
	*columns = (uint32_t)terminal->width;
	*rows    = line_count;
	return 0;
}

int
st_cterm_copy_history_pixels(st_cterm_t *terminal, size_t row, struct st_pixel_buffer **result)
{
	if (terminal == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	size_t history_rows = cterm_history_rows(terminal);
	if (row >= history_rows + (size_t)terminal->height)
		return IO_ERROR_OUT_OF_RANGE;
	if (row < history_rows)
		return cterm_history_copy_pixels(terminal, row, result);
	struct st_console_cell *cells = malloc((size_t)terminal->width * sizeof(*cells));
	if (cells == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	uint32_t               cw, ch;
	int                    status = st_console_cell_pixel_size(terminal->screen.console, &cw, &ch);
	struct st_console_rect region = {(uint32_t)(terminal->x - 1),
	    (uint32_t)(terminal->y - 1) + (uint32_t)(row - history_rows), (uint32_t)terminal->width, 1};
	if (status == 0)
		status = st_console_read_cells(terminal->screen.console, &region, cells, region.width);
	if (status == 0) {
		region.x *= cw;
		region.y *= ch;
		region.width *= cw;
		region.height = ch;
		status        = st_history_capture_pixels(result, terminal->screen.console, cells,
		    (uint32_t)terminal->width, &region);
	}
	free(cells);
	return status;
}

int
st_cterm_scrollback_size(const st_cterm_t *terminal, uint32_t *columns, size_t *rows)
{
	if (terminal == NULL || columns == NULL || rows == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	*columns = terminal->scrollback != NULL && terminal->backwidth > 0 ? (uint32_t)terminal->backwidth : 0;
	*rows    = terminal->scrollback != NULL ? cterm_history_rows(terminal) : 0;
	return 0;
}

int
st_cterm_copy_scrollback_rows(const st_cterm_t *terminal, struct st_console_cell *destination,
    uint32_t destination_columns, size_t destination_rows, size_t source_row, size_t row_count,
    size_t destination_row)
{
	size_t columns;
	size_t available;

	if (terminal == NULL || destination == NULL
	    || (destination_rows != 0 && destination_columns > SIZE_MAX / destination_rows)
	    || terminal->backfilled < 0 || terminal->backlines < 0 || terminal->backwidth < 0)
		return IO_ERROR_INVALID_ARGUMENT;
	if (terminal->scrollback == NULL || terminal->backlines == 0 || terminal->backwidth == 0
	    || source_row >= cterm_history_rows(terminal) || destination_row >= destination_rows || row_count == 0)
		return 0;
	available = cterm_history_rows(terminal) - source_row;
	if (row_count > available)
		row_count = available;
	available = destination_rows - destination_row;
	if (row_count > available)
		row_count = available;
	columns =
	    (size_t)terminal->backwidth < destination_columns ? (size_t)terminal->backwidth : destination_columns;
	if (columns == 0)
		return 0;
	for (size_t row = 0; row < row_count; ++row) {
		cterm_history_copy_cells(terminal, source_row + row,
		    destination + (destination_row + row) * destination_columns, columns);
	}
	return (int)row_count;
}

int
cterm_mapped_key(uint16_t key, uint32_t modifiers)
{
	bool shift   = (modifiers & ST_CONSOLE_MODIFIER_SHIFT) != 0;
	bool control = (modifiers & ST_CONSOLE_MODIFIER_CONTROL) != 0;
	bool alt     = (modifiers & (ST_CONSOLE_MODIFIER_ALT | ST_CONSOLE_MODIFIER_GUI)) != 0;

	if (key >= ST_CONSOLE_KEY_F1 && key <= ST_CONSOLE_KEY_F12) {
		int number = key - ST_CONSOLE_KEY_F1 + 1;

		return alt       ? CTERM_KEY_DOS_ALT_F(number)
		       : control ? CTERM_KEY_DOS_CTRL_F(number)
		       : shift   ? CTERM_KEY_DOS_SHIFT_F(number)
		                 : CTERM_KEY_DOS_F(number);
	}
	/* Other Alt combinations are host shortcuts or have no reference key. */
	if (alt && key != ST_CONSOLE_KEY_UP && key != ST_CONSOLE_KEY_DOWN && key != ST_CONSOLE_KEY_INSERT
	    && key != ST_CONSOLE_KEY_DELETE)
		return 0xffff;
	switch (key) {
		case ST_CONSOLE_KEY_UP:
			return alt       ? 0x9800
			       : control ? CTERM_KEY_DOS_CTRL_UP
			       : shift   ? CTERM_KEY_DOS_SHIFT_UP
			                 : CTERM_KEY_DOS_UP;
		case ST_CONSOLE_KEY_DOWN:
			return alt       ? 0xa000
			       : control ? CTERM_KEY_DOS_CTRL_DOWN
			       : shift   ? CTERM_KEY_DOS_SHIFT_DOWN
			                 : CTERM_KEY_DOS_DOWN;
		case ST_CONSOLE_KEY_LEFT:
			return control ? CTERM_KEY_DOS_CTRL_LEFT
			       : shift ? CTERM_KEY_DOS_SHIFT_LEFT
			               : CTERM_KEY_DOS_LEFT;
		case ST_CONSOLE_KEY_RIGHT:
			return control ? CTERM_KEY_DOS_CTRL_RIGHT
			       : shift ? CTERM_KEY_DOS_SHIFT_RIGHT
			               : CTERM_KEY_DOS_RIGHT;
		case ST_CONSOLE_KEY_INSERT:
			return alt ? 0xa200 : control ? 0x0400 : shift ? 0x0500 : CTERM_KEY_DOS_IC;
		case ST_CONSOLE_KEY_DELETE:
			return alt ? 0xa300 : control ? 0x0600 : shift ? 0x0700 : CTERM_KEY_DOS_DC;
		case ST_CONSOLE_KEY_HOME:
			return control ? 0x7700 : shift ? 0x3700 : CTERM_KEY_DOS_HOME;
		case ST_CONSOLE_KEY_END:
			return control ? CTERM_KEY_DOS_CTRL_END
			       : shift ? CTERM_KEY_DOS_SHIFT_END
			               : CTERM_KEY_DOS_END;
		case ST_CONSOLE_KEY_PAGE_UP:
			return control ? 0x8400 : shift ? 0x3900 : CTERM_KEY_DOS_PPAGE;
		case ST_CONSOLE_KEY_PAGE_DOWN:
			return control ? 0x7600 : shift ? 0x3300 : CTERM_KEY_DOS_NPAGE;
		case '\t':
			return control ? 0xffff : shift ? CTERM_KEY_DOS_BACKTAB : key;
		case '\r':
			return control ? '\n' : key;
		case '\b':
			return control ? 0x7f : key;
		default:
			return key;
	}
}

static enum st_codepage
keyboard_codepage(const st_cterm_t *terminal)
{
	return terminal->emulation == CTERM_EMULATION_PETASCII ? terminal->petscii_connection_codepage
	                                                      : terminal->screen.codepage;
}

bool
st_cterm_handle_key(st_cterm_t *terminal, uint16_t key_code, uint32_t modifiers)
{
	int                      doorway_key;

	if (terminal == NULL)
		return false;
	doorway_key = cterm_mapped_key(key_code, modifiers);
	if (doorway_key >= 0 && doorway_key < 32
	    && (modifiers & (ST_CONSOLE_MODIFIER_CONTROL | ST_CONSOLE_MODIFIER_ALT | ST_CONSOLE_MODIFIER_GUI)) == 0
	    && terminal->wire_encoding == ST_CTERM_ENCODING_CODEPAGE) {
		uint8_t byte;

		/* Ordinary control keys arrive as Unicode C0 values, whereas the
		 * reference byte-key tables expect e.g. ATASCII Return as 0x9b.
		 * Explicit Ctrl combinations retain their control-key identity. */
		if (st_codepage_from_unicode(&byte, keyboard_codepage(terminal), (uint32_t)doorway_key))
			doorway_key = byte;
	}
	return cterm_handle_key(terminal, doorway_key) == CTERM_KEY_HANDLED;
}

bool
st_cterm_handle_codepage_key(st_cterm_t *terminal, uint8_t byte)
{
	if (terminal == NULL)
		return false;
	(void)cterm_handle_key(terminal, byte);
	return true;
}

bool
st_cterm_handle_text_key(st_cterm_t *terminal, uint32_t codepoint)
{
	uint8_t byte;

	if (terminal == NULL)
		return false;
	if (terminal->wire_encoding == ST_CTERM_ENCODING_UTF8) {
		if (codepoint >= 0x80)
			return false;
		byte = (uint8_t)codepoint;
	}
	else if (!st_codepage_from_unicode(&byte, keyboard_codepage(terminal), codepoint))
		return false;
	/* An encodable byte rejected by this emulation is deliberately silent,
	 * not an invitation to bypass its keyboard policy with a raw send. */
	(void)cterm_handle_key(terminal, byte);
	return true;
}

bool
st_cterm_handle_doorway_key(st_cterm_t *terminal, uint8_t scan_code)
{
	return terminal != NULL && cterm_handle_key(terminal, (int)scan_code << 8) == CTERM_KEY_HANDLED;
}

bool
st_cterm_handle_physical_keys(st_cterm_t *terminal, const struct st_console_event *events, size_t count)
{
	if (terminal == NULL || (events == NULL && count != 0))
		return false;
	for (size_t i = 0; i < count; ++i) {
		if (events[i].type != ST_CONSOLE_EVENT_PHYSICAL_KEY)
			return false;
	}
	return cterm_pk_events(terminal, events, count);
}

static int
mouse_button_code(uint32_t button)
{
	if (button == ST_CONSOLE_MOUSE_BUTTON_LEFT)
		return 0;
	if (button == ST_CONSOLE_MOUSE_BUTTON_MIDDLE)
		return 1;
	if (button == ST_CONSOLE_MOUSE_BUTTON_RIGHT)
		return 2;
	if (button == ST_CONSOLE_MOUSE_BUTTON_X1)
		return 128;
	if (button == ST_CONSOLE_MOUSE_BUTTON_X2)
		return 129;
	return -1;
}

static size_t
append_mouse_utf8(char *buffer, size_t offset, uint32_t value)
{
	ptrdiff_t count = st_utf8_encode_one(buffer + offset, 16 - offset, value);

	return count < 0 ? 0 : offset + (size_t)count;
}

bool
st_cterm_handle_mouse(st_cterm_t *terminal, const struct st_console_event *event)
{
	if (terminal == NULL || event == NULL || event->type != ST_CONSOLE_EVENT_MOUSE)
		return false;
	uint32_t changed = terminal->api_mouse_buttons ^ event->data.mouse.buttons;
	terminal->api_mouse_buttons = event->data.mouse.buttons;
	return cterm_report_mouse(terminal, event, changed);
}

bool
cterm_report_mouse(st_cterm_t *terminal, const struct st_console_event *event, uint32_t changed)
{
	uint16_t modes = terminal->api_mouse_modes;
	uint32_t button = 0;
	uint32_t x;
	uint32_t y;
	int      code;
	bool     release = false;
	bool     report;
	char     sequence[96];
	int      length;

	if (event->data.mouse.action == ST_CONSOLE_MOUSE_WHEEL) {
		if ((modes & (MOUSE_X10 | MOUSE_NORMAL | MOUSE_BUTTON | MOUSE_ANY)) == 0
		    && (modes & MOUSE_ALT_SCROLL) != 0) {
			int key = event->data.mouse.wheel_y > 0 ? CTERM_KEY_DOS_UP : CTERM_KEY_DOS_DOWN;
			return cterm_handle_key(terminal, key) == CTERM_KEY_HANDLED;
		}
		code = event->data.mouse.wheel_y > 0 ? 64 : 65;
	}
	else if (event->data.mouse.action == ST_CONSOLE_MOUSE_MOVE) {
		report =
		    (modes & MOUSE_ANY) != 0 || ((modes & MOUSE_BUTTON) != 0 && event->data.mouse.buttons != 0);
		if (!report)
			return false;
		button = event->data.mouse.buttons & (~event->data.mouse.buttons + 1U);
		code   = mouse_button_code(button);
		if (code < 0)
			code = 3;
		code += 32;
	}
	else {
		if (changed == 0)
			return false;
		button = changed & (~changed + 1U);
		code   = mouse_button_code(button);
		if (code < 0)
			return false;
		release = (event->data.mouse.buttons & button) == 0;
		if (release && (modes & MOUSE_SGR) == 0)
			code = 3;
		if ((modes & MOUSE_X10) != 0 && release)
			return false;
	}
	if ((modes & (MOUSE_X10 | MOUSE_NORMAL | MOUSE_BUTTON | MOUSE_ANY)) == 0)
		return false;
	if ((event->data.mouse.modifiers & ST_CONSOLE_MODIFIER_SHIFT) != 0)
		code += 4;
	if ((event->data.mouse.modifiers & ST_CONSOLE_MODIFIER_ALT) != 0)
		code += 8;
	if ((event->data.mouse.modifiers & ST_CONSOLE_MODIFIER_CONTROL) != 0)
		code += 16;
	if ((modes & MOUSE_SGR_PIXELS) != 0 && event->data.mouse.pixel_x >= 0 && event->data.mouse.pixel_y >= 0) {
		x = (uint32_t)event->data.mouse.pixel_x + 1;
		y = (uint32_t)event->data.mouse.pixel_y + 1;
	}
	else {
		x = event->data.mouse.x < 0 ? 1U : (uint32_t)event->data.mouse.x + 1;
		y = event->data.mouse.y < 0 ? 1U : (uint32_t)event->data.mouse.y + 1;
	}
	if ((modes & (MOUSE_SGR | MOUSE_SGR_PIXELS)) != 0)
		length = snprintf(sequence, sizeof(sequence), "\x1b[<%d;%u;%u%c", code, x, y, release ? 'm' : 'M');
	else if ((modes & MOUSE_URXVT) != 0)
		length = snprintf(sequence, sizeof(sequence), "\x1b[%d;%u;%uM", code + 32, x, y);
	else if ((modes & MOUSE_UTF8) != 0) {
		size_t offset = 3;

		memcpy(sequence, "\x1b[M", 3);
		offset = append_mouse_utf8(sequence, offset, (uint32_t)code + 32);
		if (offset != 0)
			offset = append_mouse_utf8(sequence, offset, x + 32);
		if (offset != 0)
			offset = append_mouse_utf8(sequence, offset, y + 32);
		length = (int)offset;
	}
	else {
		if (code + 32 > UINT8_MAX || x + 32 > UINT8_MAX || y + 32 > UINT8_MAX)
			return false;
		sequence[0] = '\x1b';
		sequence[1] = '[';
		sequence[2] = 'M';
		sequence[3] = (char)(code + 32);
		sequence[4] = (char)(x + 32);
		sequence[5] = (char)(y + 32);
		length      = 6;
	}
	if (length <= 0 || (size_t)length >= sizeof(sequence))
		return false;
	emit_user(sequence, (size_t)length, terminal);
	return true;
}

void
st_cterm_sync_mouse_buttons(st_cterm_t *terminal, uint32_t buttons)
{
	if (terminal != NULL)
		terminal->api_mouse_buttons = buttons;
}

bool
st_cterm_handle_focus(st_cterm_t *terminal, bool focused)
{
	static const char gained[] = "\x1b[I";
	static const char lost[]   = "\x1b[O";

	if (terminal == NULL || (terminal->api_mouse_modes & MOUSE_FOCUS) == 0)
		return false;
	emit_user(focused ? gained : lost, 3, terminal);
	return true;
}

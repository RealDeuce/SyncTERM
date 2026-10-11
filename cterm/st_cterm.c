/* Incremental terminal-emulation parser and semantic console renderer. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_cterm.h"

#include "cterm.h"
#include "cterm_internal.h"
#include "io_error.h"
#include "st_utf8.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
emit_response(const char *bytes, size_t size, void *context)
{
	st_cterm_t *terminal = context;

	if (terminal->api_output != NULL)
		terminal->api_output(terminal->api_output_context,
		    ST_CTERM_OUTPUT_RESPONSE, (const uint8_t *)bytes, size);
}

static void
emit_user(const char *bytes, size_t size, void *context)
{
	st_cterm_t *terminal = context;

	if (terminal->api_output != NULL)
		terminal->api_output(terminal->api_output_context,
		    ST_CTERM_OUTPUT_USER, (const uint8_t *)bytes, size);
}

static void
emit_apc(char *text, size_t size, void *context)
{
	st_cterm_t *terminal = context;

	if (terminal->api_apc != NULL)
		terminal->api_apc(terminal->api_apc_context, text, size);
}

static void
emit_size(struct cterminal *terminal, int columns, int rows,
	int pixel_width, int pixel_height, void *context)
{
	st_cterm_t *owner = context;
	uint32_t cell_width;
	uint32_t cell_height;

	if ((pixel_width < 0 || pixel_height < 0)
	    && st_console_cell_pixel_size(terminal->screen.console, &cell_width,
	    &cell_height) == 0 && cell_width != 0 && cell_height != 0
	    && columns >= 0 && rows >= 0
	    && (uint32_t)columns <= UINT32_MAX / cell_width
	    && (uint32_t)rows <= UINT32_MAX / cell_height) {
		pixel_width = columns * (int)cell_width;
		pixel_height = rows * (int)cell_height;
	}

	if (owner->api_size_changed != NULL)
		owner->api_size_changed(owner->api_size_context,
		    (uint32_t)columns, (uint32_t)rows,
		    pixel_width < 0 ? 0 : (uint32_t)pixel_width,
		    pixel_height < 0 ? 0 : (uint32_t)pixel_height);
}

enum {
	MOUSE_X10 = UINT16_C(1) << 0,
	MOUSE_NORMAL = UINT16_C(1) << 1,
	MOUSE_BUTTON = UINT16_C(1) << 2,
	MOUSE_ANY = UINT16_C(1) << 3,
	MOUSE_FOCUS = UINT16_C(1) << 4,
	MOUSE_UTF8 = UINT16_C(1) << 5,
	MOUSE_SGR = UINT16_C(1) << 6,
	MOUSE_ALT_SCROLL = UINT16_C(1) << 7,
	MOUSE_URXVT = UINT16_C(1) << 8,
	MOUSE_SGR_PIXELS = UINT16_C(1) << 9,
};

static uint16_t
mouse_mode_bit(int parameter)
{
	switch (parameter) {
	case 9: return MOUSE_X10;
	case 1000: return MOUSE_NORMAL;
	case 1002: return MOUSE_BUTTON;
	case 1003: return MOUSE_ANY;
	case 1004: return MOUSE_FOCUS;
	case 1005: return MOUSE_UTF8;
	case 1006: return MOUSE_SGR;
	case 1007: return MOUSE_ALT_SCROLL;
	case 1015: return MOUSE_URXVT;
	case 1016: return MOUSE_SGR_PIXELS;
	default: return 0;
	}
}

static void
mouse_mode_changed(int parameter, int enabled, void *context)
{
	st_cterm_t *terminal = context;
	uint16_t bit = mouse_mode_bit(parameter);

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

static int
present_unicode(void *context,
	const struct st_unicode_presentation_event *event)
{
	st_cterm_t *terminal = context;
	st_cterm_t *target = terminal->status_sub != NULL
	    && terminal->status_display_active == 1
	    ? terminal->status_sub : terminal;
	st_console_t *console = target->screen.console;
	int x;
	int y;
	int status;

	switch (event->kind) {
	case ST_UNICODE_PRESENTATION_BEGIN:
		cterm_apply_window(target);
		x = target->xpos;
		y = target->ypos;
		cterm_coord_conv_xy(target, CTERM_COORD_ABSTERM,
		    CTERM_COORD_CURR, &x, &y);
		cterm_render_set_position(target, x, y);
		if (st_console_get_position(console, &target->screen.provisional_x,
		    &target->screen.provisional_y) != 0)
			return IO_ERROR_IO;
		status = st_console_write_utf8(console, (const char *)event->bytes,
		    event->size);
		break;
	case ST_UNICODE_PRESENTATION_REVISE:
		cterm_apply_window(target);
		if (st_console_set_position(console, target->screen.provisional_x,
		    target->screen.provisional_y) != 0)
			return IO_ERROR_IO;
		status = st_console_write_utf8(console, (const char *)event->bytes,
		    event->size);
		break;
	case ST_UNICODE_PRESENTATION_COMMIT:
		return 0;
	case ST_UNICODE_PRESENTATION_DECODE_ERROR:
		cterm_apply_window(target);
		x = target->xpos;
		y = target->ypos;
		cterm_coord_conv_xy(target, CTERM_COORD_ABSTERM,
		    CTERM_COORD_CURR, &x, &y);
		cterm_render_set_position(target, x, y);
		status = st_console_put_decode_error(console);
		break;
	default:
		return IO_ERROR_INVALID_ARGUMENT;
	}
	if (status == 0)
		cterm_coord_get_xy(target, CTERM_COORD_ABSTERM,
		    &target->xpos, &target->ypos);
	return status;
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
	struct st_console_style style;
	struct st_console_cell blank = {0};
	struct st_console_rect row;
	uint8_t width;
	int status;

	if (st_console_get_style(terminal->screen.console, &style) != 0) {
		cterm_render_record_error(terminal, IO_ERROR_IO);
		return;
	}
	row = (struct st_console_rect){
		.x = (uint32_t)(terminal->x - 1),
		.y = (uint32_t)(terminal->y + terminal->height - 1),
		.width = (uint32_t)terminal->width,
		.height = 1,
	};
	blank.foreground = style.foreground;
	blank.background = style.background;
	blank.flags = style.flags;
	blank.font = style.font;
	blank.glyph_font = style.font;
	status = st_console_font_lookup(
	    terminal->screen.console, blank.font, ' ', &blank.glyph_font,
	    &blank.glyph, &width);
	cterm_render_record_error(terminal, status);
	if (status != 0)
		return;
	cterm_render_record_error(terminal,
	    st_console_fill(terminal->screen.console, &row, &blank));
}

static void
status_display_changed(struct cterminal *terminal, int old_type,
	int new_type, void *context)
{
	struct cterm_render_state screen;
	uint32_t columns;
	uint32_t rows;
	int available;
	int new_rows;
	(void)context;

	if (st_console_size(terminal->screen.console, &columns, &rows) != 0) {
		terminal->status_display_type = old_type;
		cterm_render_record_error(terminal, IO_ERROR_IO);
		return;
	}
	available = terminal->height + (old_type >= 1 ? 1 : 0);
	new_rows = available - (new_type >= 1 ? 1 : 0);
	if (new_rows < 1 || columns > INT_MAX
	    || (uint32_t)(terminal->y - 1 + new_rows
	    + (new_type >= 1 ? 1 : 0)) > rows) {
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
	screen = terminal->screen;
	screen.unicode = NULL;
	screen.hold_update = 0;
	screen.error = 0;
	screen.hyperlink_root_registered = false;
	terminal->status_sub = cterm_init(1, terminal->width, terminal->x,
	    terminal->y + terminal->height, 0, terminal->width, NULL,
	    terminal->emulation, &screen);
	if (terminal->status_sub == NULL) {
		terminal->status_display_type = old_type;
		cterm_resize_rows(terminal,
		    available - (old_type >= 1 ? 1 : 0));
		cterm_render_record_error(terminal, IO_ERROR_OUT_OF_MEMORY);
		return;
	}
	terminal->status_sub->parent = terminal;
	terminal->status_sub->quiet = terminal->quiet;
	terminal->status_sub->response_cb = terminal->response_cb;
	terminal->status_sub->response_cbdata = terminal->response_cbdata;
	terminal->status_sub->keystroke_cb = terminal->keystroke_cb;
	terminal->status_sub->keystroke_cbdata = terminal->keystroke_cbdata;
	cterm_start(terminal->status_sub);
	cterm_apply_window(terminal);
}

static bool
utf8_supported(enum st_cterm_emulation emulation)
{
	return emulation == ST_CTERM_ANSI_BBS
	    || emulation == ST_CTERM_ATARI_ST_VT52;
}

static int
mark_hyperlinks(void *context, st_hyperlink_mark_fn mark, void *mark_context)
{
	st_cterm_t *terminal = context;
	struct st_console_cell *cells;
	struct st_console_rect rect;
	uint32_t columns;
	uint32_t rows;
	size_t count;
	int status;

	status = mark(mark_context, terminal->screen.current_hyperlink);
	if (status != 0)
		return status;
	if (terminal->status_sub != NULL) {
		status = mark(mark_context,
		    terminal->status_sub->screen.current_hyperlink);
		if (status != 0)
			return status;
	}
	if (terminal->backlines > 0 && terminal->scrollback != NULL) {
		count = (size_t)terminal->backlines * (size_t)terminal->backwidth;
		for (size_t i = 0; i < count; ++i) {
			status = mark(mark_context,
			    terminal->scrollback[i].hyperlink);
			if (status != 0)
				return status;
		}
	}
	status = st_console_size(terminal->screen.console, &columns, &rows);
	if (status != 0)
		return status;
	if (columns == 0 || rows == 0)
		return 0;
	if ((size_t)columns > SIZE_MAX / rows
	    || (size_t)columns * rows > SIZE_MAX / sizeof(*cells))
		return IO_ERROR_OUT_OF_RANGE;
	count = (size_t)columns * rows;
	cells = malloc(count * sizeof(*cells));
	if (cells == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	rect = (struct st_console_rect){0, 0, columns, rows};
	status = st_console_read_cells(terminal->screen.console, &rect, cells,
	    columns);
	for (size_t i = 0; status == 0 && i < count; ++i)
		status = mark(mark_context, cells[i].hyperlink);
	free(cells);
	return status;
}

static void
default_palette_mapping(enum st_cterm_emulation emulation,
	enum st_cterm_petscii_machine petscii_machine, uint32_t mapping[16])
{
	static const uint16_t colour[16] = {
		0, 4, 2, 6, 1, 5, 3, 7, 8, 12, 10, 14, 9, 13, 11, 15,
	};
	static const uint16_t c64[16] = {
		256, 257, 258, 259, 260, 261, 262, 263,
		264, 265, 266, 267, 268, 269, 270, 271,
	};
	static const uint16_t atari[16] = {
		272, 273, 273, 273, 273, 273, 273, 273,
		273, 273, 273, 273, 273, 273, 273, 273,
	};
	static const uint16_t prestel[16] = {
		0, 21, 46, 51, 196, 201, 226, 15,
		0, 21, 46, 51, 196, 201, 226, 15,
	};
	static const uint16_t atari_st[16] = {
		15, 196, 46, 226, 21, 201, 51, 274,
		275, 276, 277, 278, 279, 280, 281, 0,
	};
	const uint16_t *source = colour;

	switch (emulation) {
	case ST_CTERM_PETSCII:
		source = petscii_machine == ST_CTERM_PETSCII_C128_80
		    ? colour : c64;
		break;
	case ST_CTERM_ATASCII: source = atari; break;
	case ST_CTERM_PRESTEL:
	case ST_CTERM_BBC: source = prestel; break;
	case ST_CTERM_ATARI_ST_VT52: source = atari_st; break;
	default: break;
	}
	for (size_t i = 0; i < 16; ++i)
		mapping[i] = source[i];
}

static int
initialize_render_state(struct cterm_render_state *screen,
	const struct st_cterm_config *config)
{
	uint32_t capabilities;
	int mode = CTERM_MODE_IBM;
	int default_attribute = config->emulation == ST_CTERM_PETSCII
	    || config->emulation == ST_CTERM_ATARI_ST_VT52 ? 15 : 7;
	int status;

	status = st_console_capabilities(config->console, &capabilities);
	if (status != 0)
		return status;
	if (config->emulation == ST_CTERM_PETSCII) {
		switch (config->petscii_machine) {
		case ST_CTERM_PETSCII_C64: mode = CTERM_MODE_C64_40; break;
		case ST_CTERM_PETSCII_C128_40: mode = CTERM_MODE_C128_40; break;
		case ST_CTERM_PETSCII_C128_80: mode = CTERM_MODE_C128_80; break;
		default: return IO_ERROR_INVALID_ARGUMENT;
		}
	}
	screen->mode = mode;
	screen->default_attribute = (uint8_t)default_attribute;
	if (config->palette_mapping != NULL) {
		for (size_t i = 0; i < 16; ++i)
			screen->mode_palette[i] = config->palette_mapping[i];
	}
	else
		default_palette_mapping(config->emulation, config->petscii_machine,
		    screen->mode_palette);
	screen->capabilities = capabilities;
	screen->wscroll = 1;
	return 0;
}

int
st_cterm_create(st_cterm_t **result, const struct st_cterm_config *config)
{
	struct cterm_render_state screen = {0};
	struct st_console_cell *scrollback = NULL;
	st_cterm_t *terminal;
	uint16_t normal;
	uint32_t columns;
	uint32_t rows;
	uint32_t terminal_rows;
	uint32_t status_rows;
	int status;

	if (result == NULL || config == NULL || config->console == NULL
	    || config->fonts == NULL
	    || st_font_catalog_console(config->fonts) != config->console
	    || config->emulation < ST_CTERM_ANSI_BBS
	    || config->emulation > ST_CTERM_ATARI_ST_VT52
	    || config->encoding < ST_CTERM_ENCODING_UTF8
	    || config->encoding > ST_CTERM_ENCODING_CODEPAGE
	    || config->codepage >= ST_CODEPAGE_COUNT
	    || (config->encoding == ST_CTERM_ENCODING_UTF8
	    && !utf8_supported(config->emulation))
	    || !st_font_catalog_identifier(config->fonts,
	    config->font_selector, &normal)
	    || st_console_size(config->console, &columns, &rows) != 0
	    || config->terminal_rows > rows
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
	terminal_rows = config->terminal_rows == 0
	    ? rows - status_rows : config->terminal_rows;
	if (terminal_rows + status_rows > rows)
		return IO_ERROR_INVALID_ARGUMENT;
	if (config->scrollback_lines != 0) {
		if ((size_t)columns > SIZE_MAX / config->scrollback_lines
		    || (size_t)columns * config->scrollback_lines
		    > SIZE_MAX / sizeof(*scrollback))
			return IO_ERROR_OUT_OF_RANGE;
		scrollback = calloc((size_t)columns * config->scrollback_lines,
		    sizeof(*scrollback));
		if (scrollback == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
	}
	screen.console = config->console;
	screen.audio = config->audio;
	screen.hyperlinks = config->hyperlinks;
	screen.codepage = config->codepage;
	screen.fonts = (struct st_console_font_selection){
		.normal = normal, .bright = normal, .blink = normal,
		.bright_blink = normal,
	};
	for (size_t selector = 0; selector < UINT8_MAX + 1U; ++selector) {
		uint16_t identifier;

		if (st_font_catalog_identifier(config->fonts, (uint8_t)selector,
		    &identifier)) {
			screen.font_identifiers[selector] = identifier;
			screen.font_valid[selector] = true;
		}
	}
	for (size_t slot = 0; slot < 5; ++slot)
		screen.selectors[slot] = config->font_selector;
	terminal = cterm_init((int)terminal_rows, (int)columns, 1, 1,
	    (int)config->scrollback_lines, (int)columns, scrollback,
	    config->emulation, &screen);
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
	terminal->quiet = config->quiet;
	terminal->status_display_type = config->status_line ? 1 : 0;
	terminal->api_reserved_rows = rows - terminal_rows - status_rows;
	terminal->wire_encoding = config->encoding;
	terminal->api_output = config->output;
	terminal->api_output_context = config->output_context;
	terminal->api_apc = config->apc;
	terminal->api_apc_context = config->apc_context;
	terminal->api_size_changed = config->size_changed;
	terminal->api_size_context = config->size_context;
	terminal->response_cb = emit_response;
	terminal->response_cbdata = terminal;
	terminal->keystroke_cb = emit_user;
	terminal->keystroke_cbdata = terminal;
	terminal->apc_handler = config->apc == NULL ? NULL : emit_apc;
	terminal->apc_handler_data = terminal;
	terminal->size_change_cb = config->size_changed == NULL ? NULL : emit_size;
	terminal->size_change_cbdata = terminal;
	terminal->status_display_cb = status_display_changed;
	terminal->status_display_cbdata = terminal;
	terminal->mouse_state_change = mouse_mode_changed;
	terminal->mouse_state_change_cbdata = terminal;
	terminal->mouse_state_query = mouse_mode_query;
	terminal->mouse_state_query_cbdata = terminal;
	if (config->status_line) {
		clear_reserved_status(terminal);
		status = cterm_render_take_error(terminal);
		if (status != 0) {
			st_cterm_destroy(&terminal);
			return status;
		}
	}
	if (config->encoding == ST_CTERM_ENCODING_UTF8) {
		status = st_unicode_presentation_create(&terminal->screen.unicode,
		    present_unicode, terminal);
		if (status != 0) {
			st_cterm_destroy(&terminal);
			return status;
		}
	}
	if (config->hyperlinks != NULL) {
		status = st_hyperlink_registry_register_root(config->hyperlinks,
		    mark_hyperlinks, terminal);
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
	(void)cterm_render_set_hold_update(*terminal, 0);
	if ((*terminal)->screen.hyperlink_root_registered) {
		st_hyperlink_registry_unregister_root((*terminal)->screen.hyperlinks,
		    mark_hyperlinks, *terminal);
		(*terminal)->screen.hyperlink_root_registered = false;
	}
	cterm_end(*terminal, false);
	free(scrollback);
	*terminal = NULL;
}

static int
codepage_write(st_cterm_t *terminal, const uint8_t *bytes, size_t size,
	size_t *consumed)
{
	int speed = 0;
	int status;
	size_t offset = 0;

	*consumed = 0;

	status = cterm_render_take_error(terminal);
	if (status == 0 && terminal->status_sub != NULL)
		status = cterm_render_take_error(terminal->status_sub);
	if (status != 0)
		return status;

	while (size != 0) {
		int chunk = size > INT_MAX ? INT_MAX : (int)size;
		size_t used = cterm_write(terminal, bytes, chunk, &speed);

		offset += used;
		*consumed = offset;
		if (used != (size_t)chunk)
			return IO_ERROR_IO;
		bytes += chunk;
		size -= (size_t)chunk;
	}
	status = cterm_render_take_error(terminal);
	if (status == 0 && terminal->status_sub != NULL)
		status = cterm_render_take_error(terminal->status_sub);
	return status;
}

int
st_cterm_write(st_cterm_t *terminal, const void *bytes, size_t size,
	size_t *consumed)
{
	const uint8_t *input = bytes;
	size_t offset = 0;
	int status = 0;

	if (terminal == NULL || consumed == NULL || (bytes == NULL && size != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	if (terminal->wire_encoding == ST_CTERM_ENCODING_CODEPAGE) {
		status = codepage_write(terminal, input, size, consumed);
		if (terminal->api_status_destroy_pending) {
			destroy_status_sub(terminal);
			terminal->api_status_destroy_pending = false;
			if (terminal->status_display_type != 0)
				clear_reserved_status(terminal);
		}
		if (status == 0)
			status = cterm_render_take_error(terminal);
		return status;
	}
	while (offset < size) {
		uint8_t byte = input[offset];
		st_cterm_t *target = terminal->status_sub != NULL
		    && terminal->status_display_active == 1
		    ? terminal->status_sub : terminal;

		if (target->accumulator != NULL || byte < 0x20 || byte == 0x7f) {
			size_t used;

			status = st_unicode_presentation_boundary(
			    terminal->screen.unicode);
			if (status == 0)
				status = codepage_write(terminal, &byte, 1, &used);
			else
				used = 0;
			offset += used;
			if (status != 0)
				break;
		}
		else {
			size_t span = 1;
			size_t used;

			while (span < size - offset && input[offset + span] >= 0x20
			    && input[offset + span] != 0x7f)
				++span;
			status = st_unicode_presentation_feed(terminal->screen.unicode,
			    input + offset, span, &used);
			offset += used;
			if (status != 0)
				break;
		}
	}
	if (terminal->api_status_destroy_pending) {
		destroy_status_sub(terminal);
		terminal->api_status_destroy_pending = false;
		if (terminal->status_display_type != 0)
			clear_reserved_status(terminal);
	}
	if (status == 0)
		status = cterm_render_take_error(terminal);
	*consumed = offset;
	return status;
}

int
st_cterm_finish(st_cterm_t *terminal)
{
	int status;

	if (terminal == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = terminal->screen.unicode == NULL ? 0
	    : st_unicode_presentation_boundary(terminal->screen.unicode);
	if (status == 0)
		status = cterm_render_take_error(terminal);
	if (status == 0 && terminal->status_sub != NULL)
		status = cterm_render_take_error(terminal->status_sub);
	return status;
}

int
st_cterm_reset_terminal(st_cterm_t *terminal)
{
	int status = st_cterm_finish(terminal);

	if (status != 0)
		return status;
	cterm_reset(terminal);
	return cterm_render_take_error(terminal);
}

static int
resize_scrollback(st_cterm_t *terminal, uint32_t columns,
	struct st_console_cell **result)
{
	struct st_console_cell *resized;
	size_t count;
	size_t copy_columns;

	if (terminal->backlines == 0)
		return 0;
	if ((size_t)columns > SIZE_MAX / (size_t)terminal->backlines
	    || (size_t)columns * (size_t)terminal->backlines
	    > SIZE_MAX / sizeof(*resized))
		return IO_ERROR_OUT_OF_RANGE;
	count = (size_t)columns * (size_t)terminal->backlines;
	resized = calloc(count, sizeof(*resized));
	if (resized == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	copy_columns = columns < (uint32_t)terminal->backwidth
	    ? columns : (size_t)terminal->backwidth;
	for (int line = 0; line < terminal->backfilled; ++line) {
		int source = terminal->backstart + line;

		if (source >= terminal->backlines)
			source -= terminal->backlines;
		memcpy(resized + (size_t)line * columns,
		    terminal->scrollback + (size_t)source * terminal->backwidth,
		    copy_columns * sizeof(*resized));
	}
	*result = resized;
	return 0;
}

static void
resize_parser_geometry(st_cterm_t *terminal, int columns, int rows,
	int cursor_x, int cursor_y)
{
	int old_columns = terminal->width;
	int old_rows = terminal->height;
	uint32_t last_x = (uint32_t)(terminal->x - 1 + columns - 1);
	uint32_t last_y = (uint32_t)(terminal->y - 1 + rows - 1);

	terminal->width = columns;
	terminal->height = rows;
	if (terminal->right_margin == old_columns
	    || terminal->right_margin > columns)
		terminal->right_margin = columns;
	if (terminal->left_margin > columns)
		terminal->left_margin = 1;
	if (terminal->bottom_margin == old_rows
	    || terminal->bottom_margin > rows)
		terminal->bottom_margin = rows;
	if (terminal->top_margin > rows)
		terminal->top_margin = 1;
	if (cursor_x > columns)
		cursor_x = columns;
	if (cursor_y > rows)
		cursor_y = rows;
	if (terminal->screen.provisional_x > last_x)
		terminal->screen.provisional_x = last_x;
	if (terminal->screen.provisional_y > last_y)
		terminal->screen.provisional_y = last_y;
	cterm_apply_window(terminal);
	cterm_set_position(terminal, cursor_x, cursor_y);
}

int
st_cterm_resize(st_cterm_t *terminal, uint32_t columns, uint32_t rows)
{
	struct st_console_cell *scrollback = NULL;
	struct st_console_cell *old_scrollback;
	uint32_t status_rows;
	uint32_t terminal_rows;
	int cursor_x;
	int cursor_y;
	int status_x = 1;
	int status_y = 1;
	int status;

	if (terminal == NULL || columns == 0 || rows == 0
	    || columns > INT_MAX || rows > INT_MAX)
		return IO_ERROR_INVALID_ARGUMENT;
	status_rows = terminal->status_display_type >= 1 ? 1 : 0;
	if (rows <= terminal->api_reserved_rows + status_rows)
		return IO_ERROR_OUT_OF_RANGE;
	terminal_rows = rows - terminal->api_reserved_rows - status_rows;
	if (terminal->backlines != 0) {
		status = resize_scrollback(terminal, columns, &scrollback);
		if (status != 0)
			return status;
	}
	cursor_x = terminal->xpos;
	cursor_y = terminal->ypos;
	if (terminal->status_sub != NULL) {
		status_x = terminal->status_sub->xpos;
		status_y = terminal->status_sub->ypos;
	}
	status = st_console_resize(terminal->screen.console, columns, rows);
	if (status != 0) {
		free(scrollback);
		return status;
	}
	old_scrollback = terminal->scrollback;
	terminal->scrollback = scrollback;
	terminal->backwidth = (int)columns;
	terminal->backstart = 0;
	terminal->backpos = terminal->backfilled == terminal->backlines
	    ? 0 : terminal->backfilled;
	free(old_scrollback);
	resize_parser_geometry(terminal, (int)columns, (int)terminal_rows,
	    cursor_x, cursor_y);
	if (terminal->status_sub != NULL) {
		terminal->status_sub->x = terminal->x;
		terminal->status_sub->y = terminal->y + terminal->height;
		resize_parser_geometry(terminal->status_sub, (int)columns, 1,
		    status_x, status_y);
	}
	if (terminal->status_sub == NULL || terminal->status_display_active == 0)
		cterm_apply_window(terminal);
	status = cterm_render_take_error(terminal);
	if (status == 0 && terminal->status_sub != NULL)
		status = cterm_render_take_error(terminal->status_sub);
	return status;
}

int
st_cterm_size(const st_cterm_t *terminal, uint32_t *columns, uint32_t *rows)
{
	if (terminal == NULL || columns == NULL || rows == NULL
	    || terminal->width < 1 || terminal->height < 1)
		return IO_ERROR_INVALID_ARGUMENT;
	*columns = (uint32_t)terminal->width;
	*rows = (uint32_t)terminal->height;
	return 0;
}

enum st_cterm_encoding
st_cterm_encoding(const st_cterm_t *terminal)
{
	return terminal == NULL ? ST_CTERM_ENCODING_CODEPAGE
	    : terminal->wire_encoding;
}

enum st_codepage
st_cterm_codepage(const st_cterm_t *terminal)
{
	return terminal == NULL ? ST_CODEPAGE_CP437 : terminal->screen.codepage;
}

int
st_cterm_set_encoding(st_cterm_t *terminal, enum st_cterm_encoding encoding,
	enum st_codepage codepage)
{
	int status;

	if (terminal == NULL || encoding < ST_CTERM_ENCODING_UTF8
	    || encoding > ST_CTERM_ENCODING_CODEPAGE
	    || codepage >= ST_CODEPAGE_COUNT
	    || (encoding == ST_CTERM_ENCODING_UTF8
	    && !utf8_supported((enum st_cterm_emulation)terminal->emulation)))
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_cterm_finish(terminal);
	if (status != 0)
		return status;
	if (encoding == ST_CTERM_ENCODING_UTF8 && terminal->screen.unicode == NULL) {
		status = st_unicode_presentation_create(&terminal->screen.unicode,
		    present_unicode, terminal);
		if (status != 0)
			return status;
	}
	else if (encoding == ST_CTERM_ENCODING_CODEPAGE)
		st_unicode_presentation_destroy(&terminal->screen.unicode);
	terminal->wire_encoding = encoding;
	terminal->screen.codepage = codepage;
	return 0;
}

int
st_cterm_copy_history(st_cterm_t *terminal,
	struct st_console_cell **cells, uint32_t *columns, size_t *rows)
{
	struct st_console_cell *copy;
	struct st_console_rect visible;
	size_t line_count;
	size_t cell_count;
	int status;

	if (terminal == NULL || cells == NULL || columns == NULL || rows == NULL
	    || terminal->width <= 0 || terminal->height <= 0
	    || terminal->backfilled < 0)
		return IO_ERROR_INVALID_ARGUMENT;
	line_count = (size_t)terminal->backfilled + (size_t)terminal->height;
	if ((size_t)terminal->width > SIZE_MAX / line_count
	    || (size_t)terminal->width * line_count > SIZE_MAX / sizeof(*copy))
		return IO_ERROR_OUT_OF_RANGE;
	cell_count = (size_t)terminal->width * line_count;
	copy = malloc(cell_count * sizeof(*copy));
	if (copy == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	for (int line = 0; line < terminal->backfilled; ++line) {
		int source = terminal->backstart + line;

		if (source >= terminal->backlines)
			source -= terminal->backlines;
		memcpy(copy + (size_t)line * terminal->width,
		    terminal->scrollback + (size_t)source * terminal->backwidth,
		    (size_t)terminal->width * sizeof(*copy));
	}
	visible = (struct st_console_rect){
		.x = (uint32_t)(terminal->x - 1),
		.y = (uint32_t)(terminal->y - 1),
		.width = (uint32_t)terminal->width,
		.height = (uint32_t)terminal->height,
	};
	status = st_console_read_cells(terminal->screen.console, &visible,
	    copy + (size_t)terminal->backfilled * terminal->width,
	    (size_t)terminal->width);
	if (status != 0) {
		free(copy);
		return status;
	}
	*cells = copy;
	*columns = (uint32_t)terminal->width;
	*rows = line_count;
	return 0;
}

bool
st_cterm_handle_key(st_cterm_t *terminal, uint16_t key_code)
{
	int doorway_key;

	if (terminal == NULL)
		return false;
	switch (key_code) {
	case ST_CONSOLE_KEY_UP: doorway_key = CTERM_KEY_DOS_UP; break;
	case ST_CONSOLE_KEY_DOWN: doorway_key = CTERM_KEY_DOS_DOWN; break;
	case ST_CONSOLE_KEY_LEFT: doorway_key = CTERM_KEY_DOS_LEFT; break;
	case ST_CONSOLE_KEY_RIGHT: doorway_key = CTERM_KEY_DOS_RIGHT; break;
	case ST_CONSOLE_KEY_INSERT: doorway_key = CTERM_KEY_DOS_IC; break;
	case ST_CONSOLE_KEY_DELETE: doorway_key = CTERM_KEY_DOS_DC; break;
	case ST_CONSOLE_KEY_HOME: doorway_key = CTERM_KEY_DOS_HOME; break;
	case ST_CONSOLE_KEY_END: doorway_key = CTERM_KEY_DOS_END; break;
	case ST_CONSOLE_KEY_PAGE_UP: doorway_key = CTERM_KEY_DOS_PPAGE; break;
	case ST_CONSOLE_KEY_PAGE_DOWN: doorway_key = CTERM_KEY_DOS_NPAGE; break;
	default:
		if (key_code >= ST_CONSOLE_KEY_F1 && key_code <= ST_CONSOLE_KEY_F12)
			doorway_key = CTERM_KEY_DOS_F(key_code - ST_CONSOLE_KEY_F1 + 1);
		else
			doorway_key = key_code;
	}
	return cterm_handle_key(terminal, doorway_key) == CTERM_KEY_HANDLED;
}

bool
st_cterm_handle_doorway_key(st_cterm_t *terminal, uint8_t scan_code)
{
	return terminal != NULL
	    && cterm_handle_key(terminal, (int)scan_code << 8)
	    == CTERM_KEY_HANDLED;
}

bool
st_cterm_handle_physical_keys(st_cterm_t *terminal,
	const struct st_console_event *events, size_t count)
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
st_cterm_handle_mouse(st_cterm_t *terminal,
	const struct st_console_event *event)
{
	uint16_t modes;
	uint32_t changed;
	uint32_t button = 0;
	uint32_t x;
	uint32_t y;
	int code;
	bool release = false;
	bool report;
	char sequence[96];
	int length;

	if (terminal == NULL || event == NULL
	    || event->type != ST_CONSOLE_EVENT_MOUSE)
		return false;
	modes = terminal->api_mouse_modes;
	changed = terminal->api_mouse_buttons ^ event->data.mouse.buttons;
	terminal->api_mouse_buttons = event->data.mouse.buttons;
	if (event->data.mouse.action == ST_CONSOLE_MOUSE_WHEEL) {
		if ((modes & (MOUSE_X10 | MOUSE_NORMAL | MOUSE_BUTTON | MOUSE_ANY))
		    == 0 && (modes & MOUSE_ALT_SCROLL) != 0) {
			int key = event->data.mouse.wheel_y > 0 ? CTERM_KEY_DOS_UP
			    : CTERM_KEY_DOS_DOWN;
			return cterm_handle_key(terminal, key) == CTERM_KEY_HANDLED;
		}
		code = event->data.mouse.wheel_y > 0 ? 64 : 65;
	}
	else if (event->data.mouse.action == ST_CONSOLE_MOUSE_MOVE) {
		report = (modes & MOUSE_ANY) != 0
		    || ((modes & MOUSE_BUTTON) != 0
		    && event->data.mouse.buttons != 0);
		if (!report)
			return false;
		button = event->data.mouse.buttons & (~event->data.mouse.buttons + 1U);
		code = mouse_button_code(button);
		if (code < 0)
			code = 3;
		code += 32;
	}
	else {
		if (changed == 0)
			return false;
		button = changed & (~changed + 1U);
		code = mouse_button_code(button);
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
	if ((modes & MOUSE_SGR_PIXELS) != 0 && event->data.mouse.pixel_x >= 0
	    && event->data.mouse.pixel_y >= 0) {
		x = (uint32_t)event->data.mouse.pixel_x + 1;
		y = (uint32_t)event->data.mouse.pixel_y + 1;
	}
	else {
		x = event->data.mouse.x < 0 ? 1U : (uint32_t)event->data.mouse.x + 1;
		y = event->data.mouse.y < 0 ? 1U : (uint32_t)event->data.mouse.y + 1;
	}
	if ((modes & (MOUSE_SGR | MOUSE_SGR_PIXELS)) != 0)
		length = snprintf(sequence, sizeof(sequence), "\x1b[<%d;%u;%u%c",
		    code, x, y, release ? 'm' : 'M');
	else if ((modes & MOUSE_URXVT) != 0)
		length = snprintf(sequence, sizeof(sequence), "\x1b[%d;%u;%uM",
		    code + 32, x, y);
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
		if (code + 32 > UINT8_MAX || x + 32 > UINT8_MAX
		    || y + 32 > UINT8_MAX)
			return false;
		sequence[0] = '\x1b'; sequence[1] = '['; sequence[2] = 'M';
		sequence[3] = (char)(code + 32);
		sequence[4] = (char)(x + 32);
		sequence[5] = (char)(y + 32);
		length = 6;
	}
	if (length <= 0 || (size_t)length >= sizeof(sequence))
		return false;
	emit_user(sequence, (size_t)length, terminal);
	return true;
}

bool
st_cterm_handle_focus(st_cterm_t *terminal, bool focused)
{
	static const char gained[] = "\x1b[I";
	static const char lost[] = "\x1b[O";

	if (terminal == NULL || (terminal->api_mouse_modes & MOUSE_FOCUS) == 0)
		return false;
	emit_user(focused ? gained : lost, 3, terminal);
	return true;
}

/* Local hyperlink interaction for connected terminal input. */
/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */
#include "wren_host_internal.h"
#include "io_error.h"
#include "st_cterm.h"
#include "st_hyperlink.h"
#include "string_compat.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static char
url_character(st_console_t *console, const struct st_console_cell *cell)
{
	char text[4];
	size_t length;
	if (st_console_font_glyph_text(console, cell->glyph_font, cell->glyph,
	    text, sizeof(text), &length) != 0 || length != 1)
		return 0;
	unsigned char ch = (unsigned char)text[0];
	return ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')
	    || (ch >= '0' && ch <= '9') || (ch != 0
	    && strchr("._~:/?#@!$&'()*+,;=%-[]", ch) != NULL)) ? (char)ch : 0;
}

static bool
ascii_prefix(const char *text, const char *prefix)
{
	while (*prefix != '\0') {
		unsigned char ch = (unsigned char)*text++;
		if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
		if (ch != (unsigned char)*prefix++) return false;
	}
	return true;
}

bool
st_wren_uri_openable(const char *uri)
{
	return ascii_prefix(uri, "http://") || ascii_prefix(uri, "https://")
	    || ascii_prefix(uri, "ftp://") || ascii_prefix(uri, "ftps://");
}

char *
st_wren_detect_url(st_console_t *console, const struct st_console_cell *cells,
	uint32_t width, uint32_t height, uint32_t x, uint32_t y)
{
	char text[2048];
	if (console == NULL || cells == NULL || width == 0 || height == 0 || x >= width || y >= height
	    || height > SIZE_MAX / width)
		return NULL;
	size_t count = (size_t)width * height;
	size_t click = (size_t)y * width + x;
	if (url_character(console, &cells[click]) == 0) return NULL;
	size_t start = click, end = click;
	while (start > 0 && click - start < sizeof(text) - 2
	    && url_character(console, &cells[start - 1]) != 0) --start;
	while (end + 1 < count && end - start < sizeof(text) - 2
	    && url_character(console, &cells[end + 1]) != 0) ++end;
	/* Never open a prefix of a URL that exceeded the detection limit. */
	if ((start > 0 && url_character(console, &cells[start - 1]) != 0)
	    || (end + 1 < count && url_character(console, &cells[end + 1]) != 0))
		return NULL;
	size_t length = end - start + 1;
	for (size_t i = 0; i < length; ++i)
		text[i] = url_character(console, &cells[start + i]);
	text[length] = '\0';
	size_t offset = 0;
	while (offset <= click - start && !st_wren_uri_openable(text + offset)
	    && !ascii_prefix(text + offset, "www.")) ++offset;
	if (offset > click - start) return NULL;
	char *uri = text + offset;
	length -= offset;
	while (length > 0) {
		char last = uri[length - 1];
		bool trim = strchr(".,;:>", last) != NULL;
		if (last == ')' || last == ']') {
			int balance = 0;
			char opening = last == ')' ? '(' : '[';
			for (size_t i = 0; i < length; ++i) {
				if (uri[i] == opening) ++balance;
				else if (uri[i] == last) --balance;
			}
			trim = balance < 0;
		}
		if (!trim) break;
		uri[--length] = '\0';
	}
	if (click - start - offset >= length) return NULL;
	if (!ascii_prefix(uri, "www.")) return strdup(uri);
	char *result = malloc(length + sizeof("https://"));
	if (result != NULL) {
		memcpy(result, "https://", 8);
		memcpy(result + 8, uri, length + 1);
	}
	return result;
}

static bool
link_cell(struct st_wren_context *context, int32_t x, int32_t y,
	struct st_console_cell *cell, struct st_console_rect *area)
{
	uint32_t origin_x, origin_y, width, height;
	if (context->cterm == NULL || context->cterm_suspended || x < 0 || y < 0
	    || st_cterm_origin(context->cterm, &origin_x, &origin_y) != 0
	    || st_cterm_size(context->cterm, &width, &height) != 0)
		return false;
	*area = (struct st_console_rect){origin_x - 1, origin_y - 1, width, height};
	if ((uint32_t)x < area->x || (uint32_t)y < area->y
	    || (uint32_t)x - area->x >= width || (uint32_t)y - area->y >= height)
		return false;
	const struct st_console_rect point = {(uint32_t)x, (uint32_t)y, 1, 1};
	return st_console_read_cells(context->console, &point, cell, 1) == 0;
}

char *
st_wren_hover_uri(struct st_wren_context *context)
{
	struct st_console_cell cell;
	struct st_console_rect area;
	struct st_hyperlink link = {0};
	if (!context->link_hover_valid || !link_cell(context, context->link_hover_x,
	    context->link_hover_y, &cell, &area) || cell.hyperlink == 0
	    || st_hyperlink_registry_copy(context->host->config.hyperlinks,
	    cell.hyperlink, &link) != 0)
		return NULL;
	free(link.parameter);
	return link.uri;
}

bool
st_wren_link_mouse(struct st_wren_context *context,
	const struct st_wren_input_record *record)
{
	const int event = record->data.mouse.event;
	if (event == ST_WREN_MOUSE_MOVE) {
		context->link_hover_x = record->data.mouse.end_x > 0 ? record->data.mouse.end_x - 1 : -1;
		context->link_hover_y = record->data.mouse.end_y > 0 ? record->data.mouse.end_y - 1 : -1;
		context->link_hover_valid = true;
		if (context->cterm != NULL && st_cterm_mouse_mode(context->cterm) == 0) {
			char *uri = st_wren_hover_uri(context);
			(void)st_console_set_pointer_shape(context->console, uri == NULL
			    ? ST_CONSOLE_POINTER_TEXT : ST_CONSOLE_POINTER_LINK);
			free(uri);
		}
	}
	if (context->link_gesture && (event == ST_WREN_MOUSE_MOVE
	    || (event >= ST_WREN_MOUSE_LEFT_RELEASE && event <= ST_WREN_MOUSE_LEFT_DRAG_END))) {
		if ((event >= ST_WREN_MOUSE_LEFT_CLICK && event <= ST_WREN_MOUSE_LEFT_QUAD_CLICK)
		    || event == ST_WREN_MOUSE_LEFT_DRAG_END
		    || (event == ST_WREN_MOUSE_MOVE
		    && (record->data.mouse.buttons & ST_CONSOLE_MOUSE_BUTTON_LEFT) == 0))
			context->link_gesture = false;
		return true;
	}
	if (event != ST_WREN_MOUSE_LEFT_PRESS) return false;
	context->link_gesture = false;
	bool control = (record->data.mouse.modifiers & ST_CONSOLE_MODIFIER_CONTROL) != 0;
	if (!control && !context->cterm_mouse_disabled
	    && st_cterm_mouse_mode(context->cterm) != 0)
		return false;
	struct st_console_cell cell;
	struct st_console_rect area;
	int32_t x = record->data.mouse.start_x > 0 ? record->data.mouse.start_x - 1 : -1;
	int32_t y = record->data.mouse.start_y > 0 ? record->data.mouse.start_y - 1 : -1;
	if (!link_cell(context, x, y, &cell, &area)) return false;
	struct st_hyperlink link = {0};
	char *uri = NULL;
	if (cell.hyperlink != 0 && st_hyperlink_registry_copy(
	    context->host->config.hyperlinks, cell.hyperlink, &link) == 0) {
		uri = link.uri;
		free(link.parameter);
	}
	else if (control && area.height <= SIZE_MAX / area.width / sizeof(cell)) {
		size_t count = (size_t)area.width * area.height;
		struct st_console_cell *cells = malloc(count * sizeof(*cells));
		if (cells != NULL && st_console_read_cells(context->console, &area,
		    cells, area.width) == 0)
			uri = st_wren_detect_url(context->console, cells, area.width, area.height,
			    (uint32_t)x - area.x,
			    (uint32_t)y - area.y);
		free(cells);
	}
	if (uri == NULL) return false;
	context->link_gesture = true;
	if (context->host->config.safe_mode || !st_wren_uri_openable(uri)
	    || st_console_open_url(context->console, uri) != 0) {
		int copied = st_console_set_clipboard_text(context->console, uri);
		if (!context->profile->hide_popups)
			(void)st_wren_host_alert(context->host, copied == 0
			    ? "URL copied to clipboard" : "Unable to open URL", uri);
	}
	free(uri);
	return true;
}

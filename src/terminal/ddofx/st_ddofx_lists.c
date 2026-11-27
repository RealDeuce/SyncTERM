/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>
#include <string.h>

#include "st_ddofx_graphics_internal.h"
#include "st_ddofx_script.h"
#include "st_time.h"
#include "string_util.h"

enum {
	LIST_COUNT      = 300,
	DOUBLE_CLICK_MS = 500
};

struct item {
	char                     description[41];
	struct st_ddofx_callback callback;
	bool                     selectable, selected;
};

struct list_box {
	struct st_console_rect area;
	unsigned               columns, rows, count, top, highlight;
	unsigned               ink, paper, highlight_ink, highlight_paper, selected_ink, selected_paper;
	uint64_t               clicked_at;
	unsigned               clicked_item;
	bool                   multiple, active, stealth;
	struct item            items[];
};

struct st_ddofx_lists {
	struct list_box *boxes[LIST_COUNT];
};

void
st_ddofx_clear_lists(struct st_ddofx_graphics *g)
{
	if (g->lists) {
		for (unsigned i = 0; i < LIST_COUNT; ++i)
			free(g->lists->boxes[i]);
		free(g->lists);
		g->lists = NULL;
	}
}

static void
define(struct st_ddofx *ddo, unsigned id, char *args)
{
	int32_t n[12];
	if (!st_ddofx_numbers(ddo, args, n, 12) || n[2] < 1 || n[2] > 40 || n[3] < 1 || n[3] > 30 || n[0] < 0
	    || n[0] > WIDTH - 8 * n[2] || n[1] < 16 * n[3] - 1 || n[1] >= HEIGHT || n[10] < 1 || n[10] > 100
	    || (unsigned)n[11] > 1)
		return;
	for (unsigned i = 4; i < 10; i += 2)
		if ((unsigned)n[i] > 255 || (unsigned)n[i + 1] > 7)
			return;
	struct st_ddofx_lists *lists = ddo->graphics->lists;
	if (!lists)
		ddo->graphics->lists = lists = calloc(1, sizeof(*lists));
	if (!lists)
		return;
	struct list_box *box = calloc(1, sizeof(*box) + (size_t)n[10] * sizeof(struct item));
	if (!box)
		return;
	box->area            = (struct st_console_rect) {n[0], n[1] - 16 * n[3] + 1, 8 * n[2], 16 * n[3]};
	box->columns         = (unsigned)n[2];
	box->rows            = (unsigned)n[3];
	box->ink             = (unsigned)n[4];
	box->paper           = (unsigned)n[5];
	box->highlight_ink   = (unsigned)n[6];
	box->highlight_paper = (unsigned)n[7];
	box->selected_ink    = (unsigned)n[8];
	box->selected_paper  = (unsigned)n[9];
	box->count           = (unsigned)n[10];
	box->multiple        = n[11] != 0;
	box->stealth         = ddo->define_stealth;
	for (unsigned i = 0; i < box->count; ++i) {
		box->items[i].selectable       = true;
		box->items[i].callback.stealth = box->stealth;
	}
	free(lists->boxes[id - 1]);
	lists->boxes[id - 1] = box;
}

static void
modify(struct st_ddofx *ddo, struct list_box *box, char *args)
{
	int32_t id, selectable, selected;
	if (!st_ddofx_field_number(ddo, &args, &id) || (unsigned)id > box->count
	    || !st_ddofx_field_number(ddo, &args, &selectable) || !st_ddofx_field_number(ddo, &args, &selected))
		return;
	const char *description = st_ddofx_field(&args), *reply = st_ddofx_field(&args);
	if (!args)
		return;
	description = st_ddofx_value(ddo, description);
	struct st_ddofx_callback cb;
	st_ddofx_callback_define(ddo, &cb, reply, args);
	cb.macro[50] = cb.reply[50] = 0;
	for (unsigned i = id ? (unsigned)id - 1 : 0, end = id ? (unsigned)id : box->count; i < end; ++i) {
		struct item *item = &box->items[i];
		if ((unsigned)selectable <= 1)
			item->selectable = selectable != 0;
		if ((unsigned)selected <= 1)
			item->selected = selected != 0;
		if (*description)
			snprintf(item->description, sizeof(item->description), "%.40s", description);
		if (*cb.reply)
			strcpy(item->callback.reply, cb.reply);
		if (*cb.macro) {
			strcpy(item->callback.macro, cb.macro);
			item->callback.resource_text = cb.resource_text;
		}
	}
}

struct list_image {
	struct st_ddofx_graphics *g;
	const struct list_box    *box;
	const uint8_t            *font;
};

static int
paint(void *context, const struct st_pixel_buffer *pixels, struct st_console_rect *damage)
{
	const struct list_image *im = context;
	const struct list_box   *b  = im->box;
	if (pixels && (pixels->width != WIDTH || pixels->height != HEIGHT))
		return 0;
	for (unsigned row = 0; row < b->rows; ++row) {
		unsigned index = b->top + row;
		unsigned ink = b->ink, paper = b->paper;
		uint8_t  line[40];
		memset(line, ' ', b->columns);
		if (index < b->count) {
			const struct item *item = &b->items[index];
			size_t             n    = strlen(item->description);
			memcpy(line, item->description, n < b->columns ? n : b->columns);
			if (item->selected) {
				ink   = b->selected_ink;
				paper = b->selected_paper;
			}
			if (row == b->highlight) {
				if (!item->selected)
					ink = b->highlight_ink;
				paper = b->highlight_paper;
			}
		}
		st_ddofx_paint_vga_string(im->g, pixels, im->font, line, b->columns, b->area.x,
		    b->area.y + 16 * row, ink, paper, NULL);
	}
	if (damage)
		*damage = b->area;
	return 0;
}

static void
draw(struct st_ddofx *ddo, const struct list_box *box, bool work)
{
	struct list_image im = {ddo->graphics, box, st_ddofx_vga_bitmap(ddo)};
	if (!im.font)
		return;
	if (work)
		(void)paint(&im, NULL, NULL);
	else
		(void)st_console_modify_pixels(im.g->display, paint, &im);
}

static void
toggle(struct list_box *b)
{
	unsigned index = b->top + b->highlight;
	if (index >= b->count || !b->items[index].selectable)
		return;
	if (!b->multiple && !b->items[index].selected)
		for (unsigned i = 0; i < b->count; ++i)
			b->items[i].selected = false;
	b->items[index].selected = !b->items[index].selected;
}

static void
navigate(struct list_box *b, bool highlight, int32_t type, int direction)
{
	unsigned visible = b->count < b->rows ? b->count : b->rows;
	int      last    = (int)(b->count - visible);
	if (highlight) {
		if (type == 1) {
			b->highlight = direction > 0 ? visible - 1 : 0;
			return;
		}
		int next = (int)b->highlight + direction;
		if (next >= 0 && (unsigned)next < visible) {
			b->highlight = (unsigned)next;
			return;
		}
	}
	int distance = type == 0 ? 1 : type == 1 ? (int)visible : last;
	int top      = (int)b->top + direction * distance;
	b->top       = (unsigned)(top < 0 ? 0 : top > last ? last : top);
}

void
st_ddofx_list_click(struct st_ddofx *ddo, int x, int y)
{
	struct st_ddofx_lists *lists = ddo->graphics->lists;
	if (!lists)
		return;
	uint64_t now       = 0;
	bool     have_time = !st_time_monotonic_ms(&now);
	for (unsigned i = 0; i < LIST_COUNT; ++i) {
		struct list_box *b = lists->boxes[i];
		if (!b || !b->active || (ddo->background_download && !b->stealth))
			continue;
		struct st_console_rect r = b->area;
		if ((unsigned)x < r.x || (unsigned)x >= r.x + r.width || (unsigned)y < r.y
		    || (unsigned)y >= r.y + r.height) {
			b->clicked_item = 0;
			continue;
		}
		unsigned row = ((unsigned)y - r.y) / 16;
		unsigned id  = b->top + row + 1;
		if (id > b->count) {
			b->clicked_item = 0;
			continue;
		}
		b->highlight = row;
		if (have_time && b->clicked_item == id && now - b->clicked_at <= DOUBLE_CLICK_MS) {
			toggle(b);
			b->clicked_item = 0;
		}
		else {
			b->clicked_item = have_time ? id : 0;
			b->clicked_at   = now;
		}
		draw(ddo, b, false);
	}
}

bool
st_ddofx_next_list_item(struct st_ddofx *ddo, unsigned id, unsigned *next)
{
	struct st_ddofx_lists *lists = ddo->graphics ? ddo->graphics->lists : NULL;
	const struct list_box *b     = lists ? lists->boxes[id - 1] : NULL;
	if (!b || !b->active)
		return false;
	while (*next < b->count) {
		const struct item *item = &b->items[(*next)++];
		if (item->selected && st_ddofx_callback_enabled(ddo, &item->callback))
			return st_ddofx_callback_start(ddo, &item->callback, DDO_EVENT);
	}
	return false;
}

bool
st_ddofx_list_command(struct st_ddofx *ddo, const char *verb, char *args)
{
	enum {
		CREATE,
		MODIFY,
		DRAW,
		SHOW,
		EXECUTE,
		CLOSE,
		DISABLE,
		ENABLE,
		SCROLL,
		HIGHLIGHT,
		TOGGLE
	};

	static const char *const verbs[] = {"5CLBOX", "5LBMODLST", "5LBDRWLST", "5LBSHWLST", "5LBEXCLST",
	    "5LBCLSLST", "5LBDEACT", "5LBREACT", "5LBADJLST", "5LBADJHLT", "5LBTGLSEL"};
	unsigned                 op      = 0;
	while (op < sizeof(verbs) / sizeof(*verbs) && ascii_case_compare(verb, verbs[op]))
		++op;
	if (op == sizeof(verbs) / sizeof(*verbs))
		return false;
	int32_t id;
	if (!st_ddofx_field_number(ddo, &args, &id) || (unsigned)id > LIST_COUNT)
		return true;
	struct st_ddofx_lists *lists = ddo->graphics->lists;
	if (op == CREATE) {
		if (id)
			define(ddo, (unsigned)id, args);
		else
			st_ddofx_clear_lists(ddo->graphics);
		return true;
	}
	struct list_box *box = id && lists ? lists->boxes[id - 1] : NULL;
	if (!box)
		return true;
	if (op == MODIFY)
		modify(ddo, box, args);
	else if (op == SCROLL || op == HIGHLIGHT) {
		int32_t n[2];
		if (box->active && st_ddofx_numbers(ddo, args, n, 2)
		    && (unsigned)n[0] <= (op == HIGHLIGHT ? 1u : 2u) && (n[1] == -1 || n[1] == 1)) {
			navigate(box, op == HIGHLIGHT, n[0], n[1]);
			box->clicked_item = 0;
			draw(ddo, box, false);
		}
	}
	else if (!args) {
		switch (op) {
			case DRAW:
			case SHOW:
				box->active = true;
				draw(ddo, box, op == DRAW);
				break;
			case EXECUTE:
				if (box->active && (!ddo->background_download || box->stealth))
					st_ddofx_execute_group(ddo, DDO_LIST_BOX, (unsigned)id, false);
				break;
			case CLOSE:
				free(box);
				lists->boxes[id - 1] = NULL;
				break;
			case ENABLE:
			case DISABLE:
				box->active       = op == ENABLE;
				box->clicked_item = 0;
				break;
			case TOGGLE:
				if (box->active) {
					toggle(box);
					draw(ddo, box, false);
				}
				break;
		}
	}
	return true;
}

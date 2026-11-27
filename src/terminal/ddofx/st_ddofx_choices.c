/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>
#include <string.h>

#include "st_ddofx_graphics_internal.h"
#include "st_ddofx_script.h"
#include "string_util.h"

enum {
	CHOICE_COUNT = 300
};

struct choice {
	char                     on[9], off[9], disabled[9];
	int                      x, y, fx, fy;
	uint8_t                  colours[2];
	int32_t                  exclusive, multiple;
	uint16_t                 key;
	bool                     enabled, selected, pass;
	struct st_ddofx_callback callback;
};

struct st_ddofx_choices {
	struct choice slots[CHOICE_COUNT];
	unsigned      count;
};

unsigned
st_ddofx_control_count(const struct st_ddofx *ddo)
{
	unsigned count = st_ddofx_button_count(ddo);
	unsigned fields = st_ddofx_text_field_count(ddo);
	if (fields > count)
		count = fields;
	if (ddo->graphics) {
		for (unsigned i = 0; i < 2; ++i) {
			const struct st_ddofx_choices *table = ddo->graphics->choices[i];
			if (table && count < table->count)
				count = table->count;
		}
	}
	return count;
}

struct radio_fill {
	struct st_console_rect area;
	int                    x, y;
	uint8_t                colour;
};

static int
fill_visible(void *context, const struct st_pixel_buffer *pixels, const uint32_t *palette,
    struct st_console_rect *changed)
{
	const struct radio_fill      *fill = context;
	const struct st_console_rect *r    = &fill->area;
	if (pixels->width != WIDTH || pixels->height != HEIGHT)
		return -1;
	uint8_t *indices = malloc((size_t)r->width * r->height);
	if (!indices)
		return -1;
	for (unsigned y = 0; y < r->height; ++y) {
		const uint32_t *row = pixels->primary + (r->y + y) * WIDTH + r->x;
		for (unsigned x = 0; x < r->width; ++x)
			indices[y * r->width + x] = st_console_palette_index256(palette, row[x]);
	}
	struct bounds bounds = {0, (int)r->width - 1, 0, (int)r->height - 1};
	st_ddofx_flood(indices, (int)r->width, &bounds, fill->x, fill->y, fill->colour);
	for (unsigned y = 0; y < r->height; ++y) {
		size_t offset = (r->y + y) * WIDTH + r->x;
		for (unsigned x = 0; x < r->width; ++x) {
			uint8_t index = indices[y * r->width + x];
			if (index != st_console_palette_index256(palette, pixels->primary[offset + x]))
				pixels->primary[offset + x] = pixels->alternate[offset + x] = index;
		}
	}
	free(indices);
	*changed = *r;
	return 0;
}

static void
paint(struct st_ddofx_graphics *g, enum st_ddofx_group_kind kind, const struct choice *c, enum page page,
    bool selection_changed)
{
	const char                   *name = !c->enabled ? c->disabled : c->selected ? c->on : c->off;
	const struct st_ddofx_sprite *s    = st_ddofx_find_sprite(g, name);
	if (!st_ddofx_sprite_fits(s, c->x, c->y))
		return;
	if (kind == DDO_CHECKBOX || !selection_changed)
		st_ddofx_draw_sprite(g, s, page, 'D', c->x, c->y);
	if (kind == DDO_CHECKBOX) {
		if (selection_changed)
			st_ddofx_draw_sprite(g, s, HIDDEN, 'D', c->x, c->y);
		return;
	}
	if (!c->enabled || c->fx >= s->width || c->fy >= s->height)
		return;
	int     top    = c->y - s->height + 1;
	uint8_t colour = c->colours[c->selected];
	if (page == WORK) {
		struct bounds bounds = {c->x, c->x + s->width - 1, top, c->y};
		st_ddofx_flood((uint8_t *)g->page[WORK], WIDTH, &bounds, c->x + c->fx, c->y - c->fy, colour);
	}
	else {
		struct radio_fill fill = {
		    {c->x, top, s->width, s->height},
                    c->fx, s->height - 1 - c->fy, colour
                };
		(void)st_console_modify_pixels_with_palette(g->display, fill_visible, &fill);
	}
}

static void
define(struct st_ddofx *ddo, enum st_ddofx_group_kind kind, unsigned slot, char *args)
{
	struct st_ddofx_graphics *g = ddo->graphics;
	struct choice             c = {0};
	if (!slot) {
		free(g->choices[kind]);
		g->choices[kind] = NULL;
		return;
	}
	if (kind == DDO_RADIO) {
		int32_t v[4];
		for (unsigned i = 0; i < 4; ++i)
			if (!st_ddofx_field_number(ddo, &args, &v[i]))
				return;
		if (v[0] < 0 || v[1] < 0 || (unsigned)v[2] > 255 || (unsigned)v[3] > 255)
			return;
		c.fx         = v[0];
		c.fy         = v[1];
		c.colours[1] = (uint8_t)v[2];
		c.colours[0] = (uint8_t)v[3];
	}
	const char *on       = st_ddofx_field(&args);
	const char *off      = kind == DDO_RADIO ? on : st_ddofx_field(&args);
	const char *disabled = st_ddofx_field(&args);
	if (!disabled)
		return;
	int32_t v[9];
	for (unsigned i = 0; i < 9; ++i)
		if (!st_ddofx_field_number(ddo, &args, &v[i]))
			return;
	char *reply = st_ddofx_field(&args);
	if (!args || (unsigned)v[2] > 1 || (unsigned)v[3] > 1 || (unsigned)v[6] > 255 || (unsigned)v[7] > 255
	    || (unsigned)v[8] > 1)
		return;
	const struct st_ddofx_sprite *s = st_ddofx_find_sprite(g, on);
	if (!st_ddofx_sprite_fits(s, v[0], v[1]) || !st_ddofx_sprite_fits(st_ddofx_find_sprite(g, off), v[0], v[1])
	    || !st_ddofx_sprite_fits(st_ddofx_find_sprite(g, disabled), v[0], v[1])
	    || (kind == DDO_RADIO && (c.fx >= s->width || c.fy >= s->height)))
		return;
	strcpy(c.on, on);
	strcpy(c.off, off);
	strcpy(c.disabled, disabled);
	c.x         = v[0];
	c.y         = v[1];
	c.selected  = v[2];
	c.enabled   = v[3];
	c.exclusive = v[4];
	c.multiple  = v[5];
	c.key       = (uint16_t)((v[6] << 8) | v[7]);
	c.pass      = v[8];
	st_ddofx_callback_define(ddo, &c.callback, reply, args);
	c.callback.macro[95] = c.callback.reply[95] = 0;

	if (!g->choices[kind])
		g->choices[kind] = calloc(1, sizeof(*g->choices[kind]));
	struct st_ddofx_choices *table = g->choices[kind];
	if (!table)
		return;
	table->slots[slot - 1] = c;
	if (slot > table->count)
		table->count = slot;
	paint(g, kind, &c, WORK, false);
	if (g->mouse.buttons && !g->mouse.scan.waiting && !g->mouse.ignore_hold)
		g->mouse.scan.repeat_due = 1;
}

static void
toggle(struct st_ddofx_graphics *g, enum st_ddofx_group_kind kind, struct choice *c)
{
	if (c->exclusive) {
		if (c->selected)
			return;
		struct st_ddofx_choices *table = g->choices[kind];
		for (unsigned i = 0; i < table->count; ++i) {
			struct choice *peer = &table->slots[i];
			if (peer->selected && peer->exclusive == c->exclusive) {
				peer->selected = false;
				paint(g, kind, peer, VISUAL, true);
			}
		}
	}
	c->selected = !c->selected;
	paint(g, kind, c, VISUAL, true);
}

bool
st_ddofx_choices_consume_key(struct st_ddofx *ddo, unsigned slot, uint16_t word)
{
	if (!ddo->graphics || !word)
		return false;

	const enum st_ddofx_group_kind order[] = {DDO_CHECKBOX, DDO_RADIO};

	for (unsigned i = 0; i < 2; ++i) {
		enum st_ddofx_group_kind kind  = order[i];
		struct st_ddofx_choices  *table = ddo->graphics->choices[kind];
		if (!table || slot >= table->count)
			continue;
		struct choice *c = &table->slots[slot];
		if (c->enabled && c->key == word && st_ddofx_callback_enabled(ddo, &c->callback)) {
			toggle(ddo->graphics, kind, c);
			if (!c->pass)
				return true;
		}
	}
	return false;
}

bool
st_ddofx_choice_mouse(struct st_ddofx *ddo, enum st_ddofx_group_kind kind, unsigned slot, int x, int y)
{
	struct st_ddofx_choices *table = ddo->graphics->choices[kind];
	if (!table || slot >= table->count || !table->slots[slot].enabled)
		return false;
	struct choice                *c = &table->slots[slot];
	const struct st_ddofx_sprite *s = st_ddofx_find_sprite(ddo->graphics, c->on);
	if (!st_ddofx_callback_enabled(ddo, &c->callback) || !st_ddofx_sprite_fits(s, c->x, c->y) || x < c->x || x >= c->x + s->width || y > c->y
	    || y <= c->y - s->height)
		return false;
	toggle(ddo->graphics, kind, c);
	return true;
}

bool
st_ddofx_next_choice(struct st_ddofx *ddo, enum st_ddofx_group_kind kind, unsigned anchor, unsigned *next)
{
	struct st_ddofx_choices *table = ddo->graphics ? ddo->graphics->choices[kind] : NULL;
	if (!table || anchor > table->count)
		return false;
	const struct choice *target = &table->slots[anchor - 1];
	if (!target->on[0])
		return false;
	while (*next < table->count) {
		const struct choice *c = &table->slots[(*next)++];
		if (c->enabled && c->selected && st_ddofx_callback_enabled(ddo, &c->callback)
		    && (target == c || (target->exclusive && target->exclusive == c->exclusive)
		        || (target->multiple && target->multiple == c->multiple)))
			return st_ddofx_callback_start(ddo, &c->callback, DDO_EVENT);
	}
	return false;
}

bool
st_ddofx_choice_command(struct st_ddofx *ddo, const char *verb, char *args)
{
	enum {
		CREATE,
		EXECUTE,
		SWAP,
		ENABLE
	};

	static const struct {
		const char               *name;
		enum st_ddofx_group_kind kind;
		int                       op;
	} commands[] = {
	    {"5CRBUTTON", DDO_RADIO,    CREATE },
	    {"5EXRBTN",   DDO_RADIO,    EXECUTE},
	    {"5SWPRBTN",  DDO_RADIO,    SWAP   },
	    {"5DERBTN",   DDO_RADIO,    ENABLE },
	    {"5CCHKBOX",  DDO_CHECKBOX, CREATE },
	    {"5EXCBOX",   DDO_CHECKBOX, EXECUTE},
	    {"5SWPCBOX",  DDO_CHECKBOX, SWAP   },
	    {"5DECBOX",   DDO_CHECKBOX, ENABLE },
	};

	for (size_t i = 0; i < sizeof(commands) / sizeof(*commands); ++i) {
		if (ascii_case_compare(verb, commands[i].name))
			continue;
		int32_t slot, value;
		if (!st_ddofx_field_number(ddo, &args, &slot) || (unsigned)slot > CHOICE_COUNT)
			return true;
		enum st_ddofx_group_kind kind  = commands[i].kind;
		struct st_ddofx_choices  *table = ddo->graphics->choices[kind];
		if (commands[i].op == CREATE)
			define(ddo, kind, (unsigned)slot, args);
		else if (slot && table) {
			struct choice *c = &table->slots[slot - 1];
			switch (commands[i].op) {
				case EXECUTE:
					if (!args && c->on[0])
						st_ddofx_execute_group(ddo, kind, (unsigned)slot, false);
					break;
				case SWAP:
					if (st_ddofx_numbers(ddo, args, &value, 1) && value >= 1
					    && value <= CHOICE_COUNT) {
						struct choice tmp       = *c;
						*c                      = table->slots[value - 1];
						table->slots[value - 1] = tmp;
						if (table->count < (unsigned)value)
							table->count = (unsigned)value;
						if (table->count < (unsigned)slot)
							table->count = (unsigned)slot;
					}
					break;
				case ENABLE:
					if (c->on[0] && st_ddofx_numbers(ddo, args, &value, 1)
					    && (unsigned)value <= 1) {
						c->enabled = value;
						if (!value)
							c->selected = false;
						paint(ddo->graphics, kind, c, VISUAL, false);
					}
					break;
			}
		}
		return true;
	}
	return false;
}

/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>
#include <string.h>

#include "st_ddofx_graphics_internal.h"
#include "st_ddofx_script.h"
#include "string_util.h"

enum {
	FIELD_COUNT = 250,
	TEXT_MAX    = 70,
	AFFIX_MAX   = 25
};

struct text_field {
	struct st_ddofx_callback callback;
	unsigned                 owner, x, bottom;
	char                     text[TEXT_MAX], before[AFFIX_MAX], after[AFFIX_MAX];
	size_t                   text_length, before_length, after_length;
	uint8_t                  font[4096];
	unsigned                 limit, normal, selected, ink, filter, up, down;
	uint8_t                  mask;
	bool                     echo, caps, immediate, pass;
	int32_t                  group;
	uint16_t                 key;
};

struct st_ddofx_text_fields {
	struct text_field *slots[FIELD_COUNT];
	unsigned           order[FIELD_COUNT], count, highest;
	unsigned           active, cursor;
	bool               overwrite;
};

static struct st_ddofx_text_fields *
fields(const struct st_ddofx *ddo)
{
	return ddo->graphics ? ddo->graphics->text_fields : NULL;
}

void
st_ddofx_clear_text_fields(struct st_ddofx_graphics *g)
{
	struct st_ddofx_text_fields *t = g->text_fields;
	if (t) {
		for (unsigned i = 0; i < t->count; ++i)
			free(t->slots[t->order[i] - 1]);
		free(t);
		g->text_fields = NULL;
	}
}

unsigned
st_ddofx_text_field_count(const struct st_ddofx *ddo)
{
	const struct st_ddofx_text_fields *t = fields(ddo);
	return t ? t->highest : 0;
}

struct field_image {
	struct st_ddofx_graphics *g;
	const struct text_field  *f;
	int                       caret;
	unsigned                  owner;
};

static void
paint(void *context, const struct st_pixel_buffer *pixels, struct bounds clip)
{
	const struct field_image *image  = context;
	const struct text_field  *f      = image->f;
	struct st_ddofx_surface   target = st_ddofx_surface(image->g, image->owner, image->owner ? VISUAL : WORK);
	unsigned paper = image->caret < 0 ? f->normal : f->selected;
	for (int y = clip.top; y <= clip.bottom; ++y) {
		size_t at = (size_t)y * target.width + clip.left;
		if (pixels) {
			for (int x = clip.left; x <= clip.right; ++x, ++at)
				pixels->primary[at] = pixels->alternate[at] = paper;
		}
		else
			memset(target.data + at, (int)paper, clip.right - clip.left + 1);
	}
	size_t  length = f->text_length;
	uint8_t visible[TEXT_MAX];
	if (f->echo)
		memset(visible, f->mask, length);
	else
		memcpy(visible, f->text, length);
	st_ddofx_paint_vga_string(image->g, image->owner, pixels, f->font, visible, (unsigned)length, f->x,
	    f->bottom, f->ink, paper, &clip);
	if (image->caret >= 0 && (int)f->bottom >= clip.top && (int)f->bottom <= clip.bottom) {
		int left = (int)f->x + image->caret * image->g->cell_width;
		int end  = left + image->g->cell_width;
		for (int x = left < clip.left ? clip.left : left; x < end && x <= clip.right; ++x) {
			size_t at = (size_t)f->bottom * target.width + x;
			if (pixels)
				pixels->primary[at] = pixels->alternate[at] = 15;
			else
				target.data[at] = 15;
		}
	}
}

static void
draw(struct st_ddofx *ddo, unsigned id, bool work)
{
	struct st_ddofx_graphics    *g     = ddo->graphics;
	struct st_ddofx_text_fields *t     = fields(ddo);
	const struct text_field     *f     = t->slots[id - 1];
	unsigned                     owner = work && f->owner != g->active_graphic_window ? 0 : f->owner;
	struct field_image           image = {g, f, !work && t->active == id ? (int)t->cursor : -1, owner};
	struct bounds                area  = st_ddofx_text_bounds(g, owner, f->x, f->bottom, f->limit + 1, 1, 2);
	st_ddofx_draw_text_control(ddo, owner, work, area, paint, &image);
}

static size_t
copy_affix(const struct st_ddofx *ddo, const char *field, char dest[AFFIX_MAX])
{
	size_t length = st_ddofx_copy_value(ddo, field, dest, AFFIX_MAX);
	for (size_t i = 0; i < length; ++i)
		if (dest[i] == '~')
			dest[i] = ',';
	return length;
}

static void
define(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, unsigned id, char *args)
{
	const char *font = st_ddofx_field(&args), *text = st_ddofx_field(&args);
	const char *before = st_ddofx_field(&args), *after = st_ddofx_field(&args);
	int32_t     x, y, limit, normal, selected, ink, echo;
	if (!after || !st_ddofx_field_number(ddo, &args, &x) || !st_ddofx_field_number(ddo, &args, &y)
	    || !st_ddofx_field_number(ddo, &args, &limit) || !st_ddofx_field_number(ddo, &args, &normal)
	    || !st_ddofx_field_number(ddo, &args, &selected) || !st_ddofx_field_number(ddo, &args, &ink)
	    || !st_ddofx_field_number(ddo, &args, &echo))
		return;
	const char *mask = st_ddofx_field(&args);
	int32_t     n[9];
	for (unsigned i = 0; i < 9; ++i)
		if (!st_ddofx_field_number(ddo, &args, n + i))
			return;
	unsigned                owner = ddo->graphics->alert_graphic_window;
	struct st_ddofx_surface page  = st_ddofx_surface(ddo->graphics, owner, VISUAL);
	if (!args || x < 0 || x >= page.width || y < ddo->graphics->cell_height + 1 || y >= page.height
	    || limit < 1 || limit > TEXT_MAX || (unsigned)normal > 7 || (unsigned)selected > 7
	    || (unsigned)ink > 255 || (unsigned)echo > 1 || (unsigned)n[0] > FIELD_COUNT
	    || (unsigned)n[1] > FIELD_COUNT || (unsigned)n[2] > 1 || (unsigned)n[4] > 2 || (unsigned)n[5] > 1
	    || (unsigned)n[6] > 255 || (unsigned)n[7] > 255 || (unsigned)n[8] > 1)
		return;
	struct text_field *f = calloc(1, sizeof(*f));
	if (!f)
		return;
	font = st_ddofx_value(ddo, font);
	if (!st_ddofx_read_vga_font(sink, *font ? font : "IBMSTD.FNT", f->font)) {
		const uint8_t *current = st_ddofx_vga_bitmap(ddo);
		if (!current) {
			free(f);
			return;
		}
		memcpy(f->font, current, sizeof(f->font));
	}
	st_ddofx_callback_define(ddo, &f->callback, "", args, 100);
	f->before_length = copy_affix(ddo, before, f->before);
	f->after_length  = copy_affix(ddo, after, f->after);
	f->text_length   = st_ddofx_copy_value(ddo, text, f->text, sizeof(f->text));
	f->limit                       = (unsigned)limit;
	f->owner                       = owner;
	f->x                           = (unsigned)x;
	f->bottom                      = (unsigned)y;
	f->normal                      = (unsigned)normal;
	f->selected                    = (unsigned)selected;
	f->ink                         = (unsigned)ink;
	f->echo                        = echo != 0;
	f->mask                        = (uint8_t)*st_ddofx_value(ddo, mask);
	f->up                          = (unsigned)n[0];
	f->down                        = (unsigned)n[1];
	f->immediate                   = n[2] == 0;
	f->group                       = n[3];
	f->filter                      = (unsigned)n[4];
	f->caps                        = n[5] != 0;
	f->key                         = (uint16_t)((n[6] << 8) | n[7]);
	f->pass                        = n[8] != 0;
	struct st_ddofx_text_fields *t = fields(ddo);
	if (!t)
		ddo->graphics->text_fields = t = calloc(1, sizeof(*t));
	if (!t) {
		free(f);
		return;
	}
	if (!t->slots[id - 1])
		t->order[t->count++] = id;
	else
		free(t->slots[id - 1]);
	t->slots[id - 1] = f;
	if (id > t->highest)
		t->highest = id;
	if (t->active == id)
		t->active = 0;
	draw(ddo, id, true);
}

static void
finish_edit(struct st_ddofx *ddo)
{
	struct st_ddofx_text_fields *t = fields(ddo);
	if (!t->active)
		return;
	unsigned           id = t->active;
	struct text_field *f  = t->slots[id - 1];
	if (f->caps)
		for (size_t i = 0; i < f->text_length; ++i)
			f->text[i] = (char)ascii_to_upper((uint8_t)f->text[i]);
	t->active = 0;
	draw(ddo, id, false);
}

static void
activate(struct st_ddofx *ddo, unsigned id)
{
	struct st_ddofx_text_fields *t = fields(ddo);
	struct text_field           *f = t->slots[id - 1];
	if (!f || !st_ddofx_callback_enabled(ddo, &f->callback) || id == t->active)
		return;
	finish_edit(ddo);
	t->active    = id;
	t->cursor    = (unsigned)f->text_length;
	t->overwrite = false;
	draw(ddo, id, false);
}

bool
st_ddofx_text_field_hotkey(struct st_ddofx *ddo, unsigned slot, uint16_t word)
{
	struct st_ddofx_text_fields *t = fields(ddo);
	struct text_field           *f = t && slot < t->highest ? t->slots[slot] : NULL;
	if (!f || !word || (f->key != word && f->key != UINT16_MAX)
	    || !st_ddofx_callback_enabled(ddo, &f->callback))
		return false;
	activate(ddo, slot + 1);
	return !f->pass;
}

bool
st_ddofx_text_field_mouse(struct st_ddofx *ddo, int owner, int x, int y)
{
	struct st_ddofx_text_fields *t = fields(ddo);
	if (!t)
		return false;
	for (unsigned i = t->count; i > 0; --i) {
		unsigned               id = t->order[i - 1];
		struct text_field     *f  = t->slots[id - 1];
		if ((int)f->owner != owner)
			continue;
		struct bounds a =
		    st_ddofx_text_bounds(ddo->graphics, f->owner, f->x, f->bottom, f->limit + 1, 1, 2);
		if (x >= a.left && x <= a.right && y >= a.top && y <= a.bottom
		    && st_ddofx_callback_enabled(ddo, &f->callback)) {
			activate(ddo, id);
			return true;
		}
	}
	finish_edit(ddo);
	return false;
}

bool
st_ddofx_text_field_editing(const struct st_ddofx *ddo)
{
	const struct st_ddofx_text_fields *t = fields(ddo);
	return t && t->active && st_ddofx_callback_enabled(ddo, &t->slots[t->active - 1]->callback);
}

bool
st_ddofx_text_field_edit(struct st_ddofx *ddo, uint16_t word)
{
	if (!st_ddofx_text_field_editing(ddo))
		return false;
	struct st_ddofx_text_fields *t  = fields(ddo);
	unsigned                     id = t->active, ch = word & 255;
	struct text_field           *f    = t->slots[id - 1];
	char                        *text = f->text;
	unsigned                     len  = (unsigned)f->text_length;
	if (ch == 13 || ch == 27 || ch == 9 || word == 0x0f00 || word == 0x4800 || word == 0x5000) {
		if (ch == 27)
			f->text_length = 0;
		finish_edit(ddo);
		unsigned link = word == 0x0f00 || word == 0x4800 ? f->up : f->down;
		if (ch == 13 && f->immediate)
			st_ddofx_execute_group(ddo, DDO_TEXT_FIELD, id, true);
		else if (ch != 27 && link)
			activate(ddo, link);
		return true;
	}
	unsigned p = t->cursor;
	if (word == 0x4700)
		t->cursor = 0;
	else if (word == 0x4f00)
		t->cursor = len;
	else if (word == 0x4b00 && p)
		--t->cursor;
	else if (word == 0x4d00 && p < len)
		++t->cursor;
	else if (word == 0x5200)
		t->overwrite = !t->overwrite;
	else if (word == 0x5300 && p < len) {
		memmove(text + p, text + p + 1, len - p - 1);
		--f->text_length;
	}
	else if (ch == 8 && p) {
		memmove(text + (--t->cursor), text + p, len - p);
		--f->text_length;
	}
	else if (ch >= 32 && ch != 127) {
		unsigned upper = ascii_to_upper((uint8_t)ch);
		if ((f->filter == 1 && (upper < 'A' || upper > 'Z'))
		    || (f->filter == 2 && !ascii_is_digit((uint8_t)ch)))
			return true;
		if (t->overwrite && p < len)
			text[t->cursor++] = (char)ch;
		else if (len < f->limit) {
			memmove(text + p + 1, text + p, len - p);
			++f->text_length;
			text[t->cursor++] = (char)ch;
		}
	}
	draw(ddo, id, false);
	return true;
}

bool
st_ddofx_text_field_value(const struct st_ddofx *ddo, int32_t id, bool full,
    uint8_t out[ST_DDOFX_FIELD_REPLY_MAX], size_t *length)
{
	const struct st_ddofx_text_fields *t = fields(ddo);
	if (!t || id < 1 || (unsigned)id > t->highest || !t->slots[id - 1])
		return false;
	const struct text_field *f = t->slots[id - 1];
	size_t before = full ? f->before_length : 0;
	memcpy(out, f->before, before);
	memcpy(out + before, f->text, f->text_length);
	*length = before + f->text_length;
	if (full) {
		memcpy(out + *length, f->after, f->after_length);
		*length += f->after_length;
		*length = st_ddofx_decode_controls(out, (const char *)out, *length, true);
	}
	return true;
}

bool
st_ddofx_next_text_field(struct st_ddofx *ddo, unsigned anchor, unsigned *next)
{
	struct st_ddofx_text_fields *t = fields(ddo);
	if (!t || !t->slots[anchor - 1])
		return false;
	const struct text_field *target = t->slots[anchor - 1];
	while (*next < t->count) {
		unsigned                 id = t->order[(*next)++];
		const struct text_field *f  = t->slots[id - 1];
		if ((f == target || (f->group && f->group == target->group))
		    && st_ddofx_callback_enabled(ddo, &f->callback))
			return st_ddofx_field_callback(ddo, &f->callback, id);
	}
	return false;
}

bool
st_ddofx_text_field_command(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, const char *verb, char *args)
{
	bool create = !ascii_case_compare(verb, "5CTXTFLD"), show = !ascii_case_compare(verb, "5SHOWTFLD");
	bool swap = !ascii_case_compare(verb, "5SWPTFLD"), execute = !ascii_case_compare(verb, "5EXTFLD");
	if (!create && !show && !swap && !execute && ascii_case_compare(verb, "5ACTTFLD"))
		return false;
	int32_t id;
	if (!st_ddofx_field_number(ddo, &args, &id) || (unsigned)id > FIELD_COUNT)
		return true;
	if (create) {
		if (!id)
			st_ddofx_clear_text_fields(ddo->graphics);
		else
			define(ddo, sink, (unsigned)id, args);
		return true;
	}
	struct st_ddofx_text_fields *t = fields(ddo);
	if (!t || !id)
		return true;
	if (swap) {
		int32_t other;
		if (!st_ddofx_numbers(ddo, args, &other, 1) || other < 1 || other > FIELD_COUNT)
			return true;
		struct text_field *first = t->slots[id - 1], *second = t->slots[other - 1];
		if (!first || !second)
			for (unsigned i = 0; i < t->count; ++i) {
				if (t->order[i] == (unsigned)id)
					t->order[i] = (unsigned)other;
				else if (t->order[i] == (unsigned)other)
					t->order[i] = (unsigned)id;
			}
		t->slots[id - 1]    = second;
		t->slots[other - 1] = first;
		if (t->active == (unsigned)id)
			t->active = (unsigned)other;
		else if (t->active == (unsigned)other)
			t->active = (unsigned)id;
		if (t->highest < (unsigned)other && first)
			t->highest = (unsigned)other;
		if (t->highest < (unsigned)id && second)
			t->highest = (unsigned)id;
	}
	else if (!args && t->slots[id - 1]) {
		if (show)
			draw(ddo, (unsigned)id, true);
		else if (execute)
			st_ddofx_execute_group(ddo, DDO_TEXT_FIELD, (unsigned)id, false);
		else
			activate(ddo, (unsigned)id);
	}
	return true;
}

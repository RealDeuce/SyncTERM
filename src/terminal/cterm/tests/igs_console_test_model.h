/* Independent source-state and full-plane oracle for IGS console emissions. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef IGS_CONSOLE_TEST_MODEL_H
#define IGS_CONSOLE_TEST_MODEL_H
#include "igs_test_fixture.h"
#include "data/igs_console_color_glyphs.h"

struct model_cell { unsigned x,y,ch,fg,bg; };
struct model {
	unsigned state,x,y,row,columns,rows,count;
	uint16_t fg,bg;
	bool reverse,wrap;
	bool saved;
	unsigned saved_x,saved_y;
	uint16_t backgrounds[25];
	struct model_cell cells[4];
};

static void
model_down(struct model *m)
{
	if (m->y+1 < m->rows) { ++m->y; return; }
	unsigned kept = 0;
	for (unsigned i = 0; i < m->count; ++i) if (m->cells[i].y != 0) {
		m->cells[i].y--; m->cells[kept++] = m->cells[i];
	}
	m->count = kept;
	for (unsigned row = 1; row < m->rows; ++row) m->backgrounds[row-1] = m->backgrounds[row];
	m->backgrounds[m->rows-1] = m->bg;
}

/* Source-derived six-state oracle, independent of the production dispatch,
 * parameter accumulator, text placement, font decoder and pixel backend. */
static void
model_byte(struct model *m, uint8_t byte)
{
	unsigned state = m->state; m->state = 0;
	if (state == 1) {
		if (byte == 'Y') m->state = 2;
		else if (byte == 'b') m->state = 4;
		else if (byte == 'c') m->state = 5;
		else if (byte == 'A' && m->y != 0) --m->y;
		else if (byte == 'B' && m->y+1 < m->rows) ++m->y;
		else if (byte == 'C' && m->x+1 < m->columns) ++m->x;
		else if (byte == 'D' && m->x != 0) --m->x;
		else if (byte == 'H') m->x = m->y = 0;
		else if (byte == 'p') m->reverse = true;
		else if (byte == 'q') m->reverse = false;
		else if (byte == 'j') {
			m->saved_x = m->x;
			m->saved_y = m->y;
			m->saved = true;
		} else if (byte == 'k') {
			m->x = m->saved ? m->saved_x : 0;
			m->y = m->saved ? m->saved_y : 0;
			m->saved = false;
		}
		return;
	}
	if (state == 2) { m->row = (byte+65536-32)%65536; if (m->row >= m->rows) m->row = m->rows-1; m->state = 3; return; }
	if (state == 3) { m->x = (byte+65536-32)%65536; if (m->x >= m->columns) m->x = m->columns-1; m->y = m->row; return; }
	if (state == 4) { m->fg = (uint16_t)(byte-32); return; }
	if (state == 5) { m->bg = (uint16_t)(byte-32); return; }
	if (byte == 27) { m->state = 1; return; }
	if (byte == 8) { if (m->x != 0) --m->x; return; }
	if (byte == 9) { unsigned next = (m->x & ~7u)+8; m->x = next < m->columns ? next : m->columns-1; return; }
	if (byte >= 10 && byte <= 12) { model_down(m); return; }
	if (byte == 13) { m->x = 0; return; }
	if (byte < 32) return;
	unsigned index = 0;
	while (index < m->count && !(m->cells[index].x == m->x && m->cells[index].y == m->y)) ++index;
	if (index == m->count) { CHECK(m->count < 4); ++m->count; }
	m->cells[index] = (struct model_cell){m->x,m->y,byte,m->reverse ? m->bg : m->fg,m->reverse ? m->fg : m->bg};
	if (m->x+1 < m->columns) ++m->x;
	else if (m->wrap) { m->x = 0; model_down(m); }
}

static void
model_pixels(struct fixture *f, unsigned resolution, const struct model *m)
{
	struct cterm_igs *igs = f->terminal->igs_engine;
	unsigned ch = resolution == 2 ? 16 : 8, mask = cterm_igs_plane_mask(resolution);
	const struct st_console_rect full = {0,0,igs->width,igs->height};
	struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f->console,&full,false) == 0);
	/* Build the independent frame by rows and sparse cells. Testing each cell
	 * against every pixel repeats billions of irrelevant coordinate checks. */
	size_t count = (size_t)igs->width*igs->height, drawable = (size_t)igs->width*ch*m->rows;
	uint32_t *expected = malloc(count*sizeof(*expected)); CHECK(expected != NULL);
	for (unsigned y = 0; y < igs->height; ++y) {
		uint32_t value = y/ch < m->rows ? 768+(m->backgrounds[y/ch] & mask) : 769;
		for (unsigned x = 0; x < igs->width; ++x) expected[(size_t)y*igs->width+x] = value;
	}
	for (unsigned i = 0; i < m->count; ++i) {
		const struct model_cell *cell = &m->cells[i];
		for (unsigned y = 0; y < ch; ++y) for (unsigned x = 0; x < 8; ++x) {
			bool ink = (console_source_rows[resolution == 2][cell->ch][y] & (128u >> x)) != 0;
			expected[((size_t)cell->y*ch+y)*igs->width+cell->x*8+x] = 768+((ink ? cell->fg : cell->bg) & mask);
		}
	}
	CHECK(memcmp(b->primary,expected,count*sizeof(*expected)) == 0);
	for (size_t i = drawable; i < count; ++i) expected[i] = 768;
	CHECK(memcmp(b->alternate,expected,count*sizeof(*expected)) == 0);
	free(expected);
	st_pixel_buffer_destroy(b);
}
#endif

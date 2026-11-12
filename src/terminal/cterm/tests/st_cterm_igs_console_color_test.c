/* c: native console colors, separate from C graphical pens. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "igs_test_fixture.h"
#include "cterm_vt52.h"

static void
literal(void)
{
	static const uint8_t a8[8] = {0x18,0x3c,0x66,0x66,0x7e,0x66,0x66,0};
	static const uint8_t a16[16] = {0,0,0x18,0x3c,0x7e,0x66,0x66,0x66,0x7e,0x7e,0x66,0x66,0x66,0x66,0,0};
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f,resolution,false); struct cterm_igs *igs = f.terminal->igs_engine;
		unsigned height = resolution == 2 ? 16 : 8, mask = cterm_igs_plane_mask(resolution);
		const struct st_console_rect area = {0,0,8,height}; seed(&f,&area,768,768);
		struct cterm_igs_attributes before = igs->attributes; uint32_t palette[16]; memcpy(palette,igs->palette,sizeof(palette));
		for (unsigned reverse = 0; reverse < 2; ++reverse) for (unsigned fg = 0; fg < 16; ++fg) for (unsigned bg = 0; bg < 16; ++bg) {
			char wire[96]; (void)snprintf(wire,sizeof(wire),"\033f\033q\033HG#g>1:c>1,%u:c 0,%u:%sA",fg,bg,reverse ? "\033p" : ""); feed(&f,wire);
			unsigned foreground = reverse ? bg : fg, background = reverse ? fg : bg;
			CHECK(f.terminal->fg_color == 768+(foreground & mask) && f.terminal->bg_color == 768+(background & mask));
			CHECK(f.terminal->vt52_foreground == (uint16_t)(fg-32) && f.terminal->vt52_background == (uint16_t)(bg-32));
			CHECK(memcmp(&before,&igs->attributes,sizeof(before)) == 0 && memcmp(palette,igs->palette,sizeof(palette)) == 0);
			struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f.console,&area,false) == 0);
			for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < 8; ++x) {
				unsigned expected = 768+((((height == 16 ? a16[y] : a8[y]) & (128u >> x)) ? foreground : background) & mask);
				if (b->primary[y*8+x] != expected || b->alternate[y*8+x] != expected)
					fprintf(stderr,"literal c res%u reverse%u fg%u bg%u xy%u,%u: %u/%u != %u\n",resolution,reverse,fg,bg,x,y,b->primary[y*8+x],b->alternate[y*8+x],expected);
				CHECK(b->primary[y*8+x] == expected && b->alternate[y*8+x] == expected);
			}
			const struct st_console_rect one = {0,0,1,1}; struct st_console_cell cell;
			CHECK(st_console_read_cells(f.console,&one,&cell,1) == 0 && cell.glyph == 'A' && !(cell.flags & ST_CONSOLE_CELL_BLINK));
			CHECK(f.terminal->xpos == 2 && f.terminal->ypos == 1 && igs->draw_x == 0 && igs->draw_y == 0);
			st_pixel_buffer_destroy(b);
		}
		destroy(&f);
	}
}

static void
words(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f,resolution,false); struct cterminal *t = f.terminal; struct cterm_igs *igs = t->igs_engine;
		feed(&f,"\033fG#g 1:");
		igs->draw_start_x = 17; igs->draw_start_y = 23; igs->draw_x = 37; igs->draw_y = 47;
		struct cterm_igs_attributes before = igs->attributes;
		uint32_t palette[16]; memcpy(palette,igs->palette,sizeof(palette));
		const struct st_console_rect full = {0,0,igs->width,igs->height}; seed(&f,&full,769,768);
		unsigned mask = cterm_igs_plane_mask(resolution);
		for (unsigned reverse = 0; reverse < 2; ++reverse) for (unsigned role = 0; role < 3; ++role)
			for (int word = INT16_MIN; word <= INT16_MAX; ++word) {
				int selector = role == 0 ? word : (int)role-1, color = role == 0 ? 51 : word;
				t->accumulator = t->saved_accumulator = NULL; t->sequence = 0;
				t->vt52_foreground = 0x1234; t->vt52_background = 0xabcd;
				t->negative = reverse != 0; t->attr = reverse ? 0x4d : 0xd4;
				t->fg_color = 768+((reverse ? 13u : 4u) & mask); t->bg_color = 768+((reverse ? 4u : 13u) & mask);
				igs->math[0] = (int16_t)selector; igs->math[1] = (int16_t)color;
				uint32_t final = literal_words(igs, (int[]){selector, color}, 2);
				feed(&f,"G#c 0],1]:");
				uint16_t fg = 0x1234, bg = 0xabcd, raw = (uint16_t)((uint8_t)color-32);
				if (selector == 1) fg = raw;
				else if (selector >= 0) bg = raw;
				CHECK(t->vt52_foreground == fg && t->vt52_background == bg);
				CHECK(t->fg_color == 768+((reverse ? bg : fg) & mask) && t->bg_color == 768+((reverse ? fg : bg) & mask));
				CHECK(t->negative == (reverse != 0) && (t->accumulator != NULL) == (selector < 0));
				CHECK(igs->math[0] == selector && igs->math[1] == color && igs->random_seed == final && igs->pattern_seed == 0);
				CHECK(igs->input_deadline == 0 && igs->text_deadline == 0 && t->xpos == 1 && t->ypos == 1);
			}
		CHECK(memcmp(&before,&igs->attributes,sizeof(before)) == 0 && memcmp(palette,igs->palette,sizeof(palette)) == 0);
		CHECK(igs->draw_start_x == 17 && igs->draw_start_y == 23 && igs->draw_x == 37 && igs->draw_y == 47 && igs->last_line);
		struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f.console,&full,false) == 0);
		for (size_t i = 0; i < (size_t)igs->width*igs->height; ++i) CHECK(b->primary[i] == 769 && b->alternate[i] == 768);
		st_pixel_buffer_destroy(b); destroy(&f);
	}
}

/* Literal rows from the original TOS system forms, independent of rendering. */
static const uint8_t console_glyphs[2][4][16] = {
	{{0x18,0x3c,0x66,0x66,0x7e,0x66,0x66,0},
	 {0x60,0x60,0x7c,0x66,0x66,0x66,0x7c,0},
	 {0,0,0x3c,0x60,0x60,0x60,0x3c,0},
	 {0,0x3c,0x66,0x60,0x66,0x3c,0x08,0x38}},
	{{0,0,0x18,0x3c,0x7e,0x66,0x66,0x66,0x7e,0x7e,0x66,0x66,0x66,0x66,0,0},
	 {0,0,0x60,0x60,0x60,0x7c,0x7e,0x66,0x66,0x66,0x66,0x66,0x7e,0x7c,0,0},
	 {0,0,0,0,0,0x3c,0x7c,0x60,0x60,0x60,0x60,0x60,0x7e,0x3e,0,0},
	 {0,0,0x3c,0x7e,0x66,0x66,0x60,0x60,0x60,0x60,0x66,0x66,0x7e,0x3c,0x0c,0x38}}
};
static unsigned glyph_index(unsigned ch)
{
	return ch == 'A' ? 0 : ch == 'b' ? 1 : ch == 'c' ? 2 : 3;
}
struct model_cell { unsigned x,y,ch,fg,bg; };
struct model {
	unsigned state,x,y,row,columns,rows,count;
	uint16_t fg,bg;
	bool reverse,wrap;
	uint16_t row_background[25];
	struct model_cell cells[3];
};
/* Six-state source contract: Normal,ESC,Y-row,Y-column,FG,BG. This oracle
 * has no dispatch table, host Unicode decoder, palette map or pixel backend. */
static void
model_scroll(struct model *m)
{
	unsigned kept = 0;
	for (unsigned i = 0; i < m->count; ++i) if (m->cells[i].y != 0) {
		m->cells[i].y--; m->cells[kept++] = m->cells[i];
	}
	m->count = kept;
	for (unsigned row = 1; row < m->rows; ++row) m->row_background[row-1] = m->row_background[row];
	m->row_background[m->rows-1] = m->bg;
}

static void
model_byte(struct model *m, uint8_t byte)
{
	unsigned state = m->state;
	m->state = 0;
	if (state == 1) { if (byte == 'b') m->state = 4; else if (byte == 'c') m->state = 5; return; }
	if (state == 2) { m->row = (uint16_t)(byte-32); if (m->row >= m->rows) m->row = m->rows-1; m->state = 3; return; }
	if (state == 3) { m->x = (uint16_t)(byte-32); if (m->x >= m->columns) m->x = m->columns-1; m->y = m->row; return; }
	if (state == 4) { m->fg = (uint16_t)(byte-32); return; }
	if (state == 5) { m->bg = (uint16_t)(byte-32); return; }
	if (byte == 27) { m->state = 1; return; }
	if (byte == 8) { if (m->x != 0) --m->x; return; }
	if (byte == 9) { unsigned next = (m->x & ~7u)+8; m->x = next < m->columns ? next : m->columns-1; return; }
	if (byte >= 10 && byte <= 12) { if (m->y+1 < m->rows) ++m->y; else model_scroll(m); return; }
	if (byte == 13) { m->x = 0; return; }
	if (byte < 32) return;
	unsigned index = 0;
	while (index < m->count && !(m->cells[index].x == m->x && m->cells[index].y == m->y)) ++index;
	if (index == m->count) { CHECK(m->count < 3); ++m->count; }
	m->cells[index] = (struct model_cell){m->x,m->y,byte,m->reverse ? m->bg : m->fg,m->reverse ? m->fg : m->bg};
	if (m->x+1 < m->columns) ++m->x;
	else if (m->wrap) { m->x = 0; if (m->y+1 < m->rows) ++m->y; else model_scroll(m); }
}

static void
prior_states(void)
{
	static const char *prefixes[] = {"","\033","\033Y","\033Y!","\033b","\033c"};
	static const int selectors[] = {-1,0,1,2};
	static const uint8_t colors[] = {0,7,8,9,10,11,12,13,27,'A',128};
	for (unsigned resolution = 0; resolution < 3; ++resolution) for (unsigned reserved = 0; reserved < 2; ++reserved) {
		struct fixture f; create(&f,resolution,reserved != 0); struct cterminal *t = f.terminal; struct cterm_igs *igs = t->igs_engine;
		unsigned ch = resolution == 2 ? 16 : 8, mask = cterm_igs_plane_mask(resolution);
		const struct st_console_rect status = {0,24*ch,igs->width,ch};
		if (reserved) seed(&f,&status,769,768);
		for (unsigned state = 0; state < 7; ++state) for (unsigned reverse = 0; reverse < 2; ++reverse)
			for (unsigned selected = 0; selected < 4; ++selected) for (unsigned color = 0; color < sizeof(colors)/sizeof(*colors); ++color) {
				t->accumulator = t->saved_accumulator = NULL; t->sequence = 0;
				feed(&f,"\033q\033f\033wG#c>1,15:c 0,0:\033E\033Y\042\043");
				if (reverse) feed(&f,"\033p");
				if (state == 6) {
					feed(&f,"\033v");
					char bottom[] = {27,'Y',(char)(32+t->height-1),0}; feed(&f,bottom);
				} else feed(&f,prefixes[state]);
				/* BIOS output ignores the wire presentation's LF expansion. */
				t->lf_expand = true;
				struct model m = {.state = state == 6 ? 3 : state,.x = 3,.y = 2,.row = 1,.columns = (unsigned)t->width,
				    .rows = (unsigned)t->height,.fg = (uint16_t)(15-32),.bg = (uint16_t)(0-32),.reverse = reverse != 0,.wrap = state == 6};
				if (state == 6) m.row = m.rows-1;
				model_byte(&m,27);
				if (selectors[selected] >= 0) { model_byte(&m,selectors[selected] == 1 ? 'b' : 'c'); model_byte(&m,colors[color]); }
				igs->math[0] = (int16_t)selectors[selected]; igs->math[1] = colors[color]; feed(&f,"G#c 0],1]:");
				CHECK(t->vt52_foreground == m.fg && t->vt52_background == m.bg && t->negative == m.reverse);
				if (t->xpos != (int)m.x+1 || t->ypos != (int)m.y+1)
					fprintf(stderr,"prior c res%u status%u state%u reverse%u selector%d color%u: xy%d,%d != %u,%u\n",resolution,reserved,state,reverse,selectors[selected],colors[color],t->xpos,t->ypos,m.x+1,m.y+1);
				CHECK(t->xpos == (int)m.x+1 && t->ypos == (int)m.y+1);
				CHECK((t->accumulator != NULL) == (m.state != 0));
				CHECK((t->sequence != 0) == (m.state == 1));
				CHECK(t->seq_trailing_remain == (m.state == 3 ? 1 : 0));
				const struct st_console_rect area = {0,0,igs->width,(unsigned)t->height*ch}; struct st_pixel_buffer *b = NULL;
				CHECK(st_console_get_pixels(&b,f.console,&area,false) == 0);
				for (unsigned y = 0; y < area.height; ++y) for (unsigned x = 0; x < area.width; ++x) {
					unsigned value = m.row_background[y/ch] & mask;
					for (unsigned i = 0; i < m.count; ++i) {
						const struct model_cell *cell = &m.cells[i];
						if (x/8 != cell->x || y/ch != cell->y) continue;
						uint8_t bits = console_glyphs[resolution == 2][glyph_index(cell->ch)][y%ch];
						value = ((bits & (128u >> (x%8))) ? cell->fg : cell->bg) & mask;
					}
					if (b->primary[y*area.width+x] != 768+value || b->alternate[y*area.width+x] != 768+value)
						fprintf(stderr,"prior c state%u selector%d color%u reverse%u xy%u,%u\n",state,selectors[selected],colors[color],reverse,x,y);
					CHECK(b->primary[y*area.width+x] == 768+value && b->alternate[y*area.width+x] == 768+value);
				}
				st_pixel_buffer_destroy(b);
			}
		if (reserved) {
			struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f.console,&status,false) == 0);
			for (size_t i = 0; i < (size_t)status.width*status.height; ++i) CHECK(b->primary[i] == 769 && b->alternate[i] == 768);
			st_pixel_buffer_destroy(b);
		}
		destroy(&f);
	}
}

static void
no_reply(const char *bytes, size_t size, void *context)
{
	(void)bytes; (void)context; CHECK(size == 0);
}

static void
partitions(void)
{
	static const char *wires[] = {"AG#c>1,2:c>0,3:M 2:B","AG#g>1:c>r,R:M 2:B","AG#c>0],r:M 2:B"};
	for (unsigned resolution = 0; resolution < 3; ++resolution) for (unsigned which = 0; which < 3; ++which)
		for (size_t split = 0; split <= strlen(wires[which])+1; ++split) {
			struct fixture f; create(&f,resolution,false); struct cterm_igs *igs = f.terminal->igs_engine;
			f.terminal->response_cb = no_reply; igs->math[0] = -1;
			igs->random_range[0][1] = igs->random_range[1][1] = 255;
			size_t used, size = strlen(wires[which]);
			if (split > size) for (size_t i = 0; i < size; ++i) CHECK(st_cterm_write(f.terminal,wires[which]+i,1,&used) == 0 && used == 1);
			else {
				CHECK(st_cterm_write(f.terminal,wires[which],split,&used) == 0 && used == split);
				CHECK(st_cterm_write(f.terminal,wires[which]+split,size-split,&used) == 0 && used == size-split);
			}
			CHECK(igs->random_seed == (which == 0 ? 1 : which == 1 ? UINT32_C(0x5eb5ca17) : UINT32_C(0xbb40e62e)));
			CHECK(igs->attributes.writing_mode == 2 && igs->draw_x == 0 && igs->draw_y == 0 && igs->last_line);
			const struct st_console_rect area = {0,0,2,1}; struct st_console_cell cells[2];
			CHECK(st_console_read_cells(f.console,&area,cells,2) == 0 && cells[0].glyph == 'A');
			CHECK(cells[1].glyph == (which == 2 ? 32u : (unsigned)'B'));
			CHECK(f.terminal->xpos == (which == 2 ? 2 : 3) && f.terminal->ypos == (which == 2 ? 2 : 1));
			CHECK(igs->input_deadline == 0 && f.terminal->accumulator == NULL);
			destroy(&f);
		}
}

static void
lifetime(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f,resolution,false); struct cterminal *t = f.terminal; struct cterm_igs *igs = t->igs_engine;
		t->response_cb = no_reply; feed(&f,"\033fG#c>1,51:c 0,48:");
		CHECK(t->vt52_foreground == 19 && t->vt52_background == 16);
		const char *partial[] = {"G#c ","G#c 1,","G#c r,","G#c 0],"};
		for (size_t i = 0; i < sizeof(partial)/sizeof(*partial); ++i) {
			feed(&f,partial[i]); CHECK(t->vt52_foreground == 19 && t->vt52_background == 16 && igs->random_seed == 1);
			CHECK(st_cterm_finish(t) == 0 && t->accumulator == NULL);
			feed(&f,partial[i]); local_ms += 5001; CHECK(st_cterm_poll(t) == 0);
			CHECK(t->vt52_foreground == 19 && t->vt52_background == 16 && igs->random_seed == 1 && t->accumulator == NULL);
		}
		size_t used; const char bad[] = "G#c r,10000:";
		CHECK(st_cterm_write(t,bad,sizeof(bad)-1,&used) == 0);
		CHECK(t->vt52_foreground == 19 && t->vt52_background == 16 && igs->random_seed == 1);
		feed(&f,"G#I>0:I>1:I 2:"); CHECK(t->vt52_foreground == 19 && t->vt52_background == 16);
		/* An unchanged R and palette-only I retain console selection/state. */
		char wire[64]; (void)snprintf(wire,sizeof(wire),"G#R %u,0:",resolution); feed(&f,wire);
		CHECK(t->vt52_foreground == 19 && t->vt52_background == 16);
		/* Native console output is independent of containing-loop flow gates. */
		igs->loop_mode = 1; feed(&f,"G#c 1,7:"); CHECK(t->vt52_foreground == (uint16_t)(7-32)); igs->loop_mode = 0;
		feed(&f,"\033"); feed(&f,"G#c 1,"); CHECK(st_cterm_finish(t) == 0 && t->accumulator != NULL);
		feed(&f,"G#I 1:"); CHECK(t->accumulator != NULL); /* Incomplete numeric command emitted nothing. */
		CHECK(st_cterm_reset_terminal(t) == 0 && t->accumulator == NULL);
		CHECK(t->vt52_foreground == 15 && t->vt52_background == 0 && !t->negative);
		feed(&f,"\033b"); CHECK(t->accumulator != NULL);
		CHECK(st_cterm_reset_terminal(t) == 0 && t->accumulator == NULL);
		feed(&f,"G#c 1,3:"); CHECK(t->vt52_foreground == (uint16_t)(3-32) && t->xpos == 1 && t->ypos == 1);
		feed(&f,resolution == 0 ? "G#R 1,0:" : "G#R 0,0:");
		if (cterm_igs_paused(igs)) { local_ms += 17; CHECK(st_cterm_poll(t) == 0); }
		CHECK(t->vt52_foreground == (resolution == 2 ? (uint16_t)(3-32) : 15) && t->vt52_background == 0);
		destroy(&f);
	}
}

static void
output_failure(void)
{
	struct fixture f; create(&f,0,false); struct cterminal *t = f.terminal;
	t->screen.codepage = ST_CODEPAGE_CP437;
	t->screen.error = IO_ERROR_IO; t->lf_expand = true;
	const uint8_t bytes[] = {27,'b',128};
	CHECK(cterm_vt52_output(t,bytes,sizeof(bytes)) == IO_ERROR_IO);
	CHECK(t->screen.codepage == ST_CODEPAGE_CP437 && t->lf_expand && t->vt52_foreground == 15);
	CHECK(t->accumulator != NULL); /* The completed first ESC survives the error. */
	CHECK(st_cterm_reset_terminal(t) == 0 && t->accumulator == NULL);
	feed(&f,"G#c 1,128:"); CHECK(t->vt52_foreground == 96 && t->screen.codepage == ST_CODEPAGE_CP437);
	destroy(&f);
}

#include "data/igs_console_color_glyphs.h"

static void
printable_bytes(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution) for (unsigned encoding = 0; encoding < 2; ++encoding) {
		struct fixture f; create(&f,resolution,false); struct cterminal *t = f.terminal;
		t->wire_encoding = encoding ? ST_CTERM_ENCODING_UTF8 : ST_CTERM_ENCODING_CODEPAGE;
		t->screen.codepage = encoding ? ST_CODEPAGE_CP437 : ST_CODEPAGE_ATARI_ST;
		enum st_codepage saved = t->screen.codepage;
		unsigned height = resolution == 2 ? 16 : 8, mask = cterm_igs_plane_mask(resolution);
		const struct st_console_rect area = {24,2*height,16,height};
		for (unsigned reverse = 0; reverse < 2; ++reverse) for (unsigned byte = 32; byte < 256; ++byte) {
			feed(&f,"\033q\033f\033wG#c>1,15:c 0,0:\033E\033Y\042\043");
			if (reverse) feed(&f,"\033p");
			feed(&f,"\033b"); t->igs_engine->math[0] = (int16_t)byte; feed(&f,"G#c 1,0]:");
			if (t->screen.codepage != saved || t->xpos != 6 || t->ypos != 3)
				fprintf(stderr,"native console byte%u res%u encoding%u reverse%u: cursor%d,%d codepage%d expected6,3/%d\n",byte,resolution,encoding,reverse,t->xpos,t->ypos,t->screen.codepage,saved);
			CHECK(t->screen.codepage == saved && t->xpos == 6 && t->ypos == 3);
			struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f.console,&area,false) == 0);
			for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < 16; ++x) {
				uint8_t row = console_source_rows[resolution == 2][x < 8 ? 'b' : byte][y];
				bool ink = (row & (128u >> (x%8))) != 0;
				unsigned value = 768+((ink != (reverse != 0) ? 11u : 0u) & mask);
				if (b->primary[y*16+x] != value || b->alternate[y*16+x] != value)
					fprintf(stderr,"native console byte%u res%u encoding%u reverse%u xy%u,%u: %u/%u != %u\n",byte,resolution,encoding,reverse,x,y,b->primary[y*16+x],b->alternate[y*16+x],value);
				CHECK(b->primary[y*16+x] == value && b->alternate[y*16+x] == value);
			}
			st_pixel_buffer_destroy(b);
		}
		destroy(&f);
	}
}

int
st_cterm_igs_console_color_test_main(void)
{
	literal(); printable_bytes(); words(); prior_states(); partitions(); lifetime(); output_failure(); return 0;
}

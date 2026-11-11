/* m: repeated BIOS cursor bytes through the existing VT52 parser. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "igs_console_test_model.h"
#include "cterm_internal.h"

static void
no_reply(const char *bytes, size_t size, void *context)
{
	(void)bytes; (void)context; CHECK(size == 0);
}

static void
normal_case(struct fixture *f, int selector, int count, unsigned x, unsigned y)
{
	struct cterminal *t = f->terminal; struct cterm_igs *igs = t->igs_engine;
	t->accumulator = t->saved_accumulator = NULL; t->sequence = 0;
	cterm_set_position(t,(int)x+1,(int)y+1);
	CHECK(t->xpos == (int)x+1 && t->ypos == (int)y+1);
	igs->math[0] = (int16_t)selector; igs->math[1] = (int16_t)count;
	feed(f,"G#m 0],1]:");
	/* Algebraic normal-state oracle; no production console dispatch calls. */
	unsigned n = count > 0 ? (unsigned)count : 1;
	int kind = selector > 4 ? 0 : selector;
	if (kind == 0) x = y = 0;
	else if (kind == 1) y = n > y ? 0 : y-n;
	else if (kind == 2) y = n >= (unsigned)t->height-y ? (unsigned)t->height-1 : y+n;
	else if (kind == 3) x = n >= (unsigned)t->width-x ? (unsigned)t->width-1 : x+n;
	else if (kind == 4) x = n > x ? 0 : x-n;
	if (t->xpos != (int)x+1 || t->ypos != (int)y+1)
		fprintf(stderr,"m normal res%u %dx%d g%d selector%d count%d: cursor%d,%d != %u,%u\n",
		    igs->resolution,t->width,t->height,igs->scaling_mode,selector,count,t->xpos,t->ypos,x+1,y+1);
	CHECK(t->xpos == (int)x+1 && t->ypos == (int)y+1);
	CHECK((t->accumulator != NULL) == (kind < 0 && (n & 1) != 0));
	CHECK(igs->math[0] == selector && igs->math[1] == count && igs->random_seed == 1 && igs->pattern_seed == 0);
	CHECK(igs->input_deadline == 0 && igs->text_deadline == 0 && igs->wait_deadline == 0);
}

static void
normal(void)
{
	static const int selectors[] = {INT16_MIN,-1,0,1,2,3,4,5,INT16_MAX};
	static const int counts[] = {INT16_MIN,-1,0,1,2,3,24,25,80,256};
	for (unsigned resolution = 0; resolution < 3; ++resolution) for (unsigned reserved = 0; reserved < 2; ++reserved) {
		struct fixture f; create(&f,resolution,reserved != 0); struct cterminal *t = f.terminal; struct cterm_igs *igs = t->igs_engine;
		t->response_cb = no_reply; feed(&f,"\033f");
		igs->draw_start_x = 17; igs->draw_start_y = 23; igs->draw_x = 37; igs->draw_y = 47;
		struct cterm_igs_attributes before = igs->attributes; uint32_t palette[16]; memcpy(palette,igs->palette,sizeof(palette));
		const struct st_console_rect full = {0,0,igs->width,igs->height}; seed(&f,&full,769,768);
		for (unsigned scaling = 0; scaling < 3; ++scaling) {
			char setup[32]; (void)snprintf(setup,sizeof(setup),"G#g %u:",scaling); feed(&f,setup);
			for (size_t i = 0; i < sizeof(selectors)/sizeof(*selectors); ++i)
				for (size_t j = 0; j < sizeof(counts)/sizeof(*counts); ++j) for (unsigned origin = 0; origin < 3; ++origin)
					normal_case(&f,selectors[i],counts[j],origin == 0 ? 3 : origin == 1 ? 0 : (unsigned)t->width-1,
					    origin == 0 ? 2 : origin == 1 ? 0 : (unsigned)t->height-1);
			if (resolution == 0 && reserved == 0 && scaling == 0) {
				/* Selector classification is shared, so exhaust it once. */
				for (int word = INT16_MIN; word <= INT16_MAX; ++word) normal_case(&f,word,1,3,2);
				for (int kind = -1; kind <= 5; ++kind) normal_case(&f,kind,INT16_MAX,3,2);
				normal_case(&f,-1,INT16_MAX-1,3,2);
			}
		}
		CHECK(memcmp(&before,&igs->attributes,sizeof(before)) == 0 && memcmp(palette,igs->palette,sizeof(palette)) == 0);
		CHECK(igs->draw_start_x == 17 && igs->draw_start_y == 23 && igs->draw_x == 37 && igs->draw_y == 47 && igs->last_line);
		CHECK(t->vt52_foreground == 15 && t->vt52_background == 0 && !t->negative);
		struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f.console,&full,false) == 0);
		for (size_t i = 0; i < (size_t)igs->width*igs->height; ++i) CHECK(b->primary[i] == 769 && b->alternate[i] == 768);
		st_pixel_buffer_destroy(b); destroy(&f);
	}
}

static void
compositions(void)
{
	static const char *prefixes[] = {"","\033","\033Y","\033Y!","\033b","\033c"};
	static const int counts[] = {0,2,3};
	static const uint8_t letters[] = {'H','A','B','C','D'};
	for (unsigned resolution = 0; resolution < 3; ++resolution) for (unsigned reserved = 0; reserved < 2; ++reserved) {
		struct fixture f; create(&f,resolution,reserved != 0); struct cterminal *t = f.terminal; struct cterm_igs *igs = t->igs_engine;
		t->response_cb = no_reply; unsigned ch = resolution == 2 ? 16 : 8;
		if (reserved) { const struct st_console_rect status = {0,24*ch,igs->width,ch}; seed(&f,&status,769,768); }
		for (unsigned state = 0; state < 7; ++state) for (unsigned reverse = 0; reverse < 2; ++reverse)
			for (int selector = -1; selector <= 5; ++selector) for (size_t c = 0; c < sizeof(counts)/sizeof(*counts); ++c) {
				t->accumulator = t->saved_accumulator = NULL; t->sequence = 0;
				feed(&f,"\033q\033f\033wG#g>1:c>1,15:c 0,0:\033EG#c 0,2:\033Y\042\043");
				if (reverse) feed(&f,"\033p");
				if (state == 6) { feed(&f,"\033v"); char bottom[] = {27,'Y',(char)(32+t->height-1),0}; feed(&f,bottom); }
				else feed(&f,prefixes[state]);
				struct model m = {.state = state == 6 ? 3 : state,.x = 3,.y = 2,.row = state == 6 ? (unsigned)t->height-1 : 1,
				    .columns = (unsigned)t->width,.rows = (unsigned)t->height,.fg = (uint16_t)(15-32),.bg = (uint16_t)(2-32),.reverse = reverse != 0,.wrap = state == 6};
				unsigned n = counts[c] > 0 ? (unsigned)counts[c] : 1;
				for (unsigned i = 0; i < n; ++i) { model_byte(&m,27); if (selector >= 0) model_byte(&m,letters[selector > 4 ? 0 : selector]); }
				igs->math[0] = (int16_t)selector; igs->math[1] = (int16_t)counts[c]; feed(&f,"G#m 0],1]:");
				if (t->xpos != (int)m.x+1 || t->ypos != (int)m.y+1)
					fprintf(stderr,"m res%u status%u state%u reverse%u selector%d count%d: cursor%d,%d != %u,%u\n",resolution,reserved,state,reverse,selector,counts[c],t->xpos,t->ypos,m.x+1,m.y+1);
				CHECK(t->xpos == (int)m.x+1 && t->ypos == (int)m.y+1);
				CHECK(t->vt52_foreground == m.fg && t->vt52_background == m.bg && t->negative == m.reverse);
				CHECK((t->accumulator != NULL) == (m.state != 0) && (t->sequence != 0) == (m.state == 1));
				if (m.state >= 2) CHECK(t->seq_trailing_remain == (m.state == 2 ? 2 : 1));
				CHECK(igs->random_seed == 1 && igs->pattern_seed == 0);
				model_pixels(&f,resolution,&m);
			}
		destroy(&f);
	}
}

static void
partitions(void)
{
	static const char *wires[] = {"AG#m>3,2:M 2:B","AG#g>1:m>r,R:M 2:B","AG#g>2:m>0],1]:M 2:BZ"};
	for (unsigned resolution = 0; resolution < 3; ++resolution) for (unsigned which = 0; which < 3; ++which)
		for (size_t split = 0; split <= strlen(wires[which])+1; ++split) {
			struct fixture f; create(&f,resolution,false); struct cterminal *t = f.terminal; struct cterm_igs *igs = t->igs_engine;
			t->response_cb = no_reply; feed(&f,"\033f\033w"); igs->math[0] = -1; igs->math[1] = 3;
			igs->random_range[0][1] = igs->random_range[1][1] = 255;
			size_t used, size = strlen(wires[which]);
			if (split > size) for (size_t i = 0; i < size; ++i) CHECK(st_cterm_write(t,wires[which]+i,1,&used) == 0 && used == 1);
			else { CHECK(st_cterm_write(t,wires[which],split,&used) == 0 && used == split); CHECK(st_cterm_write(t,wires[which]+split,size-split,&used) == 0 && used == size-split); }
			CHECK(t->xpos == (which == 0 ? 5 : which == 1 ? 2 : 3) && t->ypos == (which == 2 ? 2 : 1));
			CHECK(igs->random_seed == (which == 1 ? UINT32_C(0x5eb5ca17) : 1) && igs->attributes.writing_mode == 2);
			CHECK(igs->draw_x == 0 && igs->draw_y == 0 && igs->last_line && t->accumulator == NULL && igs->input_deadline == 0);
			const struct st_console_rect area = {0,0,4,2}; struct st_console_cell cells[8];
			CHECK(st_console_read_cells(f.console,&area,cells,area.width) == 0);
			CHECK(cells[0].glyph == (which == 1 ? 'B' : 'A'));
			CHECK(cells[which == 0 ? 3 : which == 1 ? 0 : 5].glyph == (which == 2 ? 'Z' : 'B'));
			destroy(&f);
		}
}

static void
lifetime(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f,resolution,false); struct cterminal *t = f.terminal; struct cterm_igs *igs = t->igs_engine;
		t->response_cb = no_reply; feed(&f,"\033fG#p 3,2:");
		const char *partial[] = {"G#m ","G#m 0,","G#m r,","G#m 0],"};
		for (size_t i = 0; i < sizeof(partial)/sizeof(*partial); ++i) {
			feed(&f,partial[i]); CHECK(t->xpos == 4 && t->ypos == 3 && igs->random_seed == 1);
			CHECK(st_cterm_finish(t) == 0 && t->accumulator == NULL);
			feed(&f,partial[i]); local_ms += 5001; CHECK(st_cterm_poll(t) == 0);
			CHECK(t->xpos == 4 && t->ypos == 3 && igs->random_seed == 1 && t->accumulator == NULL);
		}
		size_t used; const char bad[] = "G#m r,10000:"; CHECK(st_cterm_write(t,bad,sizeof(bad)-1,&used) == 0);
		CHECK(t->xpos == 4 && t->ypos == 3 && igs->random_seed == 1);
		igs->loop_mode = 1; feed(&f,"G#m 3,0:"); CHECK(t->xpos == 5 && t->ypos == 3); igs->loop_mode = 0;
		feed(&f,"G#I>0:I>1:I 2:"); CHECK(t->xpos == 5 && t->ypos == 3);
		char same[32]; (void)snprintf(same,sizeof(same),"G#R %u,0:",resolution); feed(&f,same); CHECK(t->xpos == 5 && t->ypos == 3);
		feed(&f,"\033bG#m r,"); CHECK(st_cterm_finish(t) == 0 && t->accumulator != NULL && t->vt52_foreground == 15);
		feed(&f,"G#m 0,0:"); CHECK(t->xpos == 6 && t->ypos == 3 && t->vt52_foreground == (uint16_t)(27-32));
		CHECK(st_cterm_reset_terminal(t) == 0 && t->accumulator == NULL);
		feed(&f,"G#p>3,2:m 3,0:"); CHECK(t->xpos == 5 && t->ypos == 3 && t->vt52_foreground == 15);
		feed(&f,resolution == 0 ? "G#R 1,0:" : "G#R 0,0:");
		if (cterm_igs_paused(igs)) { local_ms += 17; CHECK(st_cterm_poll(t) == 0); }
		CHECK(t->xpos == (resolution == 2 ? 5 : 1) && t->ypos == (resolution == 2 ? 3 : 1));
		igs->random_range[0][1] = igs->random_range[1][1] = 255;
		t->screen.codepage = ST_CODEPAGE_CP437; t->screen.error = IO_ERROR_IO; t->lf_expand = true;
		const char command[] = "G#m r,R:"; CHECK(st_cterm_write(t,command,sizeof(command)-1,&used) == 0);
		CHECK(igs->random_seed == UINT32_C(0x5eb5ca17) && t->accumulator != NULL && t->sequence != 0);
		CHECK(t->screen.codepage == ST_CODEPAGE_CP437 && t->lf_expand);
		CHECK(st_cterm_reset_terminal(t) == 0); feed(&f,"G#p>3,2:m 0,0:"); CHECK(t->xpos == 1 && t->ypos == 1);
		destroy(&f);
	}
}

int
st_cterm_igs_move_test_main(void)
{
	partitions(); lifetime(); normal(); compositions(); return 0;
}

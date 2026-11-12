/* p: unscaled console words composed with the existing VT52 state. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "igs_test_fixture.h"
#include "cterm_render.h"
#include "igs_console_test_model.h"

static void
no_reply(const char *bytes, size_t size, void *context)
{
	(void)bytes; (void)context; CHECK(size == 0);
}

static unsigned
coordinate(int word, unsigned limit)
{
	/* Independent arithmetic: native addition, byte selection, then unsigned
	 * console subtraction. In particular -32 produces the maximum, not zero. */
	unsigned byte = ((unsigned)(word+65536)+32)%256;
	unsigned value = (byte+65536-32)%65536;
	return value < limit ? value : limit-1;
}

static void
words(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution) for (unsigned reserved = 0; reserved < 2; ++reserved) {
		struct fixture f; create(&f,resolution,reserved != 0); struct cterminal *t = f.terminal; struct cterm_igs *igs = t->igs_engine;
		t->response_cb = no_reply; feed(&f,"\033f");
		igs->draw_start_x = 17; igs->draw_start_y = 23; igs->draw_x = 37; igs->draw_y = 47;
		struct cterm_igs_attributes before = igs->attributes;
		uint32_t palette[16]; memcpy(palette,igs->palette,sizeof(palette));
		const struct st_console_rect full = {0,0,igs->width,igs->height}; seed(&f,&full,769,768);
		for (unsigned scaling = 0; scaling < 3; ++scaling) {
			char setup[32]; (void)snprintf(setup,sizeof(setup),"G#g %u:",scaling); feed(&f,setup);
			/* Byte conversion is shared by all geometries. Exhaust both words
			 * once; elsewhere cover every byte with negative/positive/carry
			 * representatives and both signed extremes, without repeating the
			 * same 16-bit sweep for each unrelated mode/status/g setting. */
			bool exhaustive = resolution == 0 && reserved == 0 && scaling == 0;
			unsigned cases = exhaustive ? 65536 : 770;
			for (unsigned role = 0; role < 2; ++role) for (unsigned i = 0; i < cases; ++i) {
				int word = exhaustive ? INT16_MIN+(int)i : i < 768 ? (int)i-256 : i == 768 ? INT16_MIN : INT16_MAX;
				int column = role == 0 ? word : 300, row = role == 1 ? word : 400;
				igs->math[0] = (int16_t)column; igs->math[1] = (int16_t)row;
				uint32_t final = literal_words(igs, (int[]){column, row}, 2);
				feed(&f,"G#p 0],1]:");
				CHECK(t->xpos == (int)coordinate(column,(unsigned)t->width)+1 && t->ypos == (int)coordinate(row,(unsigned)t->height)+1);
				CHECK(t->accumulator == NULL && igs->math[0] == column && igs->math[1] == row);
				CHECK(igs->random_seed == final && igs->pattern_seed == 0 && igs->input_deadline == 0 && igs->text_deadline == 0);
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
composition_case(struct fixture *f, unsigned resolution, bool reserved, unsigned state, bool reverse, int column, int row)
{
	static const char *prefixes[] = {"","\033","\033Y","\033Y!","\033b","\033c"};
	struct cterminal *t = f->terminal; struct cterm_igs *igs = t->igs_engine;
	t->accumulator = t->saved_accumulator = NULL; t->sequence = 0;
	feed(f,"\033q\033f\033wG#g>1:c>1,15:c 0,0:\033EG#c 0,2:\033Y\042\043");
	if (reverse) feed(f,"\033p");
	if (state == 6) {
		feed(f,"\033v"); char bottom[] = {27,'Y',(char)(32+t->height-1),0}; feed(f,bottom);
	} else feed(f,prefixes[state]);
	t->lf_expand = true;
	struct model m = {.state = state == 6 ? 3 : state,.x = 3,.y = 2,.row = state == 6 ? (unsigned)t->height-1 : 1,
	    .columns = (unsigned)t->width,.rows = (unsigned)t->height,.fg = (uint16_t)(15-32),.bg = (uint16_t)(2-32),.reverse = reverse,.wrap = state == 6};
	const uint8_t output[] = {27,'Y',(uint8_t)(row+32),(uint8_t)(column+32)};
	for (size_t i = 0; i < sizeof(output); ++i) model_byte(&m,output[i]);
	igs->math[0] = (int16_t)column; igs->math[1] = (int16_t)row; feed(f,"G#p 0],1]:");
	if (t->xpos != (int)m.x+1 || t->ypos != (int)m.y+1)
		fprintf(stderr,"p res%u status%d state%u reverse%d col%d row%d: cursor%d,%d != %u,%u\n",resolution,reserved,state,reverse,column,row,t->xpos,t->ypos,m.x+1,m.y+1);
	CHECK(t->xpos == (int)m.x+1 && t->ypos == (int)m.y+1 && t->lf_expand);
	CHECK(t->vt52_foreground == m.fg && t->vt52_background == m.bg && t->negative == m.reverse);
	CHECK((t->accumulator != NULL) == (m.state != 0) && (t->sequence != 0) == (m.state == 1));
	if (m.state >= 2) CHECK(t->seq_trailing_remain == (m.state == 2 ? 2 : 1));
	model_pixels(f,resolution,&m);
}

static void
compositions(void)
{
	static const int boundaries[] = {INT16_MIN,-32,-31,-25,-24,-23,-22,-21,-20,-19,-5,0,3,24,79,95,96,223,224,255,256,300,400,INT16_MAX};
	static const int escapes[][2] = {{66,-5},{67,-5},{57,-5},{33,-5},{40,-5}};
	for (unsigned resolution = 0; resolution < 3; ++resolution) for (unsigned reserved = 0; reserved < 2; ++reserved) {
		struct fixture f; create(&f,resolution,reserved != 0); f.terminal->response_cb = no_reply;
		if (reserved) { const struct st_console_rect status = {0,24*(resolution == 2 ? 16u : 8u),f.terminal->igs_engine->width,resolution == 2 ? 16u : 8u}; seed(&f,&status,769,768); }
		for (unsigned state = 0; state < 7; ++state) for (unsigned reverse = 0; reverse < 2; ++reverse) {
			for (size_t i = 0; i < sizeof(boundaries)/sizeof(*boundaries); ++i) for (unsigned role = 0; role < 2; ++role)
				composition_case(&f,resolution,reserved != 0,state,reverse != 0,role == 0 ? boundaries[i] : 0,role == 1 ? boundaries[i] : 0);
			for (size_t i = 0; i < sizeof(escapes)/sizeof(*escapes); ++i)
				composition_case(&f,resolution,reserved != 0,state,reverse != 0,escapes[i][0],escapes[i][1]);
		}
		destroy(&f);
	}
	for (unsigned resolution = 0; resolution < 3; ++resolution) for (unsigned encoding = 0; encoding < 2; ++encoding) {
		struct fixture f; create(&f,resolution,false);
		f.terminal->wire_encoding = encoding ? ST_CTERM_ENCODING_UTF8 : ST_CTERM_ENCODING_CODEPAGE;
		f.terminal->screen.codepage = encoding ? ST_CODEPAGE_CP437 : ST_CODEPAGE_ATARI_ST;
		enum st_codepage saved = f.terminal->screen.codepage;
		for (unsigned reverse = 0; reverse < 2; ++reverse) for (unsigned byte = 0; byte < 256; ++byte) for (unsigned role = 0; role < 2; ++role) {
			composition_case(&f,resolution,false,4,reverse != 0,role == 0 ? (int)byte-32 : 0,role == 1 ? (int)byte-32 : 0);
			CHECK(f.terminal->screen.codepage == saved);
		}
		destroy(&f);
	}
}

static void
partitions(void)
{
	static const char *wires[] = {"AG#p>3,2:M 2:B","AG#g>1:p>r,R:M 2:B","AG#g>2:p>0],1]:M 2:"};
	for (unsigned resolution = 0; resolution < 3; ++resolution) for (unsigned which = 0; which < 3; ++which)
		for (size_t split = 0; split <= strlen(wires[which])+1; ++split) {
			struct fixture f; create(&f,resolution,false); struct cterm_igs *igs = f.terminal->igs_engine;
			f.terminal->response_cb = no_reply; igs->math[0] = igs->math[1] = -32;
			igs->random_range[0][1] = igs->random_range[1][1] = 255; feed(&f,"\033f\033w");
			size_t used, size = strlen(wires[which]);
			if (split > size) for (size_t i = 0; i < size; ++i) CHECK(st_cterm_write(f.terminal,wires[which]+i,1,&used) == 0 && used == 1);
			else {
				CHECK(st_cterm_write(f.terminal,wires[which],split,&used) == 0 && used == split);
				CHECK(st_cterm_write(f.terminal,wires[which]+split,size-split,&used) == 0 && used == size-split);
			}
			CHECK(igs->random_seed == (which == 1 ? UINT32_C(0x5eb5ca17) : 1));
			/* TOS returns bits8..31 of each LCG state: byte operands230,202. */
			unsigned x = which == 0 ? 4 : which == 1 ? coordinate(230,(unsigned)f.terminal->width)+1 : (unsigned)f.terminal->width-1;
			if (x >= (unsigned)f.terminal->width) x = (unsigned)f.terminal->width-1;
			unsigned y = which == 0 ? 2 : which == 1 ? coordinate(202,(unsigned)f.terminal->height) : (unsigned)f.terminal->height-1;
			if (f.terminal->xpos != (int)x+1 || f.terminal->ypos != (int)y+1)
				fprintf(stderr,"p partition res%u wire%u split%zu: cursor%d,%d != %u,%u seed%08" PRIx32 "\n",resolution,which,split,f.terminal->xpos,f.terminal->ypos,x+1,y+1,igs->random_seed);
			CHECK(f.terminal->xpos == (int)x+1 && f.terminal->ypos == (int)y+1);
			CHECK(igs->attributes.writing_mode == 2 && igs->draw_x == 0 && igs->draw_y == 0 && igs->last_line);
			CHECK(igs->input_deadline == 0 && f.terminal->accumulator == NULL);
			destroy(&f);
		}
}

static void
lifetime(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f,resolution,false); struct cterminal *t = f.terminal; struct cterm_igs *igs = t->igs_engine;
		t->response_cb = no_reply; feed(&f,"\033f\033wG#p 3,2:"); CHECK(t->xpos == 4 && t->ypos == 3);
		const char *partial[] = {"G#p ","G#p 1,","G#p r,","G#p 0],"};
		for (size_t i = 0; i < sizeof(partial)/sizeof(*partial); ++i) {
			feed(&f,partial[i]); CHECK(t->xpos == 4 && t->ypos == 3 && igs->random_seed == 1);
			CHECK(st_cterm_finish(t) == 0 && t->accumulator == NULL);
			feed(&f,partial[i]); local_ms += 5001; CHECK(st_cterm_poll(t) == 0);
			CHECK(t->xpos == 4 && t->ypos == 3 && igs->random_seed == 1 && t->accumulator == NULL);
		}
		size_t used; const char bad[] = "G#p r,10000:";
		CHECK(st_cterm_write(t,bad,sizeof(bad)-1,&used) == 0);
		CHECK(t->xpos == 4 && t->ypos == 3 && igs->random_seed == 1 && t->accumulator == NULL);
		feed(&f,"G#I>0:I>1:I 2:"); CHECK(t->xpos == 4 && t->ypos == 3);
		char same[32]; (void)snprintf(same,sizeof(same),"G#R %u,0:",resolution); feed(&f,same);
		CHECK(t->xpos == 4 && t->ypos == 3);
		igs->loop_mode = 1; feed(&f,"G#p 4,3:"); CHECK(t->xpos == 5 && t->ypos == 4); igs->loop_mode = 0;
		feed(&f,"\033bG#p r,"); CHECK(st_cterm_finish(t) == 0 && t->accumulator != NULL && t->vt52_foreground == 15);
		feed(&f,"G#p 3,2:"); CHECK(t->vt52_foreground == (uint16_t)(27-32) && t->xpos == 8 && t->ypos == 4);
		CHECK(st_cterm_reset_terminal(t) == 0 && t->accumulator == NULL);
		feed(&f,"G#p 3,2:"); CHECK(t->xpos == 4 && t->ypos == 3 && t->vt52_foreground == 15);
		feed(&f,resolution == 0 ? "G#R 1,0:" : "G#R 0,0:");
		if (cterm_igs_paused(igs)) { local_ms += 17; CHECK(st_cterm_poll(t) == 0); }
		CHECK(t->xpos == (resolution == 2 ? 4 : 1) && t->ypos == (resolution == 2 ? 3 : 1));
		feed(&f,"G#p 3,2:"); CHECK(t->xpos == 4 && t->ypos == 3);
		t->screen.codepage = ST_CODEPAGE_CP437; t->screen.error = IO_ERROR_IO; t->lf_expand = true;
		igs->random_range[0][1] = igs->random_range[1][1] = 255;
		const char command[] = "G#p r,R:";
		CHECK(st_cterm_write(t,command,sizeof(command)-1,&used) == 0);
		CHECK(igs->random_seed == UINT32_C(0x5eb5ca17)); /* Both fields acquired before the first failing output. */
		CHECK(t->screen.codepage == ST_CODEPAGE_CP437 && t->lf_expand && t->accumulator != NULL && t->sequence != 0);
		CHECK(st_cterm_reset_terminal(t) == 0); feed(&f,"G#p 0,0:"); CHECK(t->xpos == 1 && t->ypos == 1);
		destroy(&f);
	}
}

int
st_cterm_igs_position_test_main(void)
{
	partitions(); lifetime(); words(); compositions(); return 0;
}

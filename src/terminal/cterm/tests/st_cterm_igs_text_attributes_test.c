/* E: literal built-in point selection, effects and rotation attributes. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "igs_fill_test_helpers.h"

static void
literal(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f,resolution,false); f.terminal->response_cb = no_response;
		const struct st_console_rect point = {40,60,1,1}; seed(&f,&point,768,768+cterm_igs_plane_mask(resolution));
		feed(&f,"G#E 63,18,4:"); struct cterm_igs *igs = f.terminal->igs_engine;
		CHECK(igs->attributes.text_effects == 31 && igs->attributes.text_points == 18);
		CHECK(igs->attributes.text_height == 13 && igs->attributes.text_point_mode && igs->attributes.text_rotation == 3600);
		struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f.console,&point,false) == 0);
		CHECK(b->primary[0] == 768 && b->alternate[0] == 768u+cterm_igs_plane_mask(resolution)); st_pixel_buffer_destroy(b);
		feed(&f,"G#E 0,20,5:");
		CHECK(igs->attributes.text_points == 20 && igs->attributes.text_height == 27 && igs->attributes.text_rotation == 0);
		feed(&f,"G#I 2:");
		CHECK(!igs->attributes.text_point_mode && igs->attributes.text_points == (resolution == 2 ? 10 : 9));
		CHECK(igs->attributes.text_height == (resolution == 2 ? 13 : 6)); destroy(&f);
	}
}
struct size_result { int points, top; };
/* Literal canonical table from reference30: every request8..20. */
static const struct size_result point_results[13] = {
	{8,4},{9,6},{10,13},{10,13},{10,13},{10,13},{10,13},{10,13},
	{16,9},{16,9},{18,13},{18,13},{20,27},
};

static void
check_state(struct cterm_igs *igs, const struct cterm_igs_attributes *before,
    int effects, int points, int rotation)
{
	struct cterm_igs_attributes expected; memcpy(&expected,before,sizeof(expected));
	int index = points < 8 ? 0 : points > 20 ? 12 : points-8;
	expected.text_effects = (int)((unsigned)effects % 32);
	expected.text_points = point_results[index].points; expected.text_height = point_results[index].top;
	expected.text_point_mode = true;
	/* Independent whole-quadrant reduction of the native signed rounding,
	 * using safe wide port values instead of accidental16-bit overflow. */
	expected.text_rotation = rotation > 4 ? 0 : rotation < 0 ? (rotation+1)*900 : rotation*900;
	CHECK(memcmp(&expected,&igs->attributes,sizeof(expected)) == 0);
}

static void
matrix(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f,resolution,false); f.terminal->response_cb = no_response;
		struct cterm_igs *igs = f.terminal->igs_engine; seed_words(&f);
		igs->draw_start_x = 17; igs->draw_start_y = 23; igs->draw_x = 37; igs->draw_y = 47;
		igs->attributes.text_pen = 0; igs->attributes.text_align_h = 2; igs->attributes.text_align_v = 5;
		memcpy(igs->patterns[2],user_rows,sizeof(user_rows));
		uint16_t patterns[8][16]; memcpy(patterns,igs->patterns,sizeof(patterns));
		uint32_t palette[16]; memcpy(palette,igs->palette,sizeof(palette));
		const struct st_console_rect full = {0,0,igs->width,igs->height}; struct st_pixel_buffer *b = NULL;
		CHECK(st_console_get_pixels(&b,f.console,&full,false) == 0);
		uint64_t hashes[2] = {native_hash(b,(unsigned[]){4,2,1}[resolution],false),native_hash(b,(unsigned[]){4,2,1}[resolution],true)};
		st_pixel_buffer_destroy(b);
		for (unsigned raw = 0; raw <= UINT16_MAX; ++raw) {
			int word = raw < 32768 ? (int)raw : (int)raw-65536;
			/* Exercise each sentinel as a literal result in each role. Three
			 * encoded inputs together instead mean three independent draws. */
			if (word == -32765 || word == -32764) {
				for (unsigned role = 0; role < 3; ++role) {
					int args[3] = {0,9,0}; args[role] = word;
					for (unsigned i = 0; i < 3; ++i) igs->math[i] = (int16_t)args[i];
					uint32_t final = literal_words(igs, args, 3);
					struct cterm_igs_attributes before = igs->attributes;
					feed(&f,"G#E 0],1],2]:"); check_state(igs,&before,args[0],args[1],args[2]);
					CHECK(igs->random_seed == final);
					for (unsigned i = 0; i < 3; ++i) CHECK(igs->math[i] == args[i]);
				}
				continue;
			}
			igs->random_seed = 1;
			igs->math[0] = igs->math[1] = igs->math[2] = (int16_t)word;
			struct cterm_igs_attributes before = igs->attributes;
			feed(&f,"G#E 0],1],2]:"); check_state(igs,&before,word,word,word);
			CHECK(igs->math[0] == word && igs->math[1] == word && igs->math[2] == word);
		}
		for (unsigned scale = 0; scale < 3; ++scale) for (unsigned effects = 0; effects < 32; ++effects)
			for (unsigned points = 8; points <= 20; ++points) for (unsigned rotation = 0; rotation <= 5; ++rotation) {
				struct cterm_igs_attributes before = igs->attributes; char wire[96];
				(void)snprintf(wire,sizeof(wire),"G#g>%u:E %u,%u,%u:",scale,effects,points,rotation);
				feed(&f,wire); check_state(igs,&before,(int)effects,(int)points,(int)rotation);
			}
		igs->math[127] = 18; feed(&f,"G#g>1:E 9999],9999],9999]:");
		CHECK(igs->attributes.text_effects == 18 && igs->attributes.text_points == 18 && igs->attributes.text_rotation == 0);
		CHECK(igs->math[127] == 18 && igs->random_seed == 1 && igs->pattern_seed == 0);
		CHECK(memcmp(patterns,igs->patterns,sizeof(patterns)) == 0 && memcmp(palette,igs->palette,sizeof(palette)) == 0);
		CHECK(igs->draw_start_x == 17 && igs->draw_start_y == 23 && igs->draw_x == 37 && igs->draw_y == 47 && igs->last_line);
		CHECK(st_console_get_pixels(&b,f.console,&full,false) == 0);
		CHECK(native_hash(b,(unsigned[]){4,2,1}[resolution],false) == hashes[0] && native_hash(b,(unsigned[]){4,2,1}[resolution],true) == hashes[1]);
		st_pixel_buffer_destroy(b); destroy(&f);
	}
}

static void
partitions(void)
{
	const char *wires[] = {"AG#g>1:E>63,18,4:M 2:B","AG#g>2:E>r,R,r:M 2:B"};
	for (unsigned resolution = 0; resolution < 3; ++resolution) for (unsigned which = 0; which < 2; ++which)
		for (size_t split = 0; split <= strlen(wires[which])+1; ++split) {
			struct fixture f; create(&f,resolution,false); f.terminal->response_cb = no_response;
			struct cterm_igs *igs = f.terminal->igs_engine; igs->random_range[0][1] = igs->random_range[1][1] = 255;
			size_t used, size = strlen(wires[which]);
			if (split > size) for (size_t i = 0; i < size; ++i) CHECK(st_cterm_write(f.terminal,wires[which]+i,1,&used) == 0 && used == 1);
			else {
				CHECK(st_cterm_write(f.terminal,wires[which],split,&used) == 0 && used == split);
				CHECK(st_cterm_write(f.terminal,wires[which]+split,size-split,&used) == 0 && used == size-split);
			}
			CHECK(igs->random_seed == (which ? UINT32_C(0x4c45300c) : 1));
			CHECK(igs->attributes.text_effects == (which ? 6 : 31) && igs->attributes.text_points == (which ? 20 : 18));
			CHECK(igs->attributes.text_height == (which ? 27 : 13) && igs->attributes.text_rotation == (which ? 0 : 3600));
			CHECK(igs->attributes.writing_mode == 2 && igs->draw_x == 0);
			struct st_console_cell cells[2]; const struct st_console_rect text = {0,0,2,1};
			CHECK(st_console_read_cells(f.console,&text,cells,2) == 0 && cells[0].glyph == 'A' && cells[1].glyph == 'B'); destroy(&f);
		}
}

static void
recovery_and_lifetime(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f,resolution,false); f.terminal->response_cb = no_response;
		struct cterm_igs *igs = f.terminal->igs_engine;
		feed(&f,"G#E>31,18,4:C>3,0:"); struct cterm_igs_attributes before = igs->attributes;
		const char *partial[] = {"G#E ","G#E 1,","G#E 1,2,","G#E r,R,"};
		for (unsigned i = 0; i < 4; ++i) {
			feed(&f,partial[i]); CHECK(st_cterm_finish(f.terminal) == 0);
			feed(&f,partial[i]); local_ms += 5001; CHECK(st_cterm_poll(f.terminal) == 0);
			CHECK(memcmp(&before,&igs->attributes,sizeof(before)) == 0 && igs->random_seed == 1);
		}
		size_t used; const char bad[] = "G#E 1,2,10000:";
		CHECK(st_cterm_write(f.terminal,bad,sizeof(bad)-1,&used) == 0);
		CHECK(memcmp(&before,&igs->attributes,sizeof(before)) == 0);
		feed(&f,"G#I 1:"); CHECK(memcmp(&before,&igs->attributes,sizeof(before)) == 0);
		const char *wire = resolution == 0 ? "G#R>1,0:E 15,16,3:" : "G#R>0,0:E 15,16,3:";
		size_t prefix = strlen("G#R>1,0:"); CHECK(st_cterm_write(f.terminal,wire,strlen(wire),&used) == 0);
		if (resolution == 2) CHECK(used == strlen(wire));
		else { CHECK(used == prefix && igs->attributes.text_points == 18); local_ms += 17; CHECK(st_cterm_poll(f.terminal) == 0); feed(&f,wire+prefix); }
		CHECK(igs->attributes.text_points == 16 && igs->attributes.text_height == 9 && igs->attributes.text_rotation == 2700);
		feed(&f,"G#I 5:"); if (cterm_igs_paused(igs)) { local_ms += 17; CHECK(st_cterm_poll(f.terminal) == 0); }
		CHECK(igs->attributes.text_points == 16 && igs->attributes.text_point_mode && igs->resolution == resolution);
		for (unsigned operation = 0; operation < 3; operation += 2) {
			char wire_reset[64]; (void)snprintf(wire_reset,sizeof(wire_reset),"G#E>31,20,4:I %u:",operation); feed(&f,wire_reset);
			CHECK(!igs->attributes.text_point_mode && igs->attributes.text_effects == 0 && igs->attributes.text_rotation == 0);
			CHECK(igs->attributes.text_points == (resolution == 2 ? 10 : 9) && igs->attributes.text_height == (resolution == 2 ? 13 : 6));
		}
		feed(&f,"G#E 1,2,"); CHECK(st_cterm_reset_terminal(f.terminal) == 0);
		CHECK(!igs->attributes.text_point_mode && igs->attributes.text_points == (resolution == 2 ? 10 : 9));
		feed(&f,"G#E 16,20,2:"); CHECK(igs->attributes.text_points == 20 && igs->attributes.text_effects == 16 && igs->attributes.text_rotation == 1800);
		destroy(&f);
	}
}

int
st_cterm_igs_text_attributes_test_main(void)
{
	literal(); matrix(); partitions(); recovery_and_lifetime();
	return 0;
}

/* X15: independent primitive compositions, exclusive bounds and local waits. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "igs_pixel_test_helpers.h"
#include "st_igs_image.h"

struct sweep_case {
	int resolution, style, width, begin, end, mode, clipping, initial_last;
	int values[9], bounds[4], position[4], last;
	uint64_t hashes[2];
};
#include "data/igs_sweep_vectors.h"
_Static_assert(sizeof(sweep_cases)/sizeof(*sweep_cases) == 600,"complete independent sweep matrix");
static size_t keys;

static void
no_reply(const char *bytes, size_t size, void *context)
{
	(void)bytes; (void)size; (void)context; CHECK(false);
}

static void
key_output(const char *bytes, size_t size, void *context)
{
	(void)bytes; (void)context; keys += size;
}

static void
position(const struct cterm_igs *igs, const int p[4])
{
	CHECK(igs->draw_start_x == p[0] && igs->draw_start_y == p[1] && igs->draw_x == p[2] && igs->draw_y == p[3]);
}

static void
request(struct fixture *f, const int values[9])
{
	for (unsigned i = 0; i < 9; ++i) f->terminal->igs_engine->math[i] = (int16_t)values[i];
	feed(f,"G#X 15,0],1],2],3],4],5],6],7],8]:");
}

static void
vectors(void)
{
	static const int ends[3][3] = {{0,52,62},{51,50,53},{61,54,60}};
	struct fixture f; bool active = false; int resolution = -1;
	for (size_t i = 0; i < sizeof(sweep_cases)/sizeof(*sweep_cases); ++i) {
		const struct sweep_case *c = &sweep_cases[i];
		if (resolution != c->resolution) {
			if (active) destroy(&f);
			create(&f,(unsigned)c->resolution,false); active = true; resolution = c->resolution;
		}
		struct cterm_igs *igs = f.terminal->igs_engine;
		igs->now_ms = now; f.terminal->response_cb = no_reply;
		const struct st_console_rect full = {0,0,igs->width,igs->height};
		seed(&f,&full,768,768+cterm_igs_plane_mask((unsigned)resolution));
		igs->attributes.line_pen = 10; igs->patterns[6][0] = 0xa531; igs->last_line = c->initial_last != 0;
		igs->clipping = c->clipping != 0;
		igs->clip_x1 = c->bounds[0]; igs->clip_y1 = c->bounds[1]; igs->clip_x2 = c->bounds[2]; igs->clip_y2 = c->bounds[3];
		igs->memory_route = 5; igs->double_step = 4; cterm_igs_set_scaling(igs,(int)(i%3));
		char wire[100]; (void)snprintf(wire,sizeof(wire),"G#T>2,%d,%d:T>2,%d,%d:M %d:",
		    c->style,c->width,c->style,ends[c->begin][c->end],c->mode); feed(&f,wire);
		CHECK(igs->attributes.line_type == c->style && igs->attributes.line_width == c->width);
		CHECK(igs->attributes.line_begin == c->begin && igs->attributes.line_end == c->end);
		struct cterm_igs_attributes attrs; memcpy(&attrs,&igs->attributes,sizeof(attrs));
		request(&f,c->values); position(igs,c->position);
		CHECK(!igs->sweep.active && !st_cterm_input_paused(f.terminal) && igs->last_line == (c->last != 0));
		CHECK(memcmp(&attrs,&igs->attributes,sizeof(attrs)) == 0 && igs->random_seed == 1);
		CHECK(igs->memory_route == 5 && igs->double_step == 4 && igs->text_counter == 0 && igs->text_deadline == 0);
		struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f.console,&full,false) == 0);
		for (unsigned phase = 0; phase < 2; ++phase) {
			uint64_t hash = native_hash(b,(unsigned[]){4,2,1}[resolution],phase != 0);
			if (hash != c->hashes[phase]) fprintf(stderr,"X15 vector%zu phase%u: %016" PRIx64 " != %016" PRIx64 "\n",i,phase,hash,c->hashes[phase]);
			CHECK(hash == c->hashes[phase]);
		}
		st_pixel_buffer_destroy(b);
	}
	if (active) destroy(&f);
}

static void
literal(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution)
		for (unsigned status = 0; status < 2; ++status) {
			struct fixture f; create(&f,resolution,status != 0);
			struct cterm_igs *igs = f.terminal->igs_engine;
			unsigned mask = cterm_igs_plane_mask(resolution);
			const struct st_console_rect full = {0,0,igs->width,igs->height};
			for (int mode = 1; mode <= 4; ++mode) {
				seed(&f,&full,768+mask,768); igs->attributes.writing_mode = mode;
				feed(&f,"G#g>2:X 15,16,16,24,16,0,16,1,4,0:");
				position(igs,(int[4]){16,16,20,16});
				struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f.console,&full,false) == 0);
				for (unsigned y = 0; y < igs->height; ++y) for (unsigned x = 0; x < igs->width; ++x) {
					bool touched = y == 16 && x >= 16 && x <= 20, odd = y == 16 && x >= 17 && x <= 20;
					unsigned p = mask, a = 0;
					if (mode <= 2 && touched) a = mask;
					if (mode == 3 && odd) { p = 0; a = mask; }
					CHECK(b->primary[y*igs->width+x] == 768+p && b->alternate[y*igs->width+x] == 768+a);
				}
				st_pixel_buffer_destroy(b);
			}
			/* D begins at the last emitted point, not the excluded bound. */
			seed(&f,&full,768,768); igs->attributes.writing_mode = 3;
			feed(&f,"G#g>0:X>15,16,16,24,16,0,16,1,4,0:D 24,16:");
			position(igs,(int[4]){24,16,24,16});
			const struct st_console_rect line = {16,16,9,1}; struct st_pixel_buffer *b = NULL;
			CHECK(st_console_get_pixels(&b,f.console,&line,false) == 0);
			for (unsigned x = 0; x < 9; ++x) CHECK(b->primary[x] == 768+(x != 0 && x != 4 ? mask : 0));
			st_pixel_buffer_destroy(b);
			/* Each sweep line on the lower edge is clipped by the host's
			 * reserved status row; the original requested endpoint survives. */
			seed(&f,&full,768,768); igs->attributes.writing_mode = 1;
			int bottom = (int)igs->height-1;
			request(&f,(int[9]){0,bottom,8,bottom,0,0,1,4,0});
			position(igs,(int[4]){0,bottom,4,bottom});
			CHECK(st_console_get_pixels(&b,f.console,&full,false) == 0);
			for (unsigned y = 0; y < igs->height; ++y) for (unsigned x = 0; x < igs->width; ++x)
				CHECK(b->primary[y*igs->width+x] == 768+(!status && y == (unsigned)bottom && x <= 4 ? mask : 0));
			st_pixel_buffer_destroy(b); destroy(&f);
		}
}

static void
waits_and_bounds(void)
{
	static const unsigned rates[] = {50,60,1000};
	static const int controls[][5] = {
		/* start,bound,add,step,wait: literal arithmetic/boundary cases. */
		{2,8,1,3,1},{8,2,0,3,2},{2,8,-1,3,60},{2,8,1,0,61},{8,2,0,-1,32767},
		{8,8,1,1,60},{8,8,0,1,60},{8,2,1,1,60},{2,8,0,1,60},
		{32766,32767,1,2,1},{-32767,-32768,0,2,1},{0,32767,1,2,1},{1,-32768,0,2,1},
		{-32768,32767,1,1,0},{32767,-32768,0,1,0},{30000,32000,1,10000,-1},
	};
	struct fixture f; create(&f,0,false); struct cterm_igs *igs = f.terminal->igs_engine;
	igs->clipping = true; igs->clip_x1 = igs->clip_y1 = 0; igs->clip_x2 = igs->clip_y2 = 0;
	f.terminal->response_cb = no_reply; f.terminal->keystroke_cb = key_output;
	for (unsigned axis = 0; axis < 2; ++axis)
		for (size_t r = 0; r < sizeof(rates)/sizeof(*rates); ++r)
			for (size_t i = 0; i < sizeof(controls)/sizeof(*controls); ++i) {
				int start = controls[i][0], bound = controls[i][1], step = controls[i][3] < 1 ? 1 : controls[i][3];
				bool increase = controls[i][2] != 0;
				int64_t distance = increase ? (int64_t)bound-start : (int64_t)start-bound;
				unsigned count = distance <= 0 ? 0 : (unsigned)((distance+step-1)/step);
				unsigned wait = controls[i][4] < 0 ? 0u : controls[i][4] > 60 ? 60u : (unsigned)controls[i][4];
				int args[9] = {30000,30000,30000,30000,(int)axis,start,controls[i][2],controls[i][3],controls[i][4]};
				args[2+axis] = bound; local_ms = 1000; igs->sync_wait_hz = rates[r]; request(&f,args);
				if (wait != 0 && count != 0) {
					uint64_t duration = ((uint64_t)wait*1000+rates[r]-1)/rates[r];
					for (unsigned line = 0; line < count; ++line) {
						int expected[4] = {30000,30000,args[2],args[3]};
						expected[2+axis] = start+(increase ? 1 : -1)*(int)line*step;
						position(igs,expected);
						CHECK(igs->sweep.active && st_cterm_input_paused(f.terminal) && igs->wait_deadline == local_ms+duration);
						uint64_t due = igs->wait_deadline; local_ms = due-1;
						CHECK(st_cterm_poll(f.terminal) == 0); position(igs,expected);
						local_ms = due; CHECK(st_cterm_poll(f.terminal) == 0);
					}
					CHECK(local_ms == 1000+count*duration);
				}
				int end[4] = {30000,30000,args[2],args[3]};
				if (count != 0) end[2+axis] = start+(increase ? 1 : -1)*(int)(count-1)*step;
				position(igs,end); CHECK(!igs->sweep.active && !st_cterm_input_paused(f.terminal) && igs->wait_deadline == 0);
			}
	/* Every unsupported signed axis word initializes positions but never
	 * calls a renderer or clock, even with otherwise active operands. */
	igs->attributes.line_width = 2;
	for (int axis = INT16_MIN; axis <= INT16_MAX; ++axis) {
		if (axis == 0 || axis == 1) continue;
		request(&f,(int[9]){3,4,5,6,axis,0,1,1,60}); position(igs,(int[4]){3,4,5,6});
	}
	igs->attributes.line_width = 1;
	request(&f,(int[9]){30000,30000,30008,30000,0,30002,1,3,1});
	uint64_t due = igs->wait_deadline;
	CHECK(st_cterm_handle_key(f.terminal,'a',0) && keys == 1 && igs->wait_deadline == due && igs->sweep.active);
	CHECK(st_cterm_finish(f.terminal) == 0 && !igs->sweep.active && !st_cterm_input_paused(f.terminal));
	position(igs,(int[4]){30000,30000,30002,30000}); destroy(&f);
}

static void
finish(struct fixture *f)
{
	while (st_cterm_input_paused(f->terminal)) {
		local_ms = f->terminal->igs_engine->wait_deadline; CHECK(st_cterm_poll(f->terminal) == 0);
	}
}

static int
bad_clock(uint64_t *value)
{
	(void)value; return IO_ERROR_IO;
}

static void
partitions_and_errors(void)
{
	struct fixture f; create(&f,0,false); struct cterm_igs *igs = f.terminal->igs_engine;
	const char wire[] = "AG#X>15,16,16,24,16,0,16,1,4,1:M 2:B";
	for (size_t split = 0; split <= sizeof(wire); ++split) {
		CHECK(st_cterm_reset_terminal(f.terminal) == 0); size_t offset = 0, size = sizeof(wire)-1;
		while (offset < size) {
			size_t piece = split > size ? 1 : offset < split ? split-offset : size-offset, used;
			CHECK(st_cterm_write(f.terminal,wire+offset,piece,&used) == 0); offset += used; finish(&f);
		}
		position(igs,(int[4]){16,16,20,16});
		CHECK(igs->attributes.writing_mode == 2 && f.terminal->xpos == 3);
		struct st_console_cell cells[2]; const struct st_console_rect rect = {0,0,2,1};
		CHECK(st_console_read_cells(f.console,&rect,cells,2) == 0 && cells[0].glyph == 'A' && cells[1].glyph == 'B');
	}
	const char randomized[] = "G#X>r,3000,3000,3010,3000,0,R,1,3,1:M 2:";
	for (size_t split = 0; split <= sizeof(randomized); ++split) {
		CHECK(st_cterm_reset_terminal(f.terminal) == 0);
		igs->random_seed = 158; igs->random_range[0][1] = 255; igs->random_range[1][1] = 10;
		size_t offset = 0, size = sizeof(randomized)-1;
		while (offset < size) {
			size_t piece = split > size ? 1 : offset < split ? split-offset : size-offset, used;
			CHECK(st_cterm_write(f.terminal,randomized+offset,piece,&used) == 0); offset += used;
			if (igs->sweep.active) CHECK(igs->random_seed == UINT32_C(0xcc7b0243));
			finish(&f);
		}
		CHECK(igs->random_seed == UINT32_C(0xcc7b0243)); position(igs,(int[4]){3000,3000,3008,3000});
	}
	feed(&f,"G#X 10,19,17:"); int before[4] = {3000,3000,19,17};
	const char partial[] = "G#X 15,16,16,24,16,0,16,1,4,1:";
	for (size_t size = 4; size < sizeof(partial)-1; ++size) {
		size_t used; CHECK(st_cterm_write(f.terminal,partial,size,&used) == 0 && used == size);
		CHECK(st_cterm_finish(f.terminal) == 0); position(igs,before);
		CHECK(st_cterm_write(f.terminal,partial,size,&used) == 0 && used == size);
		local_ms += 5001; CHECK(st_cterm_poll(f.terminal) == 0); position(igs,before);
	}
	const char invalid[] = "G#X 15,16,16,24,16,0,16,1,4,10000:"; size_t used;
	CHECK(st_cterm_write(f.terminal,invalid,sizeof(invalid)-1,&used) == 0); position(igs,before);
	igs->now_ms = bad_clock; feed(&f,"G#X 15,3,4,5,6,2,0,1,1,60:");
	feed(&f,"G#X 15,3,4,5,6,0,5,1,1,60:"); position(igs,(int[4]){3,4,5,6});
	feed(&f,"G#X 15,16,16,24,16,0,16,1,4,0:"); position(igs,(int[4]){16,16,20,16});
	const char failed[] = "G#X 15,16,16,24,16,0,16,1,4,1:";
	CHECK(st_cterm_write(f.terminal,failed,sizeof(failed)-1,&used) == 0 && !igs->sweep.active);
	position(igs,(int[4]){16,16,16,16});
	igs->now_ms = now; feed(&f,failed); igs->now_ms = bad_clock;
	CHECK(st_cterm_poll(f.terminal) == IO_ERROR_IO && igs->sweep.active);
	igs->now_ms = now; finish(&f); position(igs,(int[4]){16,16,20,16});
	igs->attributes.line_width = 2;
	CHECK(st_cterm_write(f.terminal,failed,sizeof(failed)-1,&used) == 0 && !igs->sweep.active);
	position(igs,(int[4]){16,16,16,16}); igs->attributes.line_width = 1;
	feed(&f,failed); igs->attributes.line_width = 2; local_ms = igs->wait_deadline;
	CHECK(st_cterm_poll(f.terminal) == IO_ERROR_INVALID_ARGUMENT && !igs->sweep.active && !st_cterm_input_paused(f.terminal));
	position(igs,(int[4]){16,16,20,16}); igs->attributes.line_width = 1;
	local_ms = UINT64_MAX-1;
	CHECK(st_cterm_write(f.terminal,failed,sizeof(failed)-1,&used) == 0 && !igs->sweep.active);
	local_ms = 1000; igs->sync_wait_hz = 0;
	CHECK(st_cterm_write(f.terminal,failed,sizeof(failed)-1,&used) == 0 && !igs->sweep.active);
	igs->sync_wait_hz = 60; feed(&f,failed);
	CHECK(st_cterm_reset_terminal(f.terminal) == 0 && !igs->sweep.active && !st_cterm_input_paused(f.terminal));
	position(igs,(int[4]){0,0,0,0}); destroy(&f);
}

int
st_cterm_igs_sweep_test_main(void)
{
	vectors(); literal(); waits_and_bounds(); partitions_and_errors(); return 0;
}

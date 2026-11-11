/* X8 palette cycling: independent palette algebra and local-deadline events. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "igs_test_fixture.h"
#include "st_igs_image.h"

struct palette { int requested[16][3]; uint16_t raw[16]; uint32_t rgb[16]; };
static const unsigned physical[16] = {0,15,1,2,4,6,3,5,7,8,9,10,12,14,11,13};
static size_t sent_keys;
static int context_first, context_last, context_pass, context_write;

static void
no_reply(const char *bytes, size_t size, void *context)
{
	(void)bytes; (void)size; (void)context; CHECK(false);
}

static void
key_output(const char *bytes, size_t size, void *context)
{
	(void)bytes; (void)context; sent_keys += size;
}

static unsigned
channel(unsigned value)
{
	static const unsigned values[] = {0,36,73,109,146,182,219,255};
	return values[value&7u];
}

static uint32_t
rgb(uint16_t value)
{
	return channel(value>>8)<<16 | channel(value>>4)<<8 | channel(value);
}

static void
raw_write(struct palette *p, unsigned resolution, int index, int value)
{
	if (value < 0) return;
	index &= 15;
	p->raw[index] = (uint16_t)value;
	if (resolution != 2) p->rgb[index] = rgb((uint16_t)value);
	else if (index == 0) { p->rgb[0] = value&1 ? 0xffffff : 0; p->rgb[1] = p->rgb[0]^0xffffff; }
}

static int
quantize(int value)
{
	static const int transitions[] = {71,214,357,500,643,786,929};
	int result = 0;
	for (size_t i = 0; i < sizeof(transitions)/sizeof(*transitions); ++i)
		if (value >= transitions[i]) ++result;
	return result;
}

static void
requested_write(struct palette *p, unsigned resolution, int pen, const int value[3])
{
	unsigned mask = resolution == 0 ? 15 : resolution == 1 ? 3 : 1;
	if (pen < 0 || (unsigned)pen > mask) return;
	memcpy(p->requested[pen],value,sizeof(p->requested[pen]));
	if (resolution != 2) {
		int packed = quantize(value[0])*256+quantize(value[1])*16+quantize(value[2]);
		raw_write(p,resolution,(int)(physical[pen]&mask),packed);
	}
	else {
		int sum = 0;
		for (unsigned i = 0; i < 3; ++i) sum += value[i] < 142 ? 0 : value[i] >= 858 ? 1000 : value[i];
		if (sum == 0 || sum == 3000) raw_write(p,resolution,0,sum == 0 ? pen : (int)(uint16_t)~(unsigned)pen);
	}
}

static void
check_palette(struct fixture *f, const struct palette *p)
{
	struct cterm_igs *igs = f->terminal->igs_engine;
	CHECK(memcmp(p->requested,igs->requested_palette,sizeof(p->requested)) == 0);
	for (unsigned i = 0; i < 16; ++i)
		if (p->raw[i] != igs->raw_palette[i]) fprintf(stderr,"res%u range%d,%d pass%d write%d raw%u expected%x actual%x\n",
		    igs->resolution,context_first,context_last,context_pass,context_write,i,p->raw[i],igs->raw_palette[i]);
	CHECK(memcmp(p->raw,igs->raw_palette,sizeof(p->raw)) == 0);
	unsigned count = igs->resolution == 2 ? 2 : 16;
	CHECK(memcmp(p->rgb,igs->palette,count*sizeof(*p->rgb)) == 0);
	uint32_t actual[16]; CHECK(st_console_get_palette(f->console,768,actual,16) == 0);
	CHECK(memcmp(p->rgb,actual,count*sizeof(*p->rgb)) == 0);
}

static void
prepare(struct fixture *f, struct palette *p)
{
	struct cterm_igs *igs = f->terminal->igs_engine;
	memset(p,0,sizeof(*p));
	for (unsigned i = 0; i < 16; ++i) {
		for (unsigned j = 0; j < 3; ++j) p->requested[i][j] = (int)((i*137+j*269)%1101)-50;
		int raw = (int)(0x888+i*0x111);
		raw_write(p,igs->resolution,(int)i,raw);
		CHECK(cterm_igs_palette_register(igs,(int)i,raw) == 0);
	}
	memcpy(igs->requested_palette,p->requested,sizeof(p->requested));
	f->terminal->response_cb = no_reply; f->terminal->keystroke_cb = key_output;
	check_palette(f,p);
}

static int
word(int value)
{
	unsigned bits = (unsigned)value&65535u;
	return bits > 32767 ? (int)bits-65536 : (int)bits;
}

static void
request(struct fixture *f, int first, int last, int times, int delay)
{
	struct cterm_igs *igs = f->terminal->igs_engine;
	igs->math[0] = (int16_t)first; igs->math[1] = (int16_t)last;
	igs->math[2] = (int16_t)times; igs->math[3] = (int16_t)delay;
	feed(f,"G#X 8,0],1],2],3]:");
}

static void
query_expected(const struct palette *p, unsigned resolution, bool raw, int index, int cache[3], int value[3])
{
	if (raw) { value[0] = p->raw[(unsigned)index&15u]&0x777; value[1] = value[2] = 0; }
	else {
		unsigned mask = resolution == 0 ? 15 : resolution == 1 ? 3 : 1;
		if (index >= 0 && (unsigned)index <= mask) memcpy(cache,p->requested[index],3*sizeof(*cache));
		memcpy(value,cache,3*sizeof(*value));
	}
}

/* Build expected writes from a pass's initial palette, independently of the
 * emulator's saved/current values and resume phase. Each adjacent source is
 * still untouched when queried. The extra raw source -1/16 wraps to a
 * physical register already written in a full-range pass, so read it live. */
static void
rotation(struct fixture *f, struct palette *p, int first, int last, int times, int delay)
{
	struct cterm_igs *igs = f->terminal->igs_engine;
	context_first = first; context_last = last;
	bool raw = first > 15 || last > 15;
	request(f,first,last,times,delay);
	if (raw) { first = word(first-15); last = word(last-15); }
	first = first < 0 || first > 15 ? 15 : first;
	last = last < 0 || last > 15 ? 15 : last;
	if (times < 0 || first == last) { CHECK(!st_cterm_input_paused(f->terminal)); check_palette(f,p); return; }
	int step = first < last ? -1 : 1, cache[3] = {0};
	for (int pass = 0; pass <= times; ++pass) {
		context_pass = pass;
		struct palette before = *p;
		int saved[3]; query_expected(&before,igs->resolution,raw,last,cache,saved);
		int distance = abs(first-last);
		for (int write = 0; write <= distance+1; ++write) {
			context_write = write;
			int destination = write > distance ? first : last+write*step, value[3];
			if (write > distance) memcpy(value,saved,sizeof(value));
			else query_expected(raw ? p : &before,igs->resolution,raw,destination+step,cache,value);
			CHECK(st_cterm_input_paused(f->terminal) && igs->palette_rotation.active);
			CHECK(igs->wait_deadline == local_ms+(1000+igs->sync_wait_hz-1)/igs->sync_wait_hz);
			check_palette(f,p);
			uint64_t due = igs->wait_deadline;
			local_ms = due-1; CHECK(st_cterm_poll(f->terminal) == 0); check_palette(f,p);
			local_ms = due; CHECK(st_cterm_poll(f->terminal) == 0);
			if (raw) raw_write(p,igs->resolution,destination,value[0]);
			else requested_write(p,igs->resolution,destination,value);
			check_palette(f,p);
		}
		if (delay != 0) {
			CHECK(igs->palette_rotation.delaying && igs->wait_deadline == local_ms+(uint16_t)delay*5u);
			local_ms = igs->wait_deadline-1; CHECK(st_cterm_poll(f->terminal) == 0); check_palette(f,p);
			++local_ms; CHECK(st_cterm_poll(f->terminal) == 0); check_palette(f,p);
		}
	}
	CHECK(!igs->palette_rotation.active && !st_cterm_input_paused(f->terminal) && igs->wait_deadline == 0);
}

static void
matrices(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f,resolution,true);
		struct cterm_igs *igs = f.terminal->igs_engine;
		feed(&f,"\033fG#g>1:X 10,37,43:");
		igs->double_step = 4;
		struct cterm_igs_attributes attrs; memcpy(&attrs,&igs->attributes,sizeof(attrs));
		int dx = igs->draw_x, dy = igs->draw_y;
		const struct st_console_rect full = {0,0,igs->width,igs->height}; seed(&f,&full,769,768);
		CHECK(st_igs_image_create(&igs->memory[0],resolution == 0 ? 4 : resolution == 1 ? 2 : 1) == 0);
		CHECK(st_pixel_buffer_fill(igs->memory[0]->pixels,1,0) == 0);
		struct palette p;
		for (unsigned raw = 0; raw < 2; ++raw)
			for (int first = 0; first < 16; ++first)
				for (int last = 0; last < 16; ++last) {
					prepare(&f,&p); local_ms = 1000;
					rotation(&f,&p,first+(raw ? 15 : 0),last+(raw ? 15 : 0),1,0);
				}
		static const int words[] = {INT16_MIN,-1,0,1,14,15,16,17,30,31,9999,INT16_MAX};
		for (size_t a = 0; a < sizeof(words)/sizeof(*words); ++a)
			for (size_t b = 0; b < sizeof(words)/sizeof(*words); ++b) {
				prepare(&f,&p); local_ms = 1000; rotation(&f,&p,words[a],words[b],1,0);
			}
		static const unsigned rates[] = {50,60,1000};
		static const int delays[] = {INT16_MIN,-1,0,1,9999,INT16_MAX};
		for (size_t rate = 0; rate < sizeof(rates)/sizeof(*rates); ++rate)
			for (size_t d = 0; d < sizeof(delays)/sizeof(*delays); ++d) {
				prepare(&f,&p); local_ms = 1000; igs->sync_wait_hz = rates[rate];
				rotation(&f,&p,1,3,2,delays[d]);
			}
		CHECK(memcmp(&attrs,&igs->attributes,sizeof(attrs)) == 0 && igs->draw_x == dx && igs->draw_y == dy);
		CHECK(igs->double_step == 4 && igs->scaling_mode == 1 && igs->random_seed == 1);
		CHECK(igs->text_counter == 0 && igs->text_deadline == 0 && igs->input_deadline == 0);
		for (size_t i = 0; i < igs->memory[0]->pixels->width; ++i) CHECK(igs->memory[0]->pixels->primary[i] == 1);
		struct st_pixel_buffer *pixels = NULL; CHECK(st_console_get_pixels(&pixels,f.console,&full,false) == 0);
		for (size_t i = 0; i < (size_t)igs->width*igs->height; ++i) CHECK(pixels->primary[i] == 769 && pixels->alternate[i] == 768);
		st_pixel_buffer_destroy(pixels); destroy(&f);
	}
}

static void
finish(struct fixture *f)
{
	struct cterm_igs *igs = f->terminal->igs_engine;
	unsigned iterations = 0;
	while (st_cterm_input_paused(f->terminal)) {
		CHECK(++iterations <= 100000);
		local_ms = igs->wait_deadline; CHECK(st_cterm_poll(f->terminal) == 0);
	}
}

static void
snapshots(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f,resolution,false); struct cterm_igs *igs = f.terminal->igs_engine;
		struct palette p; prepare(&f,&p);
		/* Restore before capture uses zero-initialized serial storage. */
		request(&f,0,15,0,9999);
		for (int i = 0; i < 16; ++i) requested_write(&p,resolution,i,(int[3]){0,0,0});
		check_palette(&f,&p);
		for (unsigned encoded = 0; encoded < 2; ++encoded) {
			prepare(&f,&p); struct palette original = p;
			request(&f,encoded ? 16 : 1,encoded ? 16 : 1,1,1);
			CHECK(!igs->palette_rotation.active && !st_cterm_input_paused(f.terminal)); check_palette(&f,&p);
			int cached[3] = {0};
			for (unsigned i = 0; i < 16; ++i) {
				if (i <= cterm_igs_plane_mask(resolution)) memcpy(cached,original.requested[i],sizeof(cached));
				CHECK(memcmp(igs->palette_rotation.snapshot[i],cached,sizeof(cached)) == 0);
				CHECK(igs->palette_rotation.registers[i] == (original.raw[i]&0x777));
			}
			CHECK(igs->palette_rotation.restore_raw == (encoded != 0));
			rotation(&f,&p,1,3,1,0); CHECK(igs->palette_rotation.restore_raw == (encoded != 0));
			request(&f,0,15,0,0);
			for (int i = 0; i < 16; ++i) {
				if (encoded) raw_write(&p,resolution,i,original.raw[i]&0x777);
				else requested_write(&p,resolution,i,original.requested[i]);
			}
			check_palette(&f,&p); CHECK(!igs->palette_rotation.restore_raw);
			/* The second ordinary restore selects requested data, even after
			 * the first raw restore left that cache deliberately unchanged. */
			request(&f,0,15,0,0);
			for (int i = 0; i < 16; ++i) requested_write(&p,resolution,i,original.requested[i]);
			check_palette(&f,&p);
		}
		/* An encoded no-op also changes the subsequent restore namespace. */
		request(&f,15,15,0,0); /* Ordinary restore clears selector. */
		request(&f,16,16,-1,0); CHECK(igs->palette_rotation.restore_raw && !igs->palette_rotation.active);
		feed(&f,"G#I 2:"); CHECK(igs->palette_rotation.restore_raw);
		CHECK(st_cterm_reset_terminal(f.terminal) == 0 && !igs->palette_rotation.restore_raw);
		for (unsigned i = 0; i < 16; ++i) CHECK(igs->palette_rotation.registers[i] == 0 && igs->palette_rotation.snapshot[i][0] == 0);
		destroy(&f);
	}
}

static int
bad_clock(uint64_t *value)
{
	(void)value; return IO_ERROR_IO;
}

static void
snapshot_resolution(void)
{
	struct fixture f; create(&f,1,false);
	struct palette p; prepare(&f,&p);
	struct palette original = p;
	feed(&f,"G#X 8,1,1,1,1:");
	CHECK(cterm_igs_set_resolution(f.terminal,0) == 0);
	feed(&f,"G#X 8,0,0,0,0:");
	/* Invalid medium-mode queries retained pen3's triple; after returning
	 * to low resolution those stored entries become valid restore pens. */
	for (int i = 0; i < 16; ++i) requested_write(&p,0,i,original.requested[i < 4 ? i : 3]);
	check_palette(&f,&p);
	destroy(&f);
}

static void
keys_and_lifetime(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f,resolution,false); struct cterm_igs *igs = f.terminal->igs_engine;
		struct palette p; prepare(&f,&p);
		static const int counts[] = {1,32766,32767};
		for (size_t i = 0; i < sizeof(counts)/sizeof(*counts); ++i) {
			request(&f,1,3,counts[i],9999);
			CHECK(st_cterm_handle_key(f.terminal,'a',0) && igs->palette_rotation.key);
			/* A key shortens the post-pass delay, never a preceding Vsync. */
			uint64_t due = igs->wait_deadline;
			CHECK(st_cterm_poll(f.terminal) == 0 && igs->wait_deadline == due);
			unsigned writes = 0;
			while (st_cterm_input_paused(f.terminal)) {
				CHECK(++writes <= 4 && !igs->palette_rotation.delaying);
				local_ms = igs->wait_deadline; CHECK(st_cterm_poll(f.terminal) == 0);
			}
			CHECK(writes == 4 && !igs->palette_rotation.key && igs->palette_rotation.remaining == 0);
		}
		CHECK(sent_keys == 0);
		request(&f,1,3,9999,-1);
		for (unsigned i = 0; i < 4; ++i) { local_ms = igs->wait_deadline; CHECK(st_cterm_poll(f.terminal) == 0); }
		CHECK(igs->palette_rotation.delaying);
		CHECK(st_cterm_handle_text_key(f.terminal,0x1f642) && igs->palette_rotation.key);
		CHECK(st_cterm_poll(f.terminal) == 0 && !igs->palette_rotation.active && !st_cterm_input_paused(f.terminal));
		request(&f,1,3,1,0); CHECK(st_cterm_handle_codepage_key(f.terminal,0)); finish(&f);
		request(&f,1,3,1,0); CHECK(st_cterm_handle_doorway_key(f.terminal,0)); finish(&f);
		CHECK(sent_keys == 0);
		CHECK(st_cterm_handle_key(f.terminal,'a',0) && sent_keys == 1); sent_keys = 0;
		/* A palette query is retained over its wait even if a different
		 * local consumer changes that source register in the meantime. */
		prepare(&f,&p); request(&f,16,18,1,0);
		CHECK(cterm_igs_palette_register(igs,2,0x765) == 0);
		local_ms = igs->wait_deadline; CHECK(st_cterm_poll(f.terminal) == 0);
		CHECK(igs->raw_palette[3] == (p.raw[2]&0x777));
		CHECK(st_cterm_finish(f.terminal) == 0 && !igs->palette_rotation.active && igs->palette_rotation.restore_raw);
		feed(&f,"G#I 2:"); CHECK(igs->palette_rotation.restore_raw);
		request(&f,1,3,1,0); CHECK(st_cterm_reset_terminal(f.terminal) == 0);
		CHECK(!igs->palette_rotation.active && !igs->palette_rotation.restore_raw && !st_cterm_input_paused(f.terminal));
		feed(&f,"G#X 8,1,1,1,1:");
		/* R/I5 retain the independent X8 snapshots and restore mode. */
		igs->palette_rotation.restore_raw = true; igs->palette_rotation.registers[7] = 0x123;
		feed(&f,"G#R 1,0:"); finish(&f);
		CHECK(igs->palette_rotation.restore_raw && igs->palette_rotation.registers[7] == 0x123);
		feed(&f,"G#I 5:"); finish(&f);
		CHECK(igs->palette_rotation.restore_raw && igs->palette_rotation.registers[7] == 0x123);
		for (unsigned operation = 0; operation < 6; ++operation) {
			char wire[16]; (void)snprintf(wire,sizeof(wire),"G#I %u:",operation);
			feed(&f,wire); finish(&f);
			CHECK(igs->palette_rotation.restore_raw && igs->palette_rotation.registers[7] == 0x123);
		}
		destroy(&f);
	}
}

static void
errors_and_partitions(void)
{
	struct fixture f; create(&f,0,false); struct cterm_igs *igs = f.terminal->igs_engine;
	struct palette p; prepare(&f,&p);
	const char wire[] = "AG#X>8,1,3,1,1:M 2:B";
	for (size_t split = 0; split <= sizeof(wire); ++split) {
		CHECK(st_cterm_reset_terminal(f.terminal) == 0); prepare(&f,&p);
		size_t offset = 0, size = sizeof(wire)-1;
		while (offset < size) {
			size_t count = split > size ? 1 : offset < split ? split-offset : size-offset, used;
			CHECK(st_cterm_write(f.terminal,wire+offset,count,&used) == 0);
			offset += used; finish(&f);
		}
		CHECK(igs->attributes.writing_mode == 2 && f.terminal->xpos == 3);
		struct st_console_cell cells[2]; const struct st_console_rect rect = {0,0,2,1};
		CHECK(st_console_read_cells(f.console,&rect,cells,2) == 0 && cells[0].glyph == 'A' && cells[1].glyph == 'B');
	}
	const char randomized[] = "G#X>r,1,3,R,0:M 2:B";
	for (size_t split = 0; split <= sizeof(randomized); ++split) {
		CHECK(st_cterm_reset_terminal(f.terminal) == 0);
		igs->random_seed = 535; igs->random_range[0][1] = 255;
		igs->random_range[1][0] = 1; igs->random_range[1][1] = 2;
		size_t offset = 0, size = sizeof(randomized)-1;
		while (offset < size) {
			size_t count = split > size ? 1 : offset < split ? split-offset : size-offset, used;
			CHECK(st_cterm_write(f.terminal,randomized+offset,count,&used) == 0);
			offset += used;
			if (igs->palette_rotation.active) CHECK(igs->random_seed == UINT32_C(0x64e202b5) && igs->palette_rotation.remaining == 3);
			finish(&f);
		}
		CHECK(igs->random_seed == UINT32_C(0x64e202b5) && igs->attributes.writing_mode == 2);
	}
	static const char *const partial[] = {"G#X 8", "G#X 8,", "G#X 8,16,", "G#X 8,16,16,1,", "G#X 8,16,16,1,1"};
	for (size_t i = 0; i < sizeof(partial)/sizeof(*partial); ++i) {
		feed(&f,partial[i]); CHECK(st_cterm_finish(f.terminal) == 0 && !igs->palette_rotation.restore_raw);
		feed(&f,partial[i]); local_ms += 5001; CHECK(st_cterm_poll(f.terminal) == 0 && !igs->palette_rotation.restore_raw);
	}
	const char invalid[] = "G#X 8,16,16,1,10000:"; size_t used;
	CHECK(st_cterm_write(f.terminal,invalid,sizeof(invalid)-1,&used) == IO_ERROR_OUT_OF_RANGE && !igs->palette_rotation.restore_raw);
	/* Completed no-op and restore headers have no clock dependency. */
	igs->now_ms = bad_clock;
	feed(&f,"G#X 8,1,1,1,1:"); feed(&f,"G#X 8,1,3,0,0:");
	const char active[] = "G#X 8,1,3,1,0:";
	CHECK(st_cterm_write(f.terminal,active,sizeof(active)-1,&used) == IO_ERROR_IO && !igs->palette_rotation.active);
	igs->now_ms = now; feed(&f,active); igs->now_ms = bad_clock;
	CHECK(st_cterm_poll(f.terminal) == IO_ERROR_IO && igs->palette_rotation.active);
	igs->now_ms = now; finish(&f);
	local_ms = UINT64_MAX-1;
	CHECK(st_cterm_write(f.terminal,active,sizeof(active)-1,&used) == IO_ERROR_OUT_OF_RANGE && !igs->palette_rotation.active);
	local_ms = 1000; igs->sync_wait_hz = 0;
	CHECK(st_cterm_write(f.terminal,active,sizeof(active)-1,&used) == IO_ERROR_INVALID_ARGUMENT && !igs->palette_rotation.active);
	igs->sync_wait_hz = 1001;
	CHECK(st_cterm_write(f.terminal,active,sizeof(active)-1,&used) == IO_ERROR_INVALID_ARGUMENT && !igs->palette_rotation.active);
	igs->sync_wait_hz = 60;
	/* Failed scheduling after a committed write cancels pending ownership. */
	feed(&f,active); local_ms = UINT64_MAX-1;
	CHECK(st_cterm_poll(f.terminal) == IO_ERROR_OUT_OF_RANGE && !igs->palette_rotation.active && !st_cterm_input_paused(f.terminal));
	/* Overflow when scheduling the post-pass delay preserves the final
	 * write, releases suspension and leaves the next command usable. */
	local_ms = 1000; request(&f,1,2,1,9999);
	for (unsigned i = 0; i < 2; ++i) { local_ms = igs->wait_deadline; CHECK(st_cterm_poll(f.terminal) == 0); }
	local_ms = UINT64_MAX-1;
	CHECK(st_cterm_poll(f.terminal) == IO_ERROR_OUT_OF_RANGE && !igs->palette_rotation.active && !st_cterm_input_paused(f.terminal));
	/* 32767 no longer loops forever; n+1 still applies. No wall-clock sleeps. */
	local_ms = 1000; request(&f,1,2,32767,0); finish(&f);
	CHECK(!igs->palette_rotation.active);
	destroy(&f);
}

int
st_cterm_igs_palette_rotation_test_main(void)
{
	matrices(); snapshots(); snapshot_resolution(); keys_and_lifetime(); errors_and_partitions(); return 0;
}

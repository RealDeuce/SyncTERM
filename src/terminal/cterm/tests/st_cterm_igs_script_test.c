/* Literal X9 loader results from IG221.ACC, chapters 21 and 173. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "igs_test_fixture.h"
#include "cterm_igs_music.h"
#include "cterm_igs_script.h"
#include "st_igs_image.h"

static unsigned responses;
static uint8_t output[256], user_bytes[256];
static size_t user_count;
static struct cterm_igs *live;
static bool mutate,reset_reply;
static void response(const char *bytes, size_t count, void *context)
{
	(void)context;CHECK(count<=sizeof(output)-responses);
	memcpy(output+responses,bytes,count);responses += (unsigned)count;
	if(mutate && live!=NULL) live->music->bytes[8]='4';
	if(reset_reply && live!=NULL) cterm_igs_reset(live);
}
static void user(const char *bytes,size_t count,void *context)
{(void)context;CHECK(count<=sizeof(user_bytes)-user_count);memcpy(user_bytes+user_count,bytes,count);user_count+=count;}
static int bad_clock(uint64_t *value) {(void)value; return IO_ERROR_IO;}
static void prepare(struct cterm_igs *igs)
{
	cterm_igs_script_cancel(igs);
	memset(igs->music->bytes, 0xa5, sizeof(igs->music->bytes));
	igs->music->count = 73; igs->music->script_length = 99;
	igs->input.key_count = 0; responses = 0; local_ms = 1000;
	CHECK(cterm_igs_script_load(igs) == 0);
	CHECK(igs->script.loading && igs->script.received == 0);
	CHECK(igs->music->script_length == 0 && igs->music->count == 73);
	CHECK(igs->script.deadline == 6000 && igs->music->bytes[0] == 0xa5);
}
static void fragments(struct cterm_igs *igs, const uint8_t *wire, size_t size,
    size_t first, size_t expected_used)
{
	size_t used = 0, total = 0; bool end = false;
	CHECK(cterm_igs_script_bytes(igs, wire, first, &used, &end) == 0);
	CHECK(used <= first); total += used;
	if (!end) {
		CHECK(cterm_igs_script_bytes(igs, wire + first, size - first, &used, &end) == 0);
		total += used;
	}
	CHECK(end && !igs->script.loading && total == expected_used);
	CHECK(igs->script.received == expected_used && igs->script.deadline == 0);
	CHECK(responses == 0 && igs->music->count == 73);
}
static void literal_loads(void)
{
	struct fixture f; create(&f, 0, false);
	feed(&f, "G#M 1:");
	const struct st_console_rect point = {20, 20, 1, 1};
	seed(&f, &point, 771, 772);
	struct cterm_igs *igs = f.terminal->igs_engine;
	f.terminal->response_cb = response;
	static const struct {
		const char *wire; size_t consumed; uint32_t length;
		uint8_t bytes[12]; size_t stored;
	} vectors[] = {
		{"ABC||}suffix", 6, 2, {'A','B','C','|','|'}, 5},
		{"A||}suffix", 4, 0, {'A','|','|'}, 3},
		/* Intentional safety correction: do not retain U32 underflow. */
		{"||}suffix", 3, 0, {'|','|'}, 2},
		{"A||XB||}suffix", 8, 4, {'A','|','|',0xa5,'B','|','|'}, 7},
		{"A|||}B||}suffix", 9, 5, {'A','|','|',0xa5,'}','B','|','|'}, 8},
	};
	for (size_t i = 0; i < sizeof(vectors)/sizeof(*vectors); ++i) {
		size_t size = strlen(vectors[i].wire);
		for (size_t split = 0; split <= size; ++split) {
			prepare(igs);
			fragments(igs, (const uint8_t *)vectors[i].wire, size, split, vectors[i].consumed);
			CHECK(igs->music->script_length == vectors[i].length);
			CHECK(memcmp(igs->music->bytes, vectors[i].bytes, vectors[i].stored) == 0);
			CHECK(igs->music->bytes[vectors[i].stored] == 0xa5);
		}
	}
	/* Attention, NUL, Ctrl-C/X, CR/LF and high bytes remain opaque. Only
	 * the local key queue, which X9 never polls while loading, is separate. */
	static const uint8_t binary[] = {'G','#',0,3,24,13,10,255,'|','|','}','Z'};
	for (size_t split = 0; split <= sizeof(binary); ++split) {
		prepare(igs);
		igs->input.keys[0] = (struct cterm_igs_key){.local = 3};
		igs->input.key_count = 1;
		fragments(igs, binary, sizeof(binary), split, 11);
		CHECK(igs->music->script_length == 7 && igs->input.key_count == 1);
		CHECK(memcmp(igs->music->bytes, binary, 10) == 0);
	}
	prepare(igs);
	for (size_t i = 0; i < 11; ++i) {
		size_t used; bool end;
		CHECK(cterm_igs_script_bytes(igs, binary + i, 1, &used, &end) == 0 && used == 1);
		CHECK(end == (i == 10));
	}
	CHECK(igs->music->script_length == 7 && memcmp(igs->music->bytes, binary, 10) == 0);
	struct st_pixel_buffer *pixel = NULL;
	CHECK(st_console_get_pixels(&pixel, f.console, &point, false) == 0);
	CHECK(pixel->primary[0] == 771 && pixel->alternate[0] == 772);
	st_pixel_buffer_destroy(pixel);
	destroy(&f);
}
static void capacity(void)
{
	struct fixture f; create(&f, 0, false);
	struct cterm_igs *igs = f.terminal->igs_engine;
	uint8_t wire[10006]; memset(wire, 'a', sizeof(wire));
	for (size_t split = 0; split <= 10000; split += 1000) {
		prepare(igs); fragments(igs, wire, sizeof(wire), split, 10000);
		CHECK(igs->music->script_length == 9999);
		for (size_t i = 0; i < 10000; ++i) CHECK(igs->music->bytes[i] == 'a');
		CHECK(igs->music->bytes[10000] == 0xa5);
	}
	/* Failed lookahead at the capacity edge leaves the last stored byte
	 * and a hole; logical length is 10000 rather than 9999. */
	wire[9998] = wire[9999] = '|'; wire[10000] = 'X';
	for (size_t split = 9997; split <= 10002; ++split) {
		prepare(igs); fragments(igs, wire, sizeof(wire), split, 10001);
		CHECK(igs->music->script_length == 10000);
		CHECK(igs->music->bytes[9998] == '|' && igs->music->bytes[9999] == '|');
		CHECK(igs->music->bytes[10000] == 0xa5);
	}
	destroy(&f);
}
static void clear_cancel_and_deadlines(void)
{
	struct fixture f; create(&f, 0, false);
	struct cterm_igs *igs = f.terminal->igs_engine;
	prepare(igs);
	size_t used; bool end;
	CHECK(cterm_igs_script_bytes(igs, (const uint8_t *)"AB|", 3, &used, &end) == 0 && used == 3 && !end);
	CHECK(igs->music->script_length == 3);
	cterm_igs_script_cancel(igs);
	CHECK(!igs->script.loading && igs->script.deadline == 0 && igs->music->script_length == 3);
	CHECK(igs->music->bytes[0] == 'A' && igs->music->count == 73);
	cterm_igs_script_clear(igs);
	CHECK(igs->music->script_length == 0 && igs->music->count == 0);
	for (size_t i = 0; i < sizeof(igs->music->bytes); ++i) CHECK(igs->music->bytes[i] == 0);
	/* A zero logical length is a no-op even with retained nonzero data. */
	igs->music->bytes[0] = 42; igs->music->count = 73;
	cterm_igs_script_clear(igs);
	CHECK(igs->music->bytes[0] == 42 && igs->music->count == 73);
	/* The guard prevents recursive X9 loads/clears from touching storage. */
	igs->music->script_length = 2; igs->script.active = true;
	CHECK(cterm_igs_script_load(igs) == 0 && !igs->script.loading);
	cterm_igs_script_clear(igs);
	CHECK(igs->music->script_length == 2 && igs->music->bytes[0] == 42 && igs->music->count == 73);
	cterm_igs_script_cancel(igs);
	prepare(igs);
	local_ms = 5999;
	CHECK(cterm_igs_script_bytes(igs, (const uint8_t *)"A", 1, &used, &end) == 0 && used == 1);
	CHECK(igs->script.deadline == 10999 && igs->music->bytes[0] == 'A');
	local_ms = 10999;
	CHECK(cterm_igs_script_bytes(igs, (const uint8_t *)"BC", 2, &used, &end) == 0 && used == 2);
	CHECK(igs->music->bytes[1] == ' ' && igs->music->bytes[2] == 'C');
	CHECK(igs->script.deadline == 15999 && igs->music->script_length == 3);
	/* A failed clock does not consume input; the same bytes can be retried. */
	igs->now_ms = bad_clock;
	CHECK(cterm_igs_script_bytes(igs, (const uint8_t *)"Z", 1, &used, &end) == IO_ERROR_IO && used == 0);
	CHECK(igs->music->script_length == 3 && igs->music->bytes[3] == 0xa5);
	igs->now_ms = now; cterm_igs_script_cancel(igs);
	local_ms = UINT64_MAX - 1;
	CHECK(cterm_igs_script_load(igs) == 0 && igs->script.deadline == UINT64_MAX);
	CHECK(cterm_igs_script_bytes(igs, NULL, 0, &used, &end) == 0 && used == 0 && !end);
	CHECK(cterm_igs_script_bytes(igs, (const uint8_t *)"Q", 1, &used, &end) == 0 && igs->music->bytes[0] == 'Q');
	cterm_igs_script_cancel(igs);
	CHECK(cterm_igs_script_bytes(igs, (const uint8_t *)"Z", 1, &used, &end) == IO_ERROR_PROTOCOL && used == 0 && !end);
	/* Even an invalid retained logical length cannot enlarge a clear. */
	igs->music->script_length = UINT32_MAX;
	cterm_igs_script_clear(igs);
	CHECK(igs->music->script_length == 0 && igs->music->count == 0);
	for (size_t i = 0; i < sizeof(igs->music->bytes); ++i) CHECK(igs->music->bytes[i] == 0);
	/* N really writes the same allocation and retains the independent
	 * script length. Clearing a script also resets N's stored byte count. */
	local_ms = 1000;
	igs->music->script_length = 17;
	feed(&f, "G#N 0,3:XYZ");
	CHECK(memcmp(igs->music->bytes, "XYZ", 3) == 0 && igs->music->script_length == 17 && igs->music->count == 3);
	cterm_igs_script_clear(igs);
	CHECK(igs->music->count == 0 && igs->music->script_length == 0 && igs->music->bytes[0] == 0);
	destroy(&f);
}
static void upload(struct fixture *f, const char *script)
{
	char wire[10200];
	int size=snprintf(wire,sizeof(wire),"G#X 9,0:%s!||}",script);
	CHECK(size>0 && (size_t)size<sizeof(wire));
	feed(f,wire);
	CHECK(f->terminal->igs_engine->music->script_length==strlen(script));
}
static void wire_execution(void)
{
	struct fixture f; create(&f,0,false);
	struct cterm_igs *igs=f.terminal->igs_engine;
	f.terminal->response_cb=response; responses=0;
	upload(&f,"G#M>3:X 10,7,8:");
	CHECK(igs->attributes.writing_mode==1);
	feed(&f,"G#X>9,1:C 1,2:");
	CHECK(!igs->script.active && igs->attributes.writing_mode==3 && igs->draw_x==7 && igs->draw_y==8 && igs->attributes.line_pen==2);
	CHECK(responses==0);
	upload(&f,"G#X>9,2:X>9,0:X>9,1:M 4:");
	size_t length=igs->music->script_length;
	feed(&f,"G#X 9,1:");
	CHECK(!igs->script.active && igs->attributes.writing_mode==4 && igs->music->script_length==length);
	feed(&f,"G#X 9,2:");
	CHECK(igs->music->script_length==0 && igs->music->count==0);
	for(size_t i=0;i<sizeof(igs->music->bytes);++i) CHECK(igs->music->bytes[i]==0);
	destroy(&f);
}
static void run(struct fixture *f)
{
	feed(f,"G#X 9,1:");
	for(unsigned i=0;i<200 && f->terminal->igs_engine->script.active;++i) CHECK(st_cterm_poll(f->terminal)==0);
}
static void fields_and_loops(void)
{
	struct fixture f;create(&f,0,false);struct cterm_igs *igs=f.terminal->igs_engine;
	f.terminal->response_cb=response;responses=0;
	upload(&f,"G#& 0,2,1,0,P,2,x,7:");
	igs->text_counter=31;run(&f);
	CHECK(!igs->script.active && !igs->loop.run && igs->loop_mode==0 && igs->loop.current==2 && igs->text_counter==31 && responses==0);
	const struct st_console_rect area={0,7,3,1};struct st_pixel_buffer *pixels=NULL;
	CHECK(st_console_get_pixels(&pixels,f.console,&area,false)==0);
	for(unsigned i=0;i<3;++i) CHECK(pixels->primary[i]==783 && pixels->alternate[i]==783);
	st_pixel_buffer_destroy(pixels);
	/* File unknown loops clear the shared mode at the graphics entry exit. */
	upload(&f,"G#& 0,2,1,0,~,1,x:");run(&f);CHECK(igs->loop_mode==0 && !igs->loop.run);
	upload(&f,"G#E>0,9,0:W 40,60,A");
	memcpy(igs->shared_text,"old",4);igs->shared_text_size=3;run(&f);
	CHECK(!igs->script.active && memcmp(igs->shared_text,"Ald",4)==0 && igs->shared_text_size==3 && igs->text_deadline==0 && responses==0);
	CHECK(igs->script.cursor==(size_t)igs->music->script_length+1 && igs->script.parsed==igs->script.cursor);
	/* The NUL consumes one file text slot and preserves the suffix. */
	static const uint8_t text[]="G#W 40,60,A\0G#M 4:";
	memcpy(igs->music->bytes,text,sizeof(text)-1);igs->music->script_length=sizeof(text)-1;
	memcpy(igs->shared_text,"old",4);igs->shared_text_size=3;run(&f);
	CHECK(memcmp(igs->shared_text,"Ald",4)==0 && igs->attributes.writing_mode==4);
	/* NUL command or chain skips the body after acquiring both words. */
	static const uint8_t headers[]="G#\0>M\0G#M 2:";
	memcpy(igs->music->bytes,headers,sizeof(headers)-1);igs->music->script_length=sizeof(headers)-1;run(&f);
	CHECK(igs->attributes.writing_mode==2 && !igs->script.active);
	/* A truncated numeric command returns safely to the caller's chain. */
	upload(&f,"G#P 1");feed(&f,"G#X>9,1:M 3:");
	CHECK(!igs->script.active && igs->attributes.writing_mode==3);
	/* Serial & keeps only its own dispatch/flow boundary across X9. The
	 * file entry uses and changes the very same operand/index state. */
	upload(&f,"G#M 4:");responses=0;
	feed(&f,"G#& 0,2,1,0,X,2,9,1:");
	CHECK(!igs->script.active && !igs->loop.run && igs->loop_mode==0 && igs->attributes.writing_mode==1 && igs->loop.current==2);
	CHECK(responses==2 && output[0]==19 && output[1]==17);
	/* The file & replaces global operands without allocating a second
	 * array; the enclosing serial dispatch still balances its own flow. */
	upload(&f,"G#& 40,42,1,0,P,2,x,90:");responses=0;
	feed(&f,"G#& 0,2,1,0,X,2,9,1:");
	CHECK(!igs->script.active && !igs->loop.run && igs->loop_mode==0 && igs->loop.current==42);
	CHECK(igs->loop.operands[0]==-32767 && igs->loop.operands[1]==90 && igs->loop.index==0);
	CHECK(responses==2 && output[0]==19 && output[1]==17);
	/* Repeated X9 loads take opaque bytes from the caller rather than
	 * interpreting those bytes as another numeric or attention source. */
	responses=0;feed(&f,"G#& 0,2,1,0,X,2,9,0:ABC||}DEF||}");
	CHECK(!igs->loop.run && igs->music->script_length==2 && memcmp(igs->music->bytes,"DEF||",5)==0);
	CHECK(responses==2 && output[0]==19 && output[1]==17);
	destroy(&f);
}
static void waits_and_keys(void)
{
	struct fixture f;create(&f,0,false);struct cterm_igs *igs=f.terminal->igs_engine;
	f.terminal->response_cb=response;f.terminal->keystroke_cb=user;responses=0;user_count=0;
	upload(&f,"G#q>1:M 3:");feed(&f,"G#X 9,1:");
	CHECK(igs->script.active && igs->wait_deadline==1034 && igs->attributes.writing_mode==1);
	local_ms=1033;CHECK(st_cterm_poll(f.terminal)==0 && igs->script.active);
	local_ms=1034;CHECK(st_cterm_poll(f.terminal)==0);
	CHECK(st_cterm_poll(f.terminal)==0 && !igs->script.active && igs->attributes.writing_mode==3);
	/* Outer source keys are polled after a whole graphics entry. Ctrl-C
	 * during the explicit Ctrl-S pause cannot replace its Ctrl-Q release. */
	upload(&f,"G#q 1:AG#M 4:");feed(&f,"G#X 9,1:");
	CHECK(st_cterm_handle_key(f.terminal,19,0));local_ms+=34;
	CHECK(st_cterm_poll(f.terminal)==0 && st_cterm_poll(f.terminal)==0 && igs->script.paused);
	size_t cursor=igs->script.cursor;
	CHECK(st_cterm_handle_key(f.terminal,3,0));CHECK(st_cterm_poll(f.terminal)==0);
	CHECK(igs->script.paused && igs->script.cursor==cursor);
	CHECK(st_cterm_handle_key(f.terminal,17,0));CHECK(st_cterm_poll(f.terminal)==0);
	CHECK(!igs->script.active && igs->attributes.writing_mode==4 && user_count==0);
	upload(&f,"G#q 1:AG#M 1:");feed(&f,"G#X 9,1:");CHECK(st_cterm_handle_key(f.terminal,3,0));
	local_ms+=34;CHECK(st_cterm_poll(f.terminal)==0 && st_cterm_poll(f.terminal)==0);
	CHECK(!igs->script.active && igs->attributes.writing_mode==4 && user_count==0);
	/* Collection stays pending through its file chain, then echoes the
	 * sent byte and CR despite a silent collection option. */
	upload(&f,"G#<>1,0,0:q 1:");feed(&f,"G#X 9,1:");CHECK(igs->collect.active && igs->script.active);
	CHECK(st_cterm_handle_key(f.terminal,'Z',0));
	CHECK(st_cterm_poll(f.terminal)==0 && igs->wait_deadline!=0 && user_count==0);
	local_ms+=34;CHECK(st_cterm_poll(f.terminal)==0 && st_cterm_poll(f.terminal)==0);
	CHECK(!igs->script.active && user_count==2 && memcmp(user_bytes,"Z\r",2)==0);
	CHECK(f.terminal->xpos==1);
	/* Reset/disconnect cancel local ownership without resuming script. */
	upload(&f,"G#q 100:AG#M 1:");feed(&f,"G#X 9,1:");CHECK(igs->script.active);
	CHECK(st_cterm_finish(f.terminal)==0 && !igs->script.active && !cterm_igs_paused(igs));
	feed(&f,"G#M 2:");CHECK(igs->attributes.writing_mode==2);
	upload(&f,"G#q 100:AG#M 1:");feed(&f,"G#X 9,1:");cterm_igs_reset(igs);
	CHECK(!igs->script.active && igs->music->script_length==0 && !cterm_igs_paused(igs));
	destroy(&f);
}
static void source_storage(void)
{
	struct fixture f;create(&f,0,false);struct cterm_igs *igs=f.terminal->igs_engine;
	f.terminal->response_cb=response;responses=0;
	upload(&f,"G#?>0:M 3:");live=igs;mutate=true;run(&f);mutate=false;live=NULL;
	CHECK(igs->attributes.writing_mode==4 && responses==4 && memcmp(output,"2.21",4)==0);
	/* Each operand is evaluated once: the pinned native byte rejection
	 * trace takes 78 draws to9, then451 to0, rather than previewing twice. */
	igs->random_seed=1;igs->random_range[0][0]=igs->random_range[0][1]=9;
	igs->random_range[1][0]=igs->random_range[1][1]=0;
	feed(&f,"G#X r,R:ABC||}");
	if(igs->random_seed!=UINT32_C(0xd3f5005e) || igs->music->script_length!=2)
		fprintf(stderr,"X9 random prefix seed=%08" PRIx32 " length=%" PRIu32 " bytes=%02x,%02x,%02x,%02x,%02x\n",igs->random_seed,igs->music->script_length,igs->music->bytes[0],igs->music->bytes[1],igs->music->bytes[2],igs->music->bytes[3],igs->music->bytes[4]);
	CHECK(igs->random_seed==UINT32_C(0xd3f5005e) && igs->music->script_length==2 && memcmp(igs->music->bytes,"ABC||",5)==0);
	/* File auxiliary readers neither sample the receive clock nor send the
	 * serial auxiliary XON. Binary data stays opaque through their discard. */
	static const uint8_t macro[]="G#X>3,2,1,0,3:A\0B!M 2:";
	memcpy(igs->music->bytes,macro,sizeof(macro)-1);igs->music->script_length=sizeof(macro)-1;
	igs->now_ms=bad_clock;run(&f);igs->now_ms=now;
	CHECK(!igs->script.active && igs->attributes.writing_mode==2 && igs->input.length==3 && memcmp(igs->input.bytes,"A\0B",3)==0);
	/* N overwrites the same allocation while preserving script length. */
	upload(&f,"G#N>0,3:XYZM 3:");size_t length=igs->music->script_length;
	igs->now_ms=bad_clock;run(&f);igs->now_ms=now;
	CHECK(!igs->script.active && igs->music->script_length==length && igs->music->count==3 && memcmp(igs->music->bytes,"XYZ",3)==0 && igs->attributes.writing_mode==3);
	/* A 4000-byte file bitmap section contains all byte values, including
	 * NUL and attention, and is imported once at the existing format edge. */
	static const char prefix[]="G#X>11,2,1:";
	size_t first=sizeof(prefix)-1;memcpy(igs->music->bytes,prefix,first);
	uint8_t expected[4000],actual[4000];
	for(size_t i=0;i<sizeof(expected);++i) expected[i]=(uint8_t)i;
	memcpy(igs->music->bytes+first,expected,sizeof(expected));
	memcpy(igs->music->bytes+first+sizeof(expected),"M 4:",4);
	igs->music->script_length=(uint32_t)(first+sizeof(expected)+4);
	igs->bitmap_errors=3;igs->now_ms=bad_clock;run(&f);igs->now_ms=now;
	/* File X11 bypasses serial receive errors and does not clear them. */
	CHECK(!igs->script.active && igs->attributes.writing_mode==4 && igs->bitmap_errors==3);
	CHECK(st_igs_image_export(igs->memory[0],0,actual,sizeof(actual))==0 && memcmp(actual,expected,sizeof(actual))==0);
	/* A truncated file load preserves its accepted stores and the old
	 * serial error count when the source owner unwinds its reader. */
	static const uint8_t partial[]="G#X 11,2,1:A";
	memcpy(igs->music->bytes,partial,sizeof(partial)-1);igs->music->script_length=sizeof(partial)-1;run(&f);
	CHECK(!igs->script.active && !igs->bitmap_loading && igs->bitmap_errors==3);
	CHECK(st_igs_image_export(igs->memory[0],0,actual,1)==0 && actual[0]=='A');
	/* The next serial load still observes that threshold after its first
	 * store, then clears it at its own serial byte-phase completion. */
	feed(&f,"G#X 11,2,1:Z");
	CHECK(!igs->bitmap_loading && igs->bitmap_errors==0);
	CHECK(st_igs_image_export(igs->memory[0],0,actual,1)==0 && actual[0]=='Z');
	destroy(&f);
}
static void file_snapshots(void)
{
	for(unsigned mode=0;mode<3;++mode) {
		struct fixture f;create(&f,mode,false);struct cterm_igs *igs=f.terminal->igs_engine;
		CHECK(cterm_igs_palette_pen(igs,1,100,200,300)==0);feed(&f,"G#X 8,1,1,1,1:");
		CHECK(cterm_igs_palette_pen(igs,1,400,500,600)==0);upload(&f,"G#X 8,1,1,1,1:");run(&f);
		CHECK(cterm_igs_palette_pen(igs,1,700,800,900)==0);upload(&f,"G#X 8,0,0,0,0:");run(&f);
		CHECK(igs->requested_palette[1][0]==400 && igs->requested_palette[1][1]==500 && igs->requested_palette[1][2]==600);
		feed(&f,"G#X 8,0,0,0,0:");
		CHECK(igs->requested_palette[1][0]==100 && igs->requested_palette[1][1]==200 && igs->requested_palette[1][2]==300);
		/* I retains both separate requested-colour snapshots. */
		feed(&f,"G#I 2:");upload(&f,"G#X 8,0,0,0,0:");run(&f);
		CHECK(igs->requested_palette[1][0]==400 && igs->requested_palette[1][2]==600);
		feed(&f,"G#X 8,0,0,0,0:");CHECK(igs->requested_palette[1][0]==100);
		/* Hardware snapshot and raw/VDI restore selector are shared across
		 * the two paths, even though their requested-colour arrays differ. */
		CHECK(cterm_igs_palette_register(igs,7,0x123)==0);feed(&f,"G#X 8,16,16,1,1:");
		CHECK(cterm_igs_palette_register(igs,7,0x456)==0);upload(&f,"G#X 8,0,0,0,0:");run(&f);
		CHECK(igs->raw_palette[7]==0x123 && !igs->palette_rotation.restore_raw);
		CHECK(cterm_igs_palette_register(igs,7,0x234)==0);upload(&f,"G#X 8,16,16,1,1:");run(&f);
		CHECK(cterm_igs_palette_register(igs,7,0x567)==0);feed(&f,"G#X 8,0,0,0,0:");
		CHECK(igs->raw_palette[7]==0x234 && !igs->palette_rotation.restore_raw);
		CHECK(st_cterm_reset_terminal(f.terminal)==0);
		CHECK(cterm_igs_palette_pen(igs,1,700,800,900)==0);upload(&f,"G#X 8,0,0,0,0:");run(&f);
		CHECK(igs->requested_palette[1][0]==0 && igs->requested_palette[1][1]==0 && igs->requested_palette[1][2]==0);
		destroy(&f);
	}
}
static void file_scatter(void)
{
	for(unsigned mode=0;mode<3;++mode) {
		struct fixture f;create(&f,mode,false);struct cterm_igs *igs=f.terminal->igs_engine;
		igs->marker_pen_cache=1;cterm_igs_set_marker_color(igs,0);
		feed(&f,"G#X 0,3,0,0,0,0:");
		CHECK(igs->attributes.marker_pen==(mode==2?1:0));
		cterm_igs_set_marker_color(igs,0);uint32_t seed=igs->random_seed;
		upload(&f,"G#X 0,3,0,0,0,0:");run(&f);
		CHECK(igs->attributes.marker_pen==1 && igs->random_seed==seed);
		CHECK(igs->marker_cycle==(mode==2?0:3) && igs->marker_cycle_restart==(mode==2?0:3));
		destroy(&f);
	}
}
static void file_flow(void)
{
	static const char *const scripts[]={"G#t 0:","G#F 9999,9999:",
	    "G#f 3,9999,9999,9998,9999,9999,9998:","G#z 2,40,80,42,80:",
	    "G#N>0,0:N 2:","G#N 4,0:"};
	struct fixture f;create(&f,0,false);struct cterm_igs *igs=f.terminal->igs_engine;
	f.terminal->response_cb=response;
	for(size_t i=0;i<sizeof(scripts)/sizeof(*scripts);++i) {
		responses=0;upload(&f,scripts[i]);run(&f);
		if(responses!=0) fprintf(stderr,"file flow case%zu emitted %u bytes\n",i,responses);
		CHECK(!igs->script.active && responses==0 && !igs->music->flow && !igs->timed_pause);
	}
	destroy(&f);
}
static void invalid_type_exit(void)
{
	struct fixture f;create(&f,0,false);struct cterm_igs *igs=f.terminal->igs_engine;
	f.terminal->response_cb=response;responses=0;
	/* Invalid T kind jumps directly to chain postprocessing, even if its
	 * one numeric read left an enclosing & loop active. */
	feed(&f,"G#& 0,3,1,0,T,2,0,4:");
	CHECK(!igs->loop.run && igs->loop_mode==1 && igs->loop.current==1 && igs->loop.index==1);
	CHECK(responses==2 && output[0]==19 && output[1]==17);
	CHECK(st_cterm_reset_terminal(f.terminal)==0);responses=0;
	/* The remaining stored operand belongs to the next chained command.
	 * Its own M readers finish the loop before the literal 3: is echoed. */
	upload(&f,"G#&>0,3,1,0,T,2,0,4:M 3:");run(&f);
	CHECK(!igs->script.active && !igs->loop.run && igs->loop_mode==0 && igs->attributes.writing_mode==4);
	CHECK(responses==0 && igs->loop.current==3);
	const struct st_console_rect row={0,0,2,1};struct st_console_cell cells[2];
	CHECK(st_console_read_cells(f.console,&row,cells,2)==0 && cells[0].glyph=='3' && cells[1].glyph==':');
	destroy(&f);
}
static bool mouse(struct fixture *f,int x,int y,unsigned buttons)
{
	struct st_console_event e={.type=ST_CONSOLE_EVENT_MOUSE};e.data.mouse.action=ST_CONSOLE_MOUSE_BUTTON;
	e.data.mouse.pixel_x=x;e.data.mouse.pixel_y=y;e.data.mouse.x=x/8;e.data.mouse.y=y/8;e.data.mouse.buttons=buttons;
	return st_cterm_handle_mouse(f->terminal,&e);
}
static void asynchronous_owners(void)
{
	struct fixture f;create(&f,0,false);struct cterm_igs *igs=f.terminal->igs_engine;
	f.terminal->response_cb=response;f.terminal->keystroke_cb=user;responses=0;user_count=0;
	upload(&f,"G#?>2,1:M 3:");feed(&f,"G#X 9,1:");
	CHECK(igs->query.active && igs->script.active);
	CHECK(st_cterm_handle_text_key(f.terminal,'a'));
	CHECK(mouse(&f,20,30,1) && mouse(&f,40,50,0));
	for(unsigned i=0;i<5 && igs->script.active;++i) CHECK(st_cterm_poll(f.terminal)==0);
	CHECK(!igs->query.active && !igs->script.active && igs->attributes.writing_mode==3 && user_count==0);
	CHECK(responses==8 && memcmp(output,"40,50,1:",8)==0);
	for(unsigned mode=0;mode<3;++mode) {
		char script[40];unsigned next=mode%2;
		(void)snprintf(script,sizeof(script),"G#R>%u,0:M 4:",next);
		upload(&f,script);feed(&f,"G#X 9,1:");
		for(unsigned i=0;i<6 && igs->script.active;++i) {local_ms+=100;CHECK(st_cterm_poll(f.terminal)==0);}
		if(igs->script.active || igs->resolution!=next || igs->attributes.writing_mode!=4 || cterm_igs_paused(igs))
			fprintf(stderr,"script R next%u active%d resolution%u writing%d wait%" PRIu64 " pending%d ready%d\n",next,igs->script.active,igs->resolution,igs->attributes.writing_mode,igs->wait_deadline,igs->pending_resolution,igs->script.ready);
		CHECK(!igs->script.active && igs->resolution==next && igs->attributes.writing_mode==4 && !cterm_igs_paused(igs));
	}
	/* A nested source suspends a serial loop's own XOFF lifetime. EOF
	 * closes that lifetime once even when a file body currently owns q. */
	upload(&f,"G#q 100:");responses=0;
	feed(&f,"G#& 0,2,1,0,X,2,9,1:");CHECK(igs->script.active);
	CHECK(st_cterm_finish(f.terminal)==0 && !igs->script.active && !igs->loop.run);
	CHECK(responses==2 && output[0]==19 && output[1]==17);
	/* Reply callbacks may request reset while the source owns parser state. */
	upload(&f,"G#? 0:G#M 3:");responses=0;live=igs;reset_reply=true;
	feed(&f,"G#X 9,1:");reset_reply=false;live=NULL;
	CHECK(!igs->script.active && !igs->script.starting && igs->music->script_length==0 && !cterm_igs_paused(igs));
	/* Reset after the first transmitted byte suppresses the rest. */
	CHECK(responses==1 && output[0]=='2');
	CHECK(st_cterm_poll(f.terminal)==0 && igs->attributes.writing_mode==1);
	destroy(&f);
}
static void partitions(void)
{
	static const char wire[]="G#X>9,0:G#&>0,2,1,0,P,2,x,7:M 3:!||}X>9,1:C 1,2:";
	for(unsigned mode=0;mode<3;++mode) for(unsigned status=0;status<2;++status) {
		struct fixture f;create(&f,mode,status!=0);struct cterm_igs *igs=f.terminal->igs_engine;
		f.terminal->response_cb=response;
		for(unsigned encoding=0;encoding<3;++encoding) {
			CHECK(st_cterm_set_encoding(f.terminal,encoding==2?ST_CTERM_ENCODING_UTF8:ST_CTERM_ENCODING_CODEPAGE,encoding==0?ST_CODEPAGE_ATARI_ST:ST_CODEPAGE_CP437)==0);
			for(int scaling=0;scaling<3;++scaling) {
				cterm_igs_set_scaling(igs,scaling);
				int pen=mode==2?1:2;
				for(size_t split=0;split<sizeof(wire);++split) {
					size_t used;responses=0;
					CHECK(st_cterm_write(f.terminal,wire,split,&used)==0 && used==split);
					CHECK(st_cterm_write(f.terminal,wire+split,sizeof(wire)-1-split,&used)==0 && used==sizeof(wire)-1-split);
					if(igs->script.active || igs->loop.run || igs->loop_mode!=0 || igs->attributes.writing_mode!=3 || igs->attributes.line_pen!=pen || responses!=0)
						fprintf(stderr,"X9 partition mode%u status%u encoding%u scaling%d split%zu active%d run%d loop%d writing%d pen%d replies%u\n",mode,status,encoding,scaling,split,igs->script.active,igs->loop.run,igs->loop_mode,igs->attributes.writing_mode,igs->attributes.line_pen,responses);
					CHECK(!igs->script.active && !igs->loop.run && igs->loop_mode==0 && igs->attributes.writing_mode==3 && igs->attributes.line_pen==pen && responses==0);
				}
				for(size_t i=0;i<sizeof(wire)-1;++i) {size_t used;CHECK(st_cterm_write(f.terminal,wire+i,1,&used)==0 && used==1);}
				CHECK(!igs->script.active && igs->loop.current==2 && igs->loop.index==0);
				/* File attention echoes the first G of GG#, holds a final G,
				 * and omits serial destructive-backspace expansion. */
				feed(&f,"\033EG#k 2:");upload(&f,"GG#M 3:G");run(&f);
				const struct st_console_rect one={0,0,1,1};struct st_console_cell cell;
				CHECK(st_console_read_cells(f.console,&one,&cell,1)==0 && cell.glyph=='G');
				CHECK(f.terminal->xpos==2 && f.terminal->ypos==1);
				feed(&f,"\033E");upload(&f,"A\b");run(&f);
				CHECK(st_console_read_cells(f.console,&one,&cell,1)==0 && cell.glyph=='A');
				CHECK(f.terminal->xpos==1 && f.terminal->ypos==1);
				igs->music->bytes[0]=128;igs->music->script_length=1;run(&f);
				CHECK(st_console_read_cells(f.console,&one,&cell,1)==0 && cell.glyph==128 && f.terminal->xpos==2);
			}
		}
		destroy(&f);
	}
}
static void cooperative_sources(void)
{
	struct fixture f;create(&f,0,false);struct cterm_igs *igs=f.terminal->igs_engine;
	f.terminal->response_cb=response;f.terminal->keystroke_cb=user;responses=0;user_count=0;
	feed(&f,"\033v");upload(&f,"A");feed(&f,"G#& 0,130,1,0,X,2,9,1:");
	CHECK(cterm_igs_paused(igs) && igs->script.cooperative && igs->loop.current<130);
	for(unsigned i=0;i<8 && cterm_igs_paused(igs);++i) CHECK(st_cterm_poll(f.terminal)==0);
	CHECK(!cterm_igs_paused(igs) && !igs->loop.run && igs->loop.current==130);
	CHECK(!igs->script.active && !igs->script.starting && !igs->script.cooperative);
	if(f.terminal->xpos!=11 || f.terminal->ypos!=4) fprintf(stderr,"short sources cursor %d,%d\n",f.terminal->xpos,f.terminal->ypos);
	CHECK(f.terminal->xpos==11 && f.terminal->ypos==4);
	CHECK(responses==2 && output[0]==19 && output[1]==17);
	/* A batch of independent callers also retains its unread wire suffix
	 * while an accepted short-source call waits for the next owner pump. */
	char wire[1000];size_t size=0,offset=0;
	for(unsigned i=0;i<96;++i) {memcpy(wire+size,"G#X 9,1:",8);size+=8;}
	size_t used;CHECK(st_cterm_write(f.terminal,wire,size,&used)==0 && used<size);
	offset+=used;CHECK(igs->script.starting && igs->script.cooperative);
	CHECK(st_cterm_handle_key(f.terminal,'a',0) && user_count==0 && igs->input.key_count==1);
	for(unsigned pass=0;pass<8 && (offset<size || cterm_igs_paused(igs));++pass) {
		CHECK(st_cterm_poll(f.terminal)==0);
		if(!cterm_igs_paused(igs) && offset<size) {
			CHECK(st_cterm_write(f.terminal,wire+offset,size-offset,&used)==0);
			offset+=used;
		}
	}
	CHECK(offset==size && !cterm_igs_paused(igs) && !igs->script.starting && !igs->script.cooperative);
	CHECK(user_count==0 && igs->input.key_count==0);
	destroy(&f);
}
int main(void)
{
	literal_loads(); capacity(); clear_cancel_and_deadlines(); wire_execution();fields_and_loops();waits_and_keys();source_storage();file_snapshots();file_scatter();file_flow();invalid_type_exit();asynchronous_owners();partitions();cooperative_sources();
	return 0;
}

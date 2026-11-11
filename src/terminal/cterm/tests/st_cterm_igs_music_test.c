/* N: literal retained records, opaque input, local waits and MIDI ownership. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "igs_test_fixture.h"
#include "cterm_igs_music.h"
#include "cterm_igs_audio.h"
#include "music_internal.h"
#include "st_threads.h"
#include "st_time.h"

extern size_t st_igs_test_calls;
extern int st_igs_test_arguments[4];
extern unsigned st_igs_test_stops[3], st_igs_test_stop_count;
static uint8_t replies[512], keys[512];
static size_t reply_count, key_count;
static void reply(const char *bytes, size_t count, void *context)
{
	(void)context; CHECK(count <= sizeof(replies)-reply_count);
	memcpy(replies+reply_count,bytes,count); reply_count += count;
}
static void output(const char *bytes, size_t count, void *context)
{
	(void)context; CHECK(count <= sizeof(keys)-key_count);
	memcpy(keys+key_count,bytes,count); key_count += count;
}
static void setup(struct fixture *f)
{
	create(f,0,false); f->terminal->response_cb = reply; f->terminal->keystroke_cb = output;
	reply_count = key_count = 0; st_igs_test_calls = st_igs_test_stop_count = 0;
	const struct st_console_rect rect = {0,0,8,8}; seed(f,&rect,768,768);
}
static size_t write_bytes(struct fixture *f, const void *bytes, size_t count)
{
	size_t used; CHECK(st_cterm_write(f->terminal,bytes,count,&used) == 0); return used;
}
static void error_wire(struct fixture *f, const char *wire, int expected)
{
	size_t used; int status=st_cterm_write(f->terminal,wire,strlen(wire),&used);
	/* The receive boundary owns reporting versus log-and-continue policy.
	 * N's rejection and retained side effects are checked independently. */
	if (status!=0 && status!=expected) fprintf(stderr,"%s: status %d used %zu count %d\n",wire,status,used,f->terminal->igs_engine->music->count);
	CHECK(status==0 || status==expected);
}
static void load(struct fixture *f, int op, const uint8_t *bytes, size_t count)
{
	char prefix[64]; CHECK(snprintf(prefix,sizeof(prefix),"G#N %d,%zu:",op,count)>0);
	feed(f,prefix); CHECK(write_bytes(f,bytes,count)==count);
}
static void flow_pair(void)
{
	CHECK(reply_count == 2 && replies[0] == 19 && replies[1] == 17); reply_count=0;
}
static void poll_due(struct fixture *f)
{
	local_ms = f->terminal->igs_engine->wait_deadline;
	CHECK(st_cterm_poll(f->terminal) == 0);
}
static void loads(void)
{
	struct fixture f; setup(&f); struct cterm_igs *igs=f.terminal->igs_engine;
	struct cterm_igs_music *m=igs->music;
	CHECK(m != NULL && m->count==0 && m->midi_enabled==1 && m->script_length==0);
	for(size_t i=0;i<sizeof(m->bytes);++i) CHECK(m->bytes[i]==0);
	uint8_t payload[256]; for(unsigned i=0;i<256;++i) payload[i]=(uint8_t)i;
	m->script_length=37;
	for(size_t split=0;split<=sizeof(payload);++split) {
		feed(&f,"G#N 0,256:"); CHECK(m->count==256 && m->loading && m->received==0);
		CHECK(write_bytes(&f,payload,split)==split);
		CHECK(write_bytes(&f,payload+split,sizeof(payload)-split)==sizeof(payload)-split);
		CHECK(!m->loading && !m->command && memcmp(m->bytes,payload,sizeof(payload))==0);
		CHECK(m->script_length==37 && m->midi_enabled==1 && reply_count==0);
	}
	/* Every ordinary/unsupported operation loads; nonpositive counts do not. */
	m->midi_enabled=0;
	static const int operations[]={-32768,-1,0,1,3,4,7,32767};
	for(size_t k=0;k<sizeof(operations)/sizeof(*operations);++k) {
		igs->math[0]=(int16_t)operations[k]; igs->math[1]=-1;
		feed(&f,"G#N 0],1]:"); CHECK(m->count==-1 && !m->command);
		if(operations[k]==4) flow_pair(); else CHECK(reply_count==0);
	}
	/* Capacity is enforced after publishing the count, without overwriting data. */
	igs->math[0]=10002;
	CHECK(cterm_igs_music_load(igs,10002)==IO_ERROR_OUT_OF_RANGE);
	error_wire(&f,"G#N 0,0]:",IO_ERROR_OUT_OF_RANGE);
	CHECK(m->count==10002 && !m->loading && !m->command && memcmp(m->bytes,payload,256)==0);
	igs->math[0]=CTERM_IGS_MUSIC_BYTES; feed(&f,"G#N 0,0]:");
	for(size_t i=0;i<sizeof(m->bytes);++i) { uint8_t byte=(uint8_t)i; CHECK(write_bytes(&f,&byte,1)==1); }
	CHECK(m->count==10001 && m->bytes[10000]==16 && !m->command);
	/* A local abort follows the first store, leaves the raw suffix unread and
	 * does not interpret a payload Ctrl-C/X as a key. */
	feed(&f,"G#N 0,5:"); CHECK(st_cterm_handle_key(f.terminal,3,0));
	const uint8_t abort_data[]={24,'A','B','C','D'};
	CHECK(write_bytes(&f,abort_data,sizeof(abort_data))==sizeof(abort_data));
	CHECK(m->count==0 && m->bytes[0]==24 && m->bytes[1]==1 && m->script_length==37);
	CHECK(!m->command && reply_count==0 && key_count==0);
	/* Unread suffix is ordinary console data after early completion. */
	CHECK(f.terminal->xpos>=5);
	feed(&f,"G#N 0,2:"); CHECK(write_bytes(&f,abort_data,2)==2);
	CHECK(m->count==2 && m->bytes[0]==24 && m->bytes[1]=='A');
	/* Timeout substitutes one auxiliary space and continues, never X11 '*'. */
	feed(&f,"G#N 0,2:"); local_ms=m->receive_deadline;
	CHECK(st_cterm_poll(f.terminal)==0 && m->loading && m->received==1 && m->bytes[0]==' ');
	local_ms=m->receive_deadline; CHECK(st_cterm_poll(f.terminal)==0 && !m->command && m->bytes[1]==' ');
	/* EOF cancels a partial reader without inventing remaining source bytes. */
	feed(&f,"G#N 0,3:"); CHECK(write_bytes(&f,"z",1)==1);
	CHECK(st_cterm_finish(f.terminal)==0 && !m->command && m->count==3 && m->bytes[0]=='z' && m->bytes[1]==' ');
	destroy(&f);
}
static void chip(void)
{
	struct fixture f; setup(&f); struct cterm_igs *igs=f.terminal->igs_engine; struct cterm_igs_music *m=igs->music;
	/* Independent six-byte records: effect, voice, volume, pitch, delay, stop. */
	const uint8_t records[]={0,0,15,60,2,2, 1,1,12,64,0,0, 2,2,9,67,0,0};
	load(&f,0,records,sizeof(records));
	feed(&f,"G#N 5:"); CHECK(m->playing && igs->note.active && st_igs_test_calls==1);
	CHECK(reply_count==1 && replies[0]==19 && igs->wait_deadline==local_ms+10);
	local_ms+=9; CHECK(st_cterm_poll(f.terminal)==0 && st_igs_test_calls==1);
	++local_ms; CHECK(st_cterm_poll(f.terminal)==0 && !m->playing && st_igs_test_calls==3);
	CHECK(st_igs_test_stop_count==1 && st_igs_test_stops[0]==0); flow_pair();
	CHECK(m->count==18 && m->midi_enabled==1);
	/* N4 rounds a short record up through retained bytes; N5 skips <=5. */
	st_igs_test_calls=0; load(&f,4,(uint8_t[]){3},1);
	CHECK(m->playing && igs->note.active && st_igs_test_calls==1 && igs->note.stop==2);
	poll_due(&f); CHECK(!m->playing); flow_pair();
	feed(&f,"G#N 5:"); CHECK(reply_count==0 && st_igs_test_calls==1);
	/* N6 ignores current logical length after its >5 gate. */
	load(&f,0,records,6); st_igs_test_calls=0;
	feed(&f,"G#N 6,1,3:"); CHECK(st_igs_test_calls==2 && !m->playing); flow_pair();
	CHECK(memcmp(st_igs_test_arguments,(int[]){2,9,67,7},sizeof(st_igs_test_arguments))==0);
	feed(&f,"G#N 6,3,3:"); feed(&f,"G#N 6,0,1666:"); CHECK(reply_count==0);
	igs->math[0]=-1;
	CHECK(cterm_igs_music_execute(igs,-1,1)==IO_ERROR_OUT_OF_RANGE);
	error_wire(&f,"G#N 6,0],1:",IO_ERROR_OUT_OF_RANGE); CHECK(!m->command);
	/* The final retained record is safe; no padded out-of-buffer fetch. */
	memcpy(m->bytes+9984,(uint8_t[]){125,0,0,0,0,0},6);
	feed(&f,"G#N 6,1664,1665:"); flow_pair();
	m->count=10001; memset(m->bytes,0,sizeof(m->bytes)); st_igs_test_calls=0;
	error_wire(&f,"G#N 5:",IO_ERROR_OUT_OF_RANGE);
	CHECK(!m->playing && !m->command); flow_pair();
	/* Empty N4 still enters flow, independent of MIDI enable. */
	load(&f,4,NULL,0); flow_pair(); CHECK(m->midi_enabled==1);
	destroy(&f);
}
static void keyboard(void)
{
	struct fixture f; setup(&f); struct cterm_igs *igs=f.terminal->igs_engine; struct cterm_igs_music *m=igs->music;
	const uint8_t records[]={0,0,15,60,20,0, 1,1,12,64,20,0, 2,2,9,67,20,0};
	load(&f,0,records,sizeof(records)); feed(&f,"G#N 5:");
	/* Ctrl-C in the inner delay shortens it; the second queued Ctrl-X is
	 * consumed by the next outer poll and stops before triggering record 2. */
	CHECK(st_cterm_handle_key(f.terminal,3,0) && igs->note.key);
	CHECK(st_cterm_handle_codepage_key(f.terminal,24) && m->key_count==1);
	CHECK(st_cterm_poll(f.terminal)==0 && !m->playing && st_igs_test_calls==1); flow_pair();
	CHECK(m->count==18 && igs->audio->gist.voice[0].parameters[0]>0 && key_count==0);
	/* A + used by the inner note does not shift its next record. */
	feed(&f,"G#N 5:"); CHECK(st_cterm_handle_text_key(f.terminal,'+'));
	CHECK(st_cterm_poll(f.terminal)==0 && igs->note.active && igs->note.voice==1);
	CHECK(st_cterm_handle_doorway_key(f.terminal,59));
	CHECK(st_cterm_poll(f.terminal)==0 && igs->note.active && igs->note.voice==2);
	/* Events unused after the final inner wait return through their source. */
	CHECK(st_cterm_handle_text_key(f.terminal,0x1f600));
	CHECK(st_cterm_handle_codepage_key(f.terminal,'x'));
	CHECK(st_cterm_poll(f.terminal)==0 && !m->playing && key_count==1 && keys[0]=='x'); flow_pair();
	/* A second + queued behind an inner key shifts the NEXT record by one
	 * byte. These literal shifted records become (1,12,64,20,0,2) and
	 * (2,9,67,20,0,0), including one old byte beyond the loaded count. */
	st_igs_test_calls=0; feed(&f,"G#N 5:");
	CHECK(st_cterm_handle_key(f.terminal,3,0) && st_cterm_handle_text_key(f.terminal,'+'));
	CHECK(st_cterm_poll(f.terminal)==0 && !m->playing && m->index==19 && st_igs_test_calls==3);
	CHECK(memcmp(st_igs_test_arguments,(int[]){2,15,20,7},sizeof(st_igs_test_arguments))==0);
	CHECK(memcmp(m->bytes,records,sizeof(records))==0 && m->count==18); flow_pair();
	/* Replays 5/6 safely preserve even a noncanonical nonzero enable byte. */
	m->midi_enabled=255; m->count=0; feed(&f,"G#N 5:"); feed(&f,"G#N 6:");
	CHECK(m->midi_enabled==255 && reply_count==0);
	destroy(&f);
}
static int failed_now(uint64_t *value) { (void)value; return IO_ERROR_IO; }
static void boundaries(void)
{
	struct fixture f; setup(&f); struct cterm_igs *igs=f.terminal->igs_engine;
	struct cterm_igs_music *m=igs->music;
	const uint8_t packet[]={'G','#','N',' ','4',',','1','2',':',
	    125,0,0,0,1,0, 0,2,9,67,0,2, 'G','#','X',' ','1','0',',','7',',','9',':'};
	for(size_t split=0;split<=sizeof(packet);++split) {
		cterm_igs_reset(igs); reply_count=0; st_igs_test_calls=0;
		size_t offset=0;
		for(unsigned part=0;part<2;++part) {
			size_t end=part==0?split:sizeof(packet);
			while(offset<end) {
				if(st_cterm_input_paused(f.terminal)) poll_due(&f);
				size_t used=write_bytes(&f,packet+offset,end-offset); CHECK(used!=0); offset+=used;
			}
		}
		while(st_cterm_input_paused(f.terminal)) poll_due(&f);
		CHECK(m->count==12 && !m->command && st_igs_test_calls==1 && igs->draw_x==7 && igs->draw_y==9);
		flow_pair();
	}
	/* Acquisition/arity previews never advance the real random state. */
	cterm_igs_reset(igs); igs->random_range[0][1]=255;
	feed(&f,"G#N r,r:"); CHECK(m->count==202 && igs->random_seed==UINT32_C(0x5eb5ca17));
	const uint8_t random_payload[202]={0}; CHECK(write_bytes(&f,random_payload,sizeof(random_payload))==sizeof(random_payload));
	/* Byte fields remain unsigned through the shared numeric note helper. */
	st_igs_test_calls=st_igs_test_stop_count=0;
	load(&f,4,(uint8_t[]){255,255,255,255,255,255},6);
	CHECK(st_igs_test_calls==1 && memcmp(st_igs_test_arguments,(int[]){2,15,255,7},sizeof(st_igs_test_arguments))==0);
	CHECK(igs->wait_deadline==local_ms+1275 && igs->note.stop==4);
	igs->now_ms=failed_now; CHECK(st_cterm_poll(f.terminal)==IO_ERROR_IO && m->playing && igs->note.active);
	igs->now_ms=now; poll_due(&f); CHECK(!m->playing && st_igs_test_stop_count==3); flow_pair();
	/* Raw acquisition errors retain the count and previous accepted stores. */
	cterm_igs_audio_destroy(igs); igs->now_ms=failed_now;
	error_wire(&f,"G#N 0,7:",IO_ERROR_IO); CHECK(m->count==7 && !m->command && m->bytes[0]==255);
	igs->now_ms=now;
	/* Full reset clears retained bytes, both lengths, flag and paused input. */
	m->script_length=9; m->midi_enabled=255; feed(&f,"G#N 5:");
	cterm_igs_reset(igs); CHECK(!m->playing && !st_cterm_input_paused(f.terminal));
	CHECK(m->count==0 && m->script_length==0 && m->midi_enabled==1);
	for(size_t i=0;i<sizeof(m->bytes);++i) CHECK(m->bytes[i]==0);
	destroy(&f);
}
static void reset_reply(const char *bytes,size_t count,void *context)
{
	struct cterm_igs *igs=context; reply(bytes,count,NULL);
	if(count==1 && (uint8_t)bytes[0]==19) cterm_igs_reset(igs);
}
static void geometry_and_flow(void)
{
	for(unsigned resolution=0;resolution<3;++resolution) for(unsigned status=0;status<2;++status) {
		struct fixture f; create(&f,resolution,status!=0); f.terminal->response_cb=reply;
		struct cterm_igs *igs=f.terminal->igs_engine;
		feed(&f,"\033fG#p>3,2:X 10,19,17:");
		const struct st_console_rect rect={0,0,igs->width,igs->height}; seed(&f,&rect,769,768);
		struct cterm_igs_attributes attrs=igs->attributes; uint32_t palette[16]; memcpy(palette,igs->palette,sizeof(palette));
		for(int scaling=0;scaling<3;++scaling) {
			cterm_igs_set_scaling(igs,scaling); reply_count=0;
			load(&f,4,(uint8_t[]){2,1,10,60,1,2},6);
			CHECK(igs->wait_deadline==local_ms+5 && igs->note.voice==1 && igs->note.stop==2);
			poll_due(&f); flow_pair();
			CHECK(memcmp(st_igs_test_arguments,(int[]){1,10,60,7},sizeof(st_igs_test_arguments))==0);
		}
		CHECK(memcmp(&attrs,&igs->attributes,sizeof(attrs))==0 && memcmp(palette,igs->palette,sizeof(palette))==0);
		CHECK(igs->draw_x==19 && igs->draw_y==17 && f.terminal->xpos==4 && f.terminal->ypos==3);
		struct st_pixel_buffer *pixels=NULL; CHECK(st_console_get_pixels(&pixels,f.console,&rect,false)==0);
		for(size_t i=0;i<(size_t)igs->width*igs->height;++i) CHECK(pixels->primary[i]==769 && pixels->alternate[i]==768);
		st_pixel_buffer_destroy(pixels); destroy(&f);
	}
	struct fixture f; setup(&f); struct cterm_igs *igs=f.terminal->igs_engine;
	igs->flow_off=20; igs->flow_on=18; igs->flow_off_count=3; igs->flow_on_count=2; igs->loop_mode=1;
	load(&f,4,NULL,0); CHECK(reply_count==5 && memcmp(replies,(uint8_t[]){20,20,20,18,18},5)==0);
	reply_count=0; igs->flow_mode=0; load(&f,4,NULL,0); CHECK(reply_count==0);
	cterm_igs_reset(igs); f.terminal->response_cb=reset_reply; f.terminal->response_cbdata=igs;
	load(&f,4,(uint8_t[]){0,0,15,60,1,0},6);
	CHECK(!igs->music->command && !st_cterm_input_paused(f.terminal) && igs->music->count==0); flow_pair();
	f.terminal->response_cb=reply; f.terminal->response_cbdata=NULL; destroy(&f);
}
struct midi_log { st_mutex_t mutex; st_condition_t changed; uint8_t bytes[8192]; size_t count; bool block,entered; };
static int midi_send(void *context,const uint8_t *bytes,size_t count)
{
	struct midi_log *log=context; st_mutex_lock(&log->mutex);
	log->entered=true; st_condition_broadcast(&log->changed);
	while(log->block) st_condition_wait(&log->changed,&log->mutex);
	CHECK(count<=sizeof(log->bytes)-log->count); memcpy(log->bytes+log->count,bytes,count); log->count+=count;
	st_mutex_unlock(&log->mutex); return 0;
}
static int midi_gain(void *context,float left,float right) { (void)context; (void)left; (void)right; return 0; }
static void midi(void)
{
	struct fixture f; setup(&f); struct cterm_igs *igs=f.terminal->igs_engine; struct cterm_igs_music *m=igs->music;
	st_audio_runtime_t *runtime; CHECK(st_audio_runtime_create(&runtime,0)==0); f.terminal->screen.audio=runtime;
	struct midi_log log={0}; CHECK(st_mutex_init(&log.mutex,ST_MUTEX_PLAIN)==ST_THREAD_SUCCESS);
	CHECK(st_condition_init(&log.changed)==ST_THREAD_SUCCESS);
	struct st_midi_sink sink={.state=&log,.send=midi_send,.gain=midi_gain};
	CHECK(st_music_create_with_sink(&f.terminal->music_player,runtime,&f.terminal->music_stream,&sink)==0);
	const uint8_t pairs[]={0x90,0,60,1,100,2,61,0,101,0,0x7f};
	load(&f,1,pairs,sizeof(pairs)); CHECK(m->playing && m->delaying && m->index==3);
	CHECK(igs->wait_deadline==local_ms+5); m->midi_enabled=0; poll_due(&f);
	CHECK(m->playing && m->index==5 && igs->wait_deadline==local_ms+10);
	poll_due(&f); CHECK(!m->playing && st_music_wait(f.terminal->music_player)==0); flow_pair();
	CHECK(m->midi_enabled==0); m->midi_enabled=1;
	const uint8_t expected[]={0x90,60,100,0x90,61,101};
	st_mutex_lock(&log.mutex); CHECK(log.count==sizeof(expected) && memcmp(log.bytes,expected,sizeof(expected))==0); st_mutex_unlock(&log.mutex);
	/* A key during MIDI delay waits for the NEXT outer poll, never shortens
	 * the current delay. Abort emits no MIDI all-notes-off. */
	feed(&f,"G#N 2:"); CHECK(st_cterm_handle_key(f.terminal,3,0));
	CHECK(st_cterm_poll(f.terminal)==0 && m->playing && m->delaying);
	poll_due(&f); CHECK(!m->playing && m->count==(int)sizeof(pairs)); flow_pair();
	CHECK(st_music_wait(f.terminal->music_player)==0);
	st_mutex_lock(&log.mutex); CHECK(log.count==sizeof(expected)); st_mutex_unlock(&log.mutex);
	/* + changes byte alignment rather than skipping a MIDI message. */
	load(&f,0,(uint8_t[]){0xf8,1,0xfa,0,0xfc,0},6);
	feed(&f,"G#N 2:"); CHECK(st_cterm_handle_text_key(f.terminal,'+')); poll_due(&f);
	CHECK(m->index==4 && m->delaying && igs->wait_deadline==local_ms+0xfc*5);
	poll_due(&f); CHECK(!m->playing); flow_pair(); CHECK(st_music_wait(f.terminal->music_player)==0);
	st_mutex_lock(&log.mutex); const uint8_t tail[]={0xf8,0x90,60,0}; CHECK(log.count==sizeof(expected)+sizeof(tail) && memcmp(log.bytes+sizeof(expected),tail,sizeof(tail))==0); st_mutex_unlock(&log.mutex);
	/* EOF cancels active playback and stops this session's MIDI output. */
	feed(&f,"G#N 2:"); CHECK(st_cterm_finish(f.terminal)==0 && !m->playing && !st_music_busy(f.terminal->music_player)); flow_pair();
	destroy(&f); st_audio_runtime_destroy(runtime); st_condition_destroy(&log.changed); st_mutex_destroy(&log.mutex);
}
static void backpressure(void)
{
	struct fixture f; setup(&f); struct cterm_igs *igs=f.terminal->igs_engine;
	struct cterm_igs_music *m=igs->music;
	st_audio_runtime_t *runtime; CHECK(st_audio_runtime_create(&runtime,0)==0); f.terminal->screen.audio=runtime;
	struct midi_log log={.block=true}; CHECK(st_mutex_init(&log.mutex,ST_MUTEX_PLAIN)==ST_THREAD_SUCCESS);
	CHECK(st_condition_init(&log.changed)==ST_THREAD_SUCCESS);
	struct st_midi_sink sink={.state=&log,.send=midi_send,.gain=midi_gain};
	CHECK(st_music_create_with_sink(&f.terminal->music_player,runtime,&f.terminal->music_stream,&sink)==0);
	const uint8_t realtime=0xf8;
	CHECK(st_music_queue_midi(f.terminal->music_player,&realtime,1)==0);
	st_mutex_lock(&log.mutex);
	while(!log.entered) st_condition_wait(&log.changed,&log.mutex);
	st_mutex_unlock(&log.mutex);
	for(unsigned i=1;i<128;++i) CHECK(st_music_queue_midi(f.terminal->music_player,&realtime,1)==0);
	load(&f,0,(uint8_t[]){0xfa,1,0xfc,0},4);
	feed(&f,"G#N 2:"); CHECK(m->playing && !m->delaying && m->index==1 && m->checked);
	/* A pending key cannot bypass readiness after this pair's key poll. */
	CHECK(st_cterm_handle_key(f.terminal,3,0));
	CHECK(st_cterm_poll(f.terminal)==0 && m->playing && m->index==1 && m->key_count==1);
	st_mutex_lock(&log.mutex); log.block=false; st_condition_broadcast(&log.changed); st_mutex_unlock(&log.mutex);
	CHECK(st_music_wait(f.terminal->music_player)==0);
	CHECK(st_cterm_poll(f.terminal)==0 && m->playing && m->delaying && m->key_count==1);
	poll_due(&f); CHECK(!m->playing); flow_pair(); CHECK(st_music_wait(f.terminal->music_player)==0);
	st_mutex_lock(&log.mutex);
	CHECK(log.count==129 && log.bytes[128]==0xfa);
	for(unsigned i=0;i<128;++i) CHECK(log.bytes[i]==0xf8);
	st_mutex_unlock(&log.mutex);
	destroy(&f); st_audio_runtime_destroy(runtime); st_condition_destroy(&log.changed); st_mutex_destroy(&log.mutex);
}
int main(void) { loads(); chip(); keyboard(); boundaries(); geometry_and_flow(); midi(); backpressure(); return 0; }

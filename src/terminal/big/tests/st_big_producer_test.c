/* Literal DDO0615..0699 collector/point returns, references13/58/59. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_producer.h"
#include "io_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
struct fixture {
	struct st_big_state state;
	struct st_big_producer producer;
	struct st_big_producer_point points[16];
	struct st_big_producer_registers registers;
	uint8_t pixels[64*64], byte;
	size_t reads, writes;
	int read_status, write_status;
	uint16_t read_x, read_y, write_x, write_y;
	uint8_t plane, pen;
	bool mutate;
};
static void init(struct fixture *f)
{
	memset(f,0,sizeof(*f)); st_big_state_init(&f->state);
	f->state.bgi.width=f->state.bgi.height=64;
	f->state.bgi.viewport[2]=f->state.bgi.viewport[3]=63;
	f->state.bgi.x=1; f->state.bgi.y=2;
	f->state.bgi.ddo_x=3; f->state.bgi.ddo_y=4;
	f->state.bgi.device_x=5; f->state.bgi.device_y=6;
	f->registers=(struct st_big_producer_registers){10,11,0x1234,0xabcd};
	f->producer.video_segment=0xa000;
	f->byte=0x5a;
	CHECK(st_big_producer_bind(&f->producer,f->points,16,96)==0);
}
static int read_byte(void *context, uint16_t x, uint16_t y, uint8_t plane, uint8_t *byte)
{
	struct fixture *f=context; ++f->reads;
	f->read_x=x; f->read_y=y; f->plane=plane;
	if (f->read_status!=0) return f->read_status;
	*byte=f->byte;
	return 0;
}
static int pixel(void *context, uint16_t x, uint16_t y, uint8_t pen, bool xor_mode)
{
	struct fixture *f=context;
	CHECK(x<64 && y<64 && pen<16 && !xor_mode);
	if (f->producer.batch) CHECK(f->reads==1);
	f->write_x=x; f->write_y=y; f->pen=pen;
	if (f->write_status!=0) return f->write_status;
	++f->writes; f->pixels[y*64+x]=pen;
	if (f->mutate) {
		f->producer.video_segment=0xb800; f->producer.read_plane=3;
		f->state.bgi.color=1;
	}
	return 0;
}
static struct st_big_producer_sink sink(struct fixture *f)
{
	return (struct st_big_producer_sink){.drawing={f,pixel},.read_context=f,.read_byte=read_byte};
}
static void modes_and_binding(void)
{
	struct fixture f; init(&f); struct st_big_producer_sink io=sink(&f); bool suppressed=true;
	struct st_big_producer_registers before=f.registers; struct st_big_state state=f.state;
	CHECK(st_big_producer_emit(&f.producer,&f.state,&f.registers,NULL,&suppressed)==0 && !suppressed);
	CHECK(memcmp(&before,&f.registers,sizeof(before))==0 && memcmp(&state,&f.state,sizeof(state))==0);
	CHECK(f.reads==0 && f.writes==0 && f.producer.count==0);
	for (unsigned mode=1;mode<256;++mode) if (mode!=2) {
		init(&f); f.producer.mode=(uint8_t)mode;
		CHECK(st_big_producer_emit(&f.producer,&f.state,&f.registers,&io,&suppressed)==0 && suppressed);
		CHECK(f.producer.count==1 && f.producer.run_length==1 && f.points[0].x==10 && f.points[0].y==11);
		CHECK(f.reads==0 && f.writes==0 && memcmp(&before,&f.registers,sizeof(before))==0);
	}
	init(&f); f.producer.mode=2; f.producer.count=5; f.producer.run_length=9;
	f.producer.first_x=12; f.producer.first_y=13;
	CHECK(st_big_producer_bind(&f.producer,f.points,16,4096)==0 && f.producer.capacity==682);
	CHECK(f.producer.mode==2 && f.producer.count==5 && f.producer.run_length==9 && f.producer.first_x==12);
	CHECK(st_big_producer_start(&f.producer)==0 && f.producer.mode==1 && f.producer.count==0 && f.producer.run_length==0);
	CHECK(f.producer.first_x==12 && f.producer.first_y==13 && f.producer.capacity==682);
	for (unsigned bytes=0;bytes<=65535;++bytes) {
		CHECK(st_big_producer_bind(&f.producer,f.points,16,(uint16_t)bytes)==0);
		CHECK(f.producer.capacity==(bytes-bytes%6)/6 && f.producer.storage_count==16 && f.producer.points==f.points);
	}
}
static void emit(struct fixture *f, uint16_t x, uint16_t y)
{
	f->registers=(struct st_big_producer_registers){x,y,0x1234,0xabcd};
	struct st_big_producer_sink io=sink(f); bool suppressed=false;
	CHECK(st_big_producer_emit(&f->producer,&f->state,&f->registers,&io,&suppressed)==0 && suppressed);
	CHECK(f->registers.cx==0x1234 && f->registers.dx==0xabcd && f->reads==0 && f->writes==0);
}
static void runs(void)
{
	struct fixture f; init(&f); CHECK(st_big_producer_start(&f.producer)==0);
	emit(&f,10,11); CHECK(f.producer.count==1 && f.producer.run_length==1 && f.producer.first_x==10 && f.producer.first_y==11);
	emit(&f,10,11); CHECK(f.producer.count==1 && f.producer.run_length==1);
	emit(&f,12,13); emit(&f,12,13); emit(&f,12,13);
	CHECK(f.producer.count==4 && f.producer.run_length==4 && f.producer.first_x==10 && f.producer.first_y==11);
	emit(&f,10,11);
	CHECK(f.producer.count==6 && f.producer.run_length==0);
	CHECK(f.registers.ax==0x8001 && f.registers.bx==0x8001);
	static const struct st_big_producer_point expected[]={{10,11},{12,13},{12,13},{12,13},{10,11},{0x8001,0x8001}};
	CHECK(memcmp(f.points,expected,sizeof(expected))==0);
	emit(&f,20,21);
	CHECK(f.producer.count==7 && f.producer.run_length==1 && f.producer.first_x==20 && f.producer.first_y==21);
	/* The test is against the first point of the run, not the last point. */
	emit(&f,20,22); emit(&f,20,22); CHECK(f.producer.count==9);
	emit(&f,20,21); CHECK(f.producer.count==11 && f.producer.run_length==0);
	/* Special words are stored literally; the producer does not parse markers. */
	emit(&f,0x8000,0x8001); CHECK(f.points[11].x==0x8000 && f.points[11].y==0x8001);
}
static void overflow(void)
{
	struct fixture f; init(&f); CHECK(st_big_producer_start(&f.producer)==0);
	f.producer.capacity=2; f.state.bgi.error=-11;
	emit(&f,10,11); CHECK(f.state.bgi.error==-11);
	emit(&f,12,13); emit(&f,12,13);
	CHECK(f.producer.count==2 && f.producer.run_length==3 && f.state.bgi.error==-6);
	emit(&f,10,11);
	CHECK(f.producer.count==2 && f.producer.run_length==0 && f.registers.ax==0x8001 && f.state.bgi.error==-6);
	CHECK(f.points[0].x==10 && f.points[1].x==12 && f.points[2].x==0);
	/* First overflow still publishes first-point scratch and increments run. */
	init(&f); f.producer.capacity=0; f.producer.mode=1;
	emit(&f,65535,32768);
	CHECK(f.producer.count==0 && f.producer.run_length==1 && f.producer.first_x==65535 && f.producer.first_y==32768);
	CHECK(f.state.bgi.error==-6);
	emit(&f,65535,32768); CHECK(f.producer.run_length==1);
	/* Run length wraps before the capacity check, unlike native size_t. */
	f.producer.run_length=65535;
	emit(&f,3,4); CHECK(f.producer.run_length==0 && f.producer.count==0);
	emit(&f,5,6); CHECK(f.producer.first_x==5 && f.producer.first_y==6 && f.producer.run_length==1);
	/* Overflow on only the closing marker preserves the preceding point. */
	init(&f); CHECK(st_big_producer_start(&f.producer)==0); f.producer.capacity=3;
	emit(&f,1,2); emit(&f,3,4); emit(&f,1,2);
	CHECK(f.producer.count==3 && f.producer.run_length==0 && f.points[2].x==1 && f.points[3].x==0 && f.state.bgi.error==-6);
}
static void point_returns(void)
{
	struct fixture f; struct st_big_producer_sink io; bool suppressed;
	for (unsigned batch=0;batch<2;++batch) for (unsigned phase=0;phase<8;++phase) {
		init(&f); io=sink(&f); f.producer.mode=2; f.producer.batch=batch!=0;
		f.producer.video_segment=0xa960; f.producer.read_plane=2;
		f.state.bgi.color=0x23; f.state.bgi.device_color=9;
		f.state.bgi.write_mode=1; f.state.bgi.line_mask=0; f.state.bgi.ddo_line_width=3;
		f.registers.ax=(uint16_t)(8+phase); f.registers.bx=11;
		struct st_big_state before=f.state;
		CHECK(st_big_producer_emit(&f.producer,&f.state,&f.registers,&io,&suppressed)==0 && !suppressed);
		CHECK(f.writes==1 && f.pixels[11*64+8+phase]==3 && f.pen==3);
		CHECK(f.registers.ax==(batch?0xa95a:0x0f02) && f.registers.bx==phase);
		CHECK(f.registers.cx==((uint16_t)(0x8000u>>phase)|0x23) && f.registers.dx==0xab23);
		CHECK(memcmp(&before,&f.state,sizeof(before))==0 && f.producer.count==0 && f.producer.run_length==0);
		CHECK(f.reads==batch);
		if (batch) CHECK(f.read_x==8 && f.read_y==11 && f.plane==2);
	}
	for (unsigned value=0;value<256;++value) {
		init(&f); io=sink(&f); f.producer.mode=2; f.producer.batch=true;
		f.producer.video_segment=0xb84f; f.byte=(uint8_t)value;
		f.state.bgi.color=(uint16_t)(0x1200u|value); f.state.bgi.error=-11;
		CHECK(st_big_producer_emit(&f.producer,&f.state,&f.registers,&io,&suppressed)==0 && !suppressed);
		CHECK(f.registers.ax==0xb800u+value && f.registers.bx==2);
		CHECK(f.registers.cx==(0x2000u|value) && f.registers.dx==(0xab00u|value));
		CHECK(f.pen==(value&15) && f.state.bgi.error==-11 && f.reads==1 && f.writes==1);
	}
	/* Lower physical points do not translate the viewport a second time. */
	init(&f); io=sink(&f); f.producer.mode=2;
	f.state.bgi.viewport[0]=8; f.state.bgi.viewport[1]=9;
	f.registers=(struct st_big_producer_registers){10,11,0,0};
	CHECK(st_big_producer_emit(&f.producer,&f.state,&f.registers,&io,&suppressed)==0 && f.write_x==10 && f.write_y==11);
	/* Rejection is carry-clear and changes only DL, even when no sink exists. */
	static const uint16_t rejected[][2]={{65535,0},{0,65535},{64,0},{0,64}};
	for (size_t i=0;i<sizeof(rejected)/sizeof(rejected[0]);++i) {
		init(&f); f.producer.mode=2; f.producer.batch=true;
		f.registers.ax=rejected[i][0]; f.registers.bx=rejected[i][1]; f.state.bgi.color=0x123;
		CHECK(st_big_producer_emit(&f.producer,&f.state,&f.registers,NULL,&suppressed)==0 && !suppressed);
		CHECK(f.registers.ax==rejected[i][0] && f.registers.bx==rejected[i][1] && f.registers.cx==0x1234 && f.registers.dx==0xab23);
		CHECK(f.reads==0 && f.writes==0);
	}
	/* Inputs read by addressing are captured before the host write callback. */
	init(&f); io=sink(&f); f.producer.mode=2; f.producer.batch=true; f.mutate=true;
	f.producer.video_segment=0xa960; f.state.bgi.color=0x23;
	CHECK(st_big_producer_emit(&f.producer,&f.state,&f.registers,&io,&suppressed)==0);
	CHECK(f.registers.ax==0xa95a && f.registers.cx==0x2023 && f.registers.dx==0xab23 && f.plane==0);
}
static void failures(void)
{
	struct fixture f; init(&f); struct st_big_producer_sink io=sink(&f); bool suppressed;
	f.producer.mode=1; f.producer.storage_count=1;
	emit(&f,1,2); f.registers.ax=3;
	CHECK(st_big_producer_emit(&f.producer,&f.state,&f.registers,&io,&suppressed)==IO_ERROR_OUT_OF_RANGE);
	CHECK(f.producer.count==1 && f.producer.run_length==2 && f.points[0].x==1 && f.state.bgi.error==0);
	for (size_t storage=2;storage<=3;++storage) {
		init(&f); f.producer.mode=1; f.producer.storage_count=storage;
		emit(&f,1,2); emit(&f,3,4); f.registers.ax=1; f.registers.bx=2;
		CHECK(st_big_producer_emit(&f.producer,&f.state,&f.registers,&io,&suppressed)==IO_ERROR_OUT_OF_RANGE && suppressed);
		CHECK(f.producer.count==storage && f.producer.run_length==storage+1 && f.state.bgi.error==0);
		CHECK(f.registers.ax==(storage==2?1:0x8001) && f.registers.bx==(storage==2?2:0x8001));
		if (storage==3) CHECK(f.points[2].x==1 && f.points[2].y==2);
	}
	init(&f); f.producer.mode=1; f.producer.points=NULL;
	CHECK(st_big_producer_emit(&f.producer,&f.state,&f.registers,&io,&suppressed)==IO_ERROR_OUT_OF_RANGE);
	CHECK(f.producer.first_x==10 && f.producer.run_length==1 && f.producer.count==0);
	init(&f); f.producer.mode=2; f.write_status=IO_ERROR_IO;
	CHECK(st_big_producer_emit(&f.producer,&f.state,&f.registers,&io,&suppressed)==IO_ERROR_IO);
	CHECK(f.writes==0 && f.state.bgi.error==0 && f.registers.dx==0xab0f);
	CHECK(f.registers.ax==0xa000 && f.registers.bx==2 && f.registers.cx==0x200f);
	f.registers=(struct st_big_producer_registers){10,11,0x1234,0xabcd};
	f.write_status=IO_ERROR_IN_PROGRESS;
	CHECK(st_big_producer_emit(&f.producer,&f.state,&f.registers,&io,&suppressed)==IO_ERROR_PROTOCOL);
	init(&f); f.producer.mode=2; f.producer.batch=true; f.read_status=IO_ERROR_IO;
	CHECK(st_big_producer_emit(&f.producer,&f.state,&f.registers,&io,&suppressed)==IO_ERROR_IO && f.reads==1 && f.writes==0);
	f.registers=(struct st_big_producer_registers){10,11,0x1234,0xabcd};
	f.read_status=IO_ERROR_IN_PROGRESS;
	CHECK(st_big_producer_emit(&f.producer,&f.state,&f.registers,&io,&suppressed)==IO_ERROR_PROTOCOL);
	init(&f); f.producer.mode=2; f.producer.batch=true; io.read_byte=NULL;
	CHECK(st_big_producer_emit(&f.producer,&f.state,&f.registers,&io,&suppressed)==IO_ERROR_INVALID_ARGUMENT && f.writes==0);
	f.registers=(struct st_big_producer_registers){10,11,0x1234,0xabcd};
	io=sink(&f); f.producer.read_plane=4;
	CHECK(st_big_producer_emit(&f.producer,&f.state,&f.registers,&io,&suppressed)==IO_ERROR_OUT_OF_RANGE && f.reads==0);
	init(&f); io=sink(&f); f.producer.mode=2; f.state.bgi.state=0;
	CHECK(st_big_producer_emit(&f.producer,&f.state,&f.registers,&io,&suppressed)==IO_ERROR_PROTOCOL && f.state.bgi.error==-1);
	CHECK(st_big_producer_emit(NULL,&f.state,&f.registers,&io,&suppressed)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_producer_emit(&f.producer,NULL,&f.registers,&io,&suppressed)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_producer_emit(&f.producer,&f.state,NULL,&io,&suppressed)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_producer_emit(&f.producer,&f.state,&f.registers,&io,NULL)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_producer_bind(NULL,f.points,16,96)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_producer_start(NULL)==IO_ERROR_INVALID_ARGUMENT);
}
int main(void) { modes_and_binding(); runs(); overflow(); point_returns(); failures(); return 0; }

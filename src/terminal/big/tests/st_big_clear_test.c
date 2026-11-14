/* Independent BIG23/46/59 page-clear and VGA console-blank oracles. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_clear.h"
#include "st_big_draw.h"
#include "io_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
struct event { uint16_t address; uint8_t planes,pen; };
struct fixture { struct st_big_state state; uint8_t pens[65536][8]; struct event trace[1024]; size_t calls,fail_at,change_at; int error; };
static void init(struct fixture *f)
{
	memset(f,0,sizeof(*f)); memset(f->pens,11,sizeof(f->pens)); st_big_state_init(&f->state);
	f->state.bgi.x=31; f->state.bgi.y=32; f->state.bgi.ddo_x=33; f->state.bgi.ddo_y=34; f->state.bgi.device_x=35; f->state.bgi.device_y=36;
	f->state.bgi.viewport[0]=10; f->state.bgi.viewport[1]=12; f->state.bgi.viewport[2]=11; f->state.bgi.viewport[3]=13;
	f->state.bgi.error=-11; f->state.bgi.write_mode=1; f->state.bgi.background=4; f->fail_at=SIZE_MAX; f->change_at=SIZE_MAX; f->error=IO_ERROR_IO;
}
static int write_byte(void *context, uint16_t address, uint8_t mask, uint8_t planes, const uint8_t pens[8], uint8_t mode)
{
	struct fixture *f=context; CHECK(mask==255 && planes<=15 && mode==0);
	if (f->calls==f->fail_at) return f->error;
	if (f->calls<1024) f->trace[f->calls]=(struct event){address,planes,pens[0]};
	if (++f->calls==f->change_at) { CHECK(f->state.bgi.x==31 && f->state.bgi.ddo_x==33); f->state.bgi.viewport[0]=7; f->state.bgi.viewport[1]=9; }
	for (unsigned i=0;i<8;++i) { CHECK(pens[i]==pens[0]); f->pens[address][i]=(uint8_t)((f->pens[address][i]&(15u^planes))|(pens[i]&planes)); }
	return 0;
}
static struct st_big_memory_sink sink(struct fixture *f) { return (struct st_big_memory_sink){.context=f,.write=write_byte}; }
static void device(void)
{
	struct fixture *f=malloc(sizeof(*f)); CHECK(f!=NULL);
	static const size_t counts[]={16384,32768,38400};
	for (unsigned mode=1;mode<=3;++mode) {
		init(f); f->state.bgi.mode=(uint16_t)(mode-1); struct st_big_state before=f->state; struct st_big_memory_sink out=sink(f);
		CHECK(st_big_clear_device(&f->state,&out)==0 && f->calls==counts[mode-1]);
		for (size_t a=0;a<65536;++a) for (unsigned bit=0;bit<8;++bit) CHECK(f->pens[a][bit]==(a<counts[mode-1]?0:11));
		before.bgi.x=before.bgi.y=0; before.bgi.ddo_x=before.bgi.device_x=10; before.bgi.ddo_y=before.bgi.device_y=12;
		CHECK(memcmp(&before,&f->state,sizeof(before))==0);
	}
	init(f); struct st_big_memory_sink out=sink(f); f->change_at=32768;
	CHECK(st_big_clear_device(&f->state,&out)==0 && f->state.bgi.ddo_x==7 && f->state.bgi.device_y==9);
	for (unsigned busy=0;busy<2;++busy) {
		init(f); f->fail_at=10; f->error=busy?IO_ERROR_IN_PROGRESS:IO_ERROR_IO;
		CHECK(st_big_clear_device(&f->state,&out)==(busy?IO_ERROR_PROTOCOL:IO_ERROR_IO) && f->calls==10 && f->state.bgi.x==31 && f->state.bgi.ddo_x==33);
		CHECK(f->pens[9][0]==0 && f->pens[10][0]==11);
	}
	init(f); f->state.bgi.state=0; CHECK(st_big_clear_device(&f->state,&out)==IO_ERROR_PROTOCOL && f->calls==0 && f->state.bgi.error==-1);
	free(f);
}
static void console(void)
{
	struct fixture *f=malloc(sizeof(*f)); CHECK(f!=NULL); const uint8_t bounds[]={0,0,79,24};
	static const size_t bytes[]={16000,28000,32000};
	for (unsigned mode=1;mode<=3;++mode) {
		init(f); struct st_big_memory_sink out=sink(f); struct st_big_state before=f->state;
		CHECK(st_big_clear_console(bounds,7,st_big_mode(mode),0,&out)==0 && f->calls==bytes[mode-1]*2);
		for (size_t a=0;a<65536;++a) for (unsigned bit=0;bit<8;++bit) CHECK(f->pens[a][bit]==(a<bytes[mode-1]?7:11));
		CHECK(memcmp(&before,&f->state,sizeof(before))==0);
		CHECK(f->trace[0].address==0 && f->trace[0].planes==15 && f->trace[0].pen==0);
		CHECK(f->trace[79].address==79 && f->trace[80].address==0 && f->trace[80].planes==7 && f->trace[80].pen==15 && f->trace[160].address==80);
	}
	const uint8_t small[]={1,1,2,1};
	for (unsigned pen=0;pen<16;++pen) {
		init(f); struct st_big_memory_sink out=sink(f); CHECK(st_big_clear_console(small,(uint8_t)(0x70u|pen),st_big_mode(1),1,&out)==0 && f->calls==32);
		for (size_t a=0;a<65536;++a) CHECK(f->pens[a][0]==(a>=0x4000+641 && a<=0x4000+1202 && (a-0x4000-641)%80<2?pen:11));
	}
	init(f); struct st_big_memory_sink out=sink(f); f->fail_at=83;
	CHECK(st_big_clear_console(bounds,7,st_big_mode(2),0,&out)==IO_ERROR_IO && f->calls==83);
	for (unsigned a=0;a<161;++a) CHECK(f->pens[a][0]==(a<3?7:a<80?0:11));
	const uint8_t width0[]={0,0,255,0},both0[]={1,1,0,0},borrow[]={5,1,3,3};
	init(f); CHECK(st_big_clear_console(width0,7,st_big_mode(1),0,&out)==0 && f->calls==0);
	CHECK(st_big_clear_console(both0,7,st_big_mode(1),0,&out)==0 && f->calls==0);
	CHECK(st_big_clear_console(borrow,7,st_big_mode(1),0,&out)==0 && f->calls==8160);
	CHECK(f->trace[0].address==645 && f->trace[254].address==899 && f->trace[255].address==645 && f->trace[510].address==725);
	init(f); struct st_big_mode zero=*st_big_mode(1); zero.cell_height=zero.columns=0; const uint8_t one[]={0,0,0,0};
	CHECK(st_big_clear_console(one,0x80,&zero,0,&out)==0 && f->calls==131072 && f->pens[0][0]==0);
	CHECK(st_big_clear_console(NULL,7,&zero,0,&out)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_clear_device(NULL,&out)==IO_ERROR_INVALID_ARGUMENT);
	free(f);
}
int main(void) { device(); console(); return 0; }

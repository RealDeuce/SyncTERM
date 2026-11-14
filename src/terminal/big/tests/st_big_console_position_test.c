/* Independent BIG31/51 cached-window and byte-coordinate expectations. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_console.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
static void literals(void)
{
	static const struct { uint8_t b[4]; uint16_t x,y; bool valid; uint8_t p[2]; } cases[]={
		{{0,0,79,24},1,1,true,{0,0}},{{0,0,79,24},80,25,true,{79,24}},
		{{0,0,79,24},0,1,false,{0,0}},{{0,0,79,24},1,0,false,{0,0}},
		{{0,0,79,24},81,1,false,{0,0}},{{0,0,79,24},1,26,false,{0,0}},
		{{0,0,79,24},257,257,true,{0,0}},{{0,0,79,24},336,281,true,{79,24}},
		{{0,0,79,24},65535,1,false,{0,0}},{{0,0,79,24},1,32768,false,{0,0}},
		{{5,7,14,16},1,1,true,{5,7}},{{5,7,14,16},10,10,true,{14,16}},
		{{5,7,14,16},0,1,false,{0,0}},{{5,7,14,16},11,1,false,{0,0}},
		{{250,250,255,255},6,6,true,{255,255}},{{250,250,255,255},7,1,false,{0,0}},
		{{0,0,255,255},0,0,true,{255,255}},{{0,0,255,255},65535,32768,true,{255,255}},
		{{0,0,255,255},32767,32767,true,{254,254}},{{255,255,255,255},1,1,true,{255,255}},
		{{5,7,4,6},1,1,false,{0,0}},{{0,0,79,24},256,256,false,{0,0}},
	};
	for (size_t i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
		uint8_t p[2]={165,90},before[4]; memcpy(before,cases[i].b,sizeof(before));
		CHECK(st_big_console_position(cases[i].b,cases[i].x,cases[i].y,p)==cases[i].valid);
		CHECK(memcmp(before,cases[i].b,sizeof(before))==0);
		CHECK(p[0]==(cases[i].valid?cases[i].p[0]:165) && p[1]==(cases[i].valid?cases[i].p[1]:90));
	}
}
static void words(void)
{
	const uint8_t normal[]={0,0,79,24},full[]={0,0,255,255};
	for (unsigned word=0;word<65536;++word) {
		uint8_t p[]={165,90}; unsigned byte=word%256;
		bool x=word<32768 && byte>=1 && byte<=80,y=word<32768 && byte>=1 && byte<=25;
		CHECK(st_big_console_position(normal,(uint16_t)word,1,p)==x);
		CHECK(p[0]==(x?byte-1:165) && p[1]==(x?0:90));
		p[0]=165; p[1]=90; CHECK(st_big_console_position(normal,1,(uint16_t)word,p)==y);
		CHECK(p[0]==(y?0:165) && p[1]==(y?byte-1:90));
		CHECK(st_big_console_position(full,(uint16_t)word,(uint16_t)word,p));
		unsigned expected=word>=32768 || byte==0?255:byte-1;
		CHECK(p[0]==expected && p[1]==expected);
	}
	uint8_t p[]={165,90}; CHECK(!st_big_console_position(NULL,1,1,p) && p[0]==165 && p[1]==90);
	CHECK(!st_big_console_position(normal,1,1,NULL));
}
int main(void) { literals(); words(); return 0; }

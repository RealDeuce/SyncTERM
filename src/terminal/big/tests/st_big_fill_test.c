/* Literal fill/WORD/cache expectations from BIG10/11/50/54/83. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_fill.h"
#include "st_big_draw.h"
#include "st_bgi_data.h"
#include "io_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
/* Original same-index stock masks, independently transcribed from BIG11. */
static const uint8_t stock[12][8]={
	{0,0,0,0,0,0,0,0},{255,255,255,255,255,255,255,255},
	{255,255,0,0,255,255,0,0},{1,2,4,8,16,32,64,128},
	{0xe0,0xc1,0x83,0x07,0x0e,0x1c,0x38,0x70},
	{0xf0,0x78,0x3c,0x1e,0x0f,0x87,0xc3,0xe1},
	{0xa5,0xd2,0x69,0xb4,0x5a,0x2d,0x96,0x4b},
	{0xff,0x88,0x88,0x88,0xff,0x88,0x88,0x88},
	{0x81,0x42,0x24,0x18,0x18,0x24,0x42,0x81},
	{0xcc,0x33,0xcc,0x33,0xcc,0x33,0xcc,0x33},
	{0x80,0,8,0,0x80,0,8,0},{0x88,0,0x22,0,0x88,0,0x22,0},
};
static void rows(const struct st_big_state *s, const uint32_t expected[8])
{
	for (size_t row=0;row<8;++row) for (unsigned x=0;x<8;++x)
		CHECK(s->bgi.fill_pixels[row*8+x]==((expected[row]>>(4*x))&15));
}
static void data_and_owners(void)
{
	CHECK(memcmp(st_bgi_fill_patterns,stock,sizeof(stock))==0);
	struct st_big_state s; st_big_state_init(&s);
	static const uint32_t full[]={0xffffffff,0xffffffff,0xffffffff,0xffffffff,0xffffffff,0xffffffff,0xffffffff,0xffffffff};
	rows(&s,full); CHECK(s.bgi.device_fill_style==1 && s.bgi.fill_low==15 && s.bgi.device_fill_color==15);
	for (size_t i=0;i<8;++i) CHECK(s.bgi.fill_user[i]==255 && s.bgi.device_fill_user[i]==255 && s.bgi.fill_pattern[i]==255);
	s.application.colors[2]=5; CHECK(st_big_fill_select(&s,3)==0);
	static const uint32_t slash[]={0x50000000,0x05000000,0x00500000,0x00050000,0x00005000,0x00000500,0x00000050,0x00000005};
	rows(&s,slash); CHECK(memcmp(s.bgi.fill_pattern,stock[3],8)==0);
	CHECK(st_big_fill_select(&s,12)==0);
	static const uint32_t custom[]={0x05050505,0x50505050,0x05050505,0x50505050,0x05050505,0x50505050,0x05050505,0x50505050};
	rows(&s,custom); CHECK(s.bgi.fill_style==12 && s.bgi.fill_color==5 && s.bgi.device_fill_style==12);
	static const uint8_t checker[]={0xaa,0x55,0xaa,0x55,0xaa,0x55,0xaa,0x55};
	CHECK(memcmp(s.bgi.fill_user,checker,8)==0 && memcmp(s.bgi.device_fill_user,checker,8)==0);
	s.application.patterns[0][0]=0; s.application.colors[2]=9;
	CHECK(s.bgi.fill_user[0]==0xaa && s.bgi.device_fill_user[0]==0xaa && s.bgi.fill_pattern[0]==0xaa); rows(&s,custom);
	/* Draw-colour dispatch uses the current fill BYTE but never rebuilds
	 * the original colour-latched pattern cache. */
	s.bgi.fill_low=2; CHECK(st_big_draw_color(&s,7)==0 && s.bgi.device_fill_color==2); rows(&s,custom);
	CHECK(st_big_fill_builtin(&s,0,31)==0 && s.bgi.fill_style==0 && s.bgi.fill_color==31 && s.bgi.fill_low==31 && s.bgi.device_fill_color==15);
	static const uint32_t empty[8]={0}; rows(&s,empty);
	CHECK(memcmp(s.bgi.fill_user,checker,8)==0 && memcmp(s.bgi.device_fill_user,checker,8)==0);
	CHECK(st_big_fill_apply(&s)==0 && s.application.fill==12 && s.bgi.fill_color==9 && s.bgi.fill_user[0]==0 && s.bgi.device_fill_user[0]==0);
	static const uint32_t resubmitted[]={0,0x90909090,0x09090909,0x90909090,0x09090909,0x90909090,0x09090909,0x90909090};
	rows(&s,resubmitted);
	/* The second source copy must use the original pointer, and both
	 * copies must retain forward byte order if the source overlaps. */
	for (size_t i=0;i<8;++i) s.bgi.fill_user[i]=(uint8_t)(i+1);
	uint8_t *object=(uint8_t *)&s;
	size_t start=(size_t)((uint8_t *)s.bgi.fill_user-object)-1;
	object[start]=0xa5;
	CHECK(st_big_fill_custom(&s,object+start,5)==0);
	for (size_t i=0;i<8;++i) CHECK(s.bgi.fill_user[i]==0xa5 && s.bgi.device_fill_user[i]==0xa5);
	for (size_t i=0;i<8;++i) s.bgi.fill_user[i]=(uint8_t)(i+1);
	start=(size_t)((uint8_t *)s.bgi.fill_user-object)+1;
	object[start+7]=0xb6;
	CHECK(st_big_fill_custom(&s,object+start,5)==0);
	static const uint8_t mirror[]={2,3,4,5,6,7,8,0xb6}, driver[]={3,4,5,6,7,8,0xb6,0xb6};
	CHECK(memcmp(s.bgi.fill_user,mirror,8)==0 && memcmp(s.bgi.device_fill_user,driver,8)==0 && memcmp(s.bgi.fill_pattern,driver,8)==0);
}
static void words_and_selection(void)
{
	struct st_big_state s; st_big_state_init(&s); s.application.colors[2]=5;
	for (size_t slot=0;slot<12;++slot) for (size_t row=0;row<8;++row) s.application.patterns[slot][row]=(uint8_t)(slot*16+row);
	for (unsigned raw=0;raw<=65535;++raw) {
		CHECK(st_big_fill_select(&s,(uint16_t)raw)==0);
		unsigned selected=raw<24?raw:1;
		CHECK(s.application.fill==selected && s.bgi.fill_style==(selected<12?selected:12) && s.bgi.fill_color==5);
		const uint8_t *expected=selected<12?stock[selected]:s.application.patterns[selected-12];
		CHECK(memcmp(s.bgi.fill_pattern,expected,8)==0);
	}
	static const uint8_t first[]={128,0,0,0,0,0,0,0};
	for (unsigned word=0;word<=65535;++word) {
		CHECK(st_big_fill_builtin(&s,1,(uint16_t)word)==0);
		CHECK(s.bgi.fill_color==word && s.bgi.fill_low==(uint8_t)word && s.bgi.device_fill_color==(word&15));
		for (size_t i=0;i<64;++i) CHECK(s.bgi.fill_pixels[i]==(word&15));
		s.bgi.error=0;
		CHECK(st_big_fill_custom(&s,first,(uint16_t)word)==0);
		if (word<16) {
			CHECK(s.bgi.fill_style==12 && s.bgi.fill_color==word && s.bgi.error==0 && s.bgi.fill_pixels[0]==word);
			for (size_t i=1;i<64;++i) CHECK(s.bgi.fill_pixels[i]==0);
		} else CHECK(s.bgi.fill_style==1 && s.bgi.fill_color==word && s.bgi.error==-11);
	}
}
static void errors_and_defaults(void)
{
	struct st_big_state s; st_big_state_init(&s); s.bgi.error=-11;
	CHECK(st_big_fill_builtin(&s,3,5)==0 && s.bgi.error==-11);
	uint8_t before[64]; memcpy(before,s.bgi.fill_pixels,64);
	CHECK(st_big_fill_builtin(&s,12,7)==0 && s.bgi.fill_style==3 && s.bgi.fill_color==5 && s.bgi.error==-11 && memcmp(before,s.bgi.fill_pixels,64)==0);
	CHECK(st_big_fill_select(&s,12)==0 && s.bgi.fill_style==12);
	s.application.colors[2]=31; CHECK(st_big_fill_select(&s,13)==0);
	CHECK(s.application.fill==13 && s.bgi.fill_style==12 && s.bgi.fill_color==15 && s.bgi.error==-11);
	s.bgi.state=0; memcpy(before,s.bgi.fill_pixels,64);
	CHECK(st_big_fill_builtin(&s,4,0x123)==IO_ERROR_PROTOCOL && s.bgi.fill_style==4 && s.bgi.fill_color==0x123 && s.bgi.fill_low==0x23);
	CHECK(s.bgi.device_fill_style==12 && memcmp(before,s.bgi.fill_pixels,64)==0 && s.bgi.error==-1);
	CHECK(st_big_fill_select(&s,12)==IO_ERROR_PROTOCOL && s.application.fill==12 && s.bgi.fill_style==4 && s.bgi.fill_color==0x123);
	CHECK(st_big_fill_custom(&s,NULL,5)==IO_ERROR_INVALID_ARGUMENT && st_big_fill_builtin(NULL,1,5)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_fill_custom(NULL,stock[0],5)==IO_ERROR_INVALID_ARGUMENT && st_big_fill_apply(NULL)==IO_ERROR_INVALID_ARGUMENT && st_big_fill_select(NULL,1)==IO_ERROR_INVALID_ARGUMENT);
}
int main(void) { data_and_owners(); words_and_selection(); errors_and_defaults(); return 0; }

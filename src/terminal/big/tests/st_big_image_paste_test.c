/* Independent Boolean, geometry and byte/plane traces, BIG25/56/83. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_image.h"
#include "st_bgi_pixel.h"
#include "io_error.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
struct event { uint16_t address, height; uint8_t mask, planes, mode; bool write; };
struct fixture {
	struct st_big_state state;
	struct st_big_image image;
	uint8_t bytes[ST_BIG_IMAGE_CAPACITY], pens[ST_BIG_IMAGE_PENS];
	uint8_t primary[65536*8], alternate[65536*8];
	struct event events[2048];
	size_t count, fail;
	int failure;
};
static const uint8_t source_pens[]={9,3,4,14,6,12,11,1};
/* Truth-table bit number is destination*2+source. These literal tables
 * are an oracle separate from the operator implementation. */
static uint8_t expected(unsigned source, unsigned destination, unsigned mode)
{
	static const unsigned truth[]={0xa,0x6,0xe,0x8,0x5};
	uint8_t value=0;
	for (unsigned plane=0;plane<4;++plane) {
		unsigned index=((destination>>plane)&1u)*2+((source>>plane)&1u);
		if ((truth[mode]&(1u<<index))!=0) value|=(uint8_t)(1u<<plane);
	}
	return value;
}
static void put_word(uint8_t *p, uint16_t value) { p[0]=(uint8_t)value; p[1]=(uint8_t)(value>>8); }
static uint16_t height(const struct fixture *f) { return (uint16_t)((unsigned)f->bytes[2]+256u*f->bytes[3]); }
static struct fixture *create(void)
{
	struct fixture *f=calloc(1,sizeof(*f)); CHECK(f!=NULL);
	st_big_state_init(&f->state);
	f->state.bgi.x=1; f->state.bgi.y=2; f->state.bgi.ddo_x=3; f->state.bgi.ddo_y=4;
	f->state.bgi.device_x=5; f->state.bgi.device_y=6;
	f->state.application.write_mode=f->state.bgi.write_mode=1; f->state.bgi.error=-11;
	f->fail=SIZE_MAX; f->failure=IO_ERROR_IO;
	CHECK(st_big_image_bind(&f->image,f->bytes,sizeof(f->bytes),f->pens,sizeof(f->pens))==0);
	return f;
}
static int event(struct fixture *f, uint16_t address, uint8_t mask, uint8_t planes, uint8_t mode, bool write)
{
	if (f->count<sizeof(f->events)/sizeof(f->events[0]))
		f->events[f->count]=(struct event){address,height(f),mask,planes,mode,write};
	return f->count++==f->fail?f->failure:0;
}
static int read_byte(void *context, uint16_t address, uint8_t pens[8])
{
	struct fixture *f=context; int status=event(f,address,0,0,0,false);
	if (status==0) memcpy(pens,f->primary+(size_t)address*8,8);
	return status;
}
static int write_byte(void *context, uint16_t address, uint8_t mask, uint8_t planes, const uint8_t pens[8], uint8_t mode)
{
	struct fixture *f=context; int status=event(f,address,mask,planes,mode,true);
	if (status!=0) return status;
	CHECK(mode<=4);
	for (unsigned i=0;i<8;++i) if ((mask&(0x80u>>i))!=0) {
		CHECK(pens[i]<16); size_t at=(size_t)address*8+i;
		f->primary[at]=(uint8_t)((f->primary[at]&~planes)|(expected(pens[i],f->primary[at],mode)&planes));
		f->alternate[at]=(uint8_t)((f->alternate[at]&~planes)|(expected(pens[i],f->alternate[at],mode)&planes));
	}
	return 0;
}
static int paste(struct fixture *f, uint16_t x, uint16_t y, uint16_t mode)
{
	const struct st_big_memory_sink sink={f,read_byte,write_byte};
	return st_big_image_paste(&f->image,&f->state,&sink,x,y,mode);
}
static void image(struct fixture *f, unsigned width, unsigned rows)
{
	/* Literal plane bytes96/3c/5a/c3 encode source_pens, repeated every
	 * eight pixels; odd rows invert all four bits. Padding stays present. */
	static const uint8_t data[]={0x96,0x3c,0x5a,0xc3};
	put_word(f->bytes,(uint16_t)(width-1)); put_word(f->bytes+2,(uint16_t)(rows-1));
	size_t cursor=4;
	for (unsigned row=0;row<rows;++row) for (unsigned p=0;p<4;++p) for (unsigned byte=0;byte<(width+7)/8;++byte) {
		CHECK(cursor<sizeof(f->bytes)); f->bytes[cursor++]=(uint8_t)(data[p]^(row%2?255:0));
	}
	CHECK(st_big_image_import(&f->image)==0);
	f->count=0; f->fail=SIZE_MAX;
}
static void boolean_core(void)
{
	static const uint8_t literal[]={5,3,7,4,10};
	for (unsigned mode=0;mode<5;++mode) {
		CHECK(st_bgi_combine_pixel(5,6,(uint8_t)mode)==literal[mode]);
		for (unsigned s=0;s<16;++s) for (unsigned d=0;d<16;++d)
			CHECK(st_bgi_combine_pixel((uint8_t)s,(uint8_t)d,(uint8_t)mode)==expected(s,d,mode));
	}
}
static void normal_images(void)
{
	struct fixture *f=create();
	static const unsigned widths[]={1,7,8,9,15,16,17,31,639};
	for (unsigned mode=0;mode<5;++mode) for (unsigned shift=0;shift<8;++shift) for (size_t w=0;w<sizeof(widths)/sizeof(widths[0]);++w) {
		unsigned width=widths[w]; if (shift+width>640) continue;
		image(f,width,2);
		for (size_t i=0;i<sizeof(f->primary);++i) { f->primary[i]=(uint8_t)((i*3+6)%16); f->alternate[i]=(uint8_t)((i*5+1)%16); }
		struct st_big_state before=f->state;
		CHECK(paste(f,(uint16_t)shift,2,(uint16_t)mode)==0 && height(f)==1);
		CHECK(memcmp(&before,&f->state,sizeof(before))==0);
		for (size_t i=0;i<sizeof(f->primary);++i) {
			unsigned x=(unsigned)(i%640), y=(unsigned)(i/640);
			uint8_t a=(uint8_t)((i*3+6)%16), b=(uint8_t)((i*5+1)%16);
			if (y>=2 && y<4 && x>=shift && x<shift+width) {
				unsigned source=source_pens[(x-shift)%8]^(y==3?15u:0u);
				a=expected(source,a,mode); b=expected(source,b,mode);
			}
			CHECK(f->primary[i]==a && f->alternate[i]==b);
		}
		unsigned bytes=(shift+width+7)/8;
		CHECK(f->count==16u*bytes);
		for (unsigned row=0;row<2;++row) for (unsigned p=0;p<4;++p) for (unsigned byte=0;byte<bytes;++byte) {
			const struct event *r=&f->events[((row*4u+p)*bytes+byte)*2], *out=r+1;
			CHECK(!r->write && out->write && r->address==(2+row)*80+byte && out->address==r->address);
			CHECK(out->planes==(8u>>p) && out->mode==mode && out->height==1 && r->height==1);
			uint8_t mask=255;
			if (byte==0) mask&=(uint8_t)(255u>>shift);
			if (byte+1==bytes) mask&=(uint8_t)(255u<<(7u-((shift+width-1)%8)));
			CHECK(out->mask==mask);
		}
	}
	free(f);
}
static void cropped_errors(void)
{
	struct fixture *f=create();
	for (unsigned mode=0;mode<5;++mode) for (unsigned fail=0;fail<16;++fail) {
		image(f,9,5); memset(f->primary,6,sizeof(f->primary)); memset(f->alternate,9,sizeof(f->alternate));
		f->fail=fail; struct st_big_state before=f->state;
		CHECK(paste(f,5,348,(uint16_t)mode)==IO_ERROR_IO && f->count==fail+1 && height(f)==1);
		CHECK(memcmp(&before,&f->state,sizeof(before))==0);
		for (size_t i=0;i<sizeof(f->primary);++i) {
			uint8_t a=6,b=9;
			for (unsigned accepted=0;accepted<fail/2;++accepted) {
				unsigned byte=accepted%2, plane=3-accepted/2;
				if (i/8==348*80+byte && i%640>=5 && i%640<=13) {
					unsigned source=source_pens[(i%640-5)%8], bit=1u<<plane;
					a=(uint8_t)((a&~bit)|(expected(source,a,mode)&bit));
					b=(uint8_t)((b&~bit)|(expected(source,b,mode)&bit));
				}
			}
			CHECK(f->primary[i]==a && f->alternate[i]==b);
		}
		for (size_t i=0;i<f->count;++i) CHECK(f->events[i].height==1);
	}
	for (unsigned fail=0;fail<2;++fail) {
		image(f,9,5); f->fail=fail; f->failure=IO_ERROR_IN_PROGRESS;
		CHECK(paste(f,5,348,1)==IO_ERROR_PROTOCOL && height(f)==1);
	}
	f->failure=IO_ERROR_IO;
	image(f,9,5); CHECK(paste(f,5,348,0)==0 && height(f)==4 && f->count==32);
	for (size_t i=0;i<f->count;++i) CHECK(f->events[i].height==1);
	free(f);
}
static void geometry_and_dispatch(void)
{
	struct fixture *f=create();
	static const struct { uint16_t x,y,ox,oy; size_t count; uint16_t first; } cases[]={
		{637,0,0,0,0,0}, /* width4 ends at640, whole image rejected */
		{636,0,0,0,8,79},
		{65535,0,0,0,0,0}, /* wrapped right end passes, X ADD/JL rejects */
		{65535,0,1,0,8,0},{1,0,65535,0,8,0},
		{0,348,0,0,16,27840}, /* bottom crop: two rows */
		{0,350,0,0,40,28000}, /* unsigned remainingffff: all five rows */
		{0,65535,0,0,0,0},{0,32768,0,32768,0,0},
		{0,32767,0,1,40,0}, /* positive signed overflow: JL is false */
		{0,65535,0,1,40,0},{0,1,0,65535,40,0},
	};
	for (size_t i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
		image(f,4,cases[i].count==8?1:5);
		f->state.bgi.viewport[0]=cases[i].ox; f->state.bgi.viewport[1]=cases[i].oy;
		uint16_t old=height(f); CHECK(paste(f,cases[i].x,cases[i].y,0)==0 && f->count==cases[i].count && height(f)==old);
		if (f->count!=0) CHECK(f->events[0].address==cases[i].first);
	}
	f->state.bgi.viewport[0]=f->state.bgi.viewport[1]=0;
	/* Physical max rather than viewport clip; temporary header precedes
	 * the reached installed/mode/layout dispatch checks. */
	f->state.bgi.viewport[2]=f->state.bgi.viewport[3]=0;
	image(f,9,5); CHECK(paste(f,16,10,0x0104)==0 && f->count==80 && height(f)==4 && f->events[1].mode==4);
	image(f,9,5); CHECK(paste(f,0,348,9)==IO_ERROR_OUT_OF_RANGE && f->count==0 && height(f)==1);
	image(f,9,5); CHECK(paste(f,639,348,9)==0 && f->count==0 && height(f)==4);
	image(f,9,5); f->state.bgi.state=0;
	CHECK(paste(f,0,348,0)==IO_ERROR_PROTOCOL && f->state.bgi.error==-1 && height(f)==1 && f->count==0);
	f->state.bgi.state=3; image(f,1,5); put_word(f->bytes,8);
	CHECK(paste(f,0,348,0)==IO_ERROR_PROTOCOL && height(f)==1 && f->count==0);
	/* Each physical mode crops against its actual maxY. */
	static const unsigned heights[]={200,350,480};
	for (size_t i=0;i<3;++i) {
		f->state.bgi.height=heights[i]; image(f,9,5);
		CHECK(paste(f,5,(uint16_t)(heights[i]-2),2)==0 && f->count==32 && height(f)==4);
		CHECK(f->events[0].address==(heights[i]-2)*80 && f->events[0].height==1);
	}
	/* Public mode WORD reaches the device as AL; only a reached unsafe
	 * BYTE table index rejects. Check the whole WORD domain independently. */
	f->state.bgi.height=350; image(f,9,5);
	for (unsigned mode=0;mode<65536;++mode) {
		put_word(f->bytes+2,4); f->count=0;
		int status=paste(f,5,348,(uint16_t)mode);
		CHECK(status==((mode%256)<5?0:IO_ERROR_OUT_OF_RANGE));
		CHECK(f->count==((mode%256)<5?32u:0u));
		CHECK(height(f)==((mode%256)<5?4u:1u));
	}
	free(f);
}
static void flag_domain(void)
{
	struct fixture *f=create(); image(f,1,1);
	static const uint16_t origins[]={0,1,32767,32768,65535};
	for (unsigned value=0;value<65536;++value) for (size_t i=0;i<sizeof(origins)/sizeof(origins[0]);++i) {
		int x=value<32768?(int)value:(int)value-65536;
		int origin=origins[i]<32768?(int)origins[i]:(int)origins[i]-65536;
		unsigned absolute=(value+(unsigned)origins[i])%65536;
		f->state.bgi.viewport[0]=origins[i]; f->count=0;
		CHECK(paste(f,(uint16_t)value,0,0)==0);
		CHECK(f->count==((absolute<640 && x+origin>=0)?8u:0u));
		f->state.bgi.viewport[0]=0; f->state.bgi.viewport[1]=origins[i]; f->count=0;
		CHECK(paste(f,0,(uint16_t)value,0)==0);
		CHECK(f->count==(x+origin>=0?8u:0u));
		f->state.bgi.viewport[1]=0;
	}
	free(f);
}
static void source_extents_and_spill(void)
{
	struct fixture *f=create(); image(f,8,1); f->bytes[4]=0xa5;
	CHECK(st_big_image_bind(&f->image,f->bytes,5,f->pens,sizeof(f->pens))==0);
	memset(f->primary,6,sizeof(f->primary)); memset(f->alternate,9,sizeof(f->alternate));
	/* A single source byte produces both destination writes before the
	 * next plane's missing byte fails. Raw edits do not re-decode on paste. */
	f->bytes[4]=0;
	CHECK(paste(f,5,0,0)==IO_ERROR_OUT_OF_RANGE && f->count==4);
	CHECK(f->events[1].mask==7 && f->events[3].mask==0xf8 && f->events[3].planes==8);
	for (unsigned x=0;x<16;++x) {
		unsigned source=x>=5 && x<13 && ((0xa5u&(0x80u>>(x-5)))!=0)?8u:0u;
		uint8_t a=x>=5 && x<13?(uint8_t)(6|source):6;
		uint8_t b=x>=5 && x<13?(uint8_t)(1|source):9;
		CHECK(f->primary[x]==a && f->alternate[x]==b);
	}
	put_word(f->bytes,65535); put_word(f->bytes+2,0); f->bytes[4]=0xff;
	CHECK(st_big_image_import(&f->image)==0); f->count=0;
	CHECK(paste(f,1,0,4)==IO_ERROR_OUT_OF_RANGE && f->count==2 && f->events[1].mask==0x7f);
	/* Source cursor wraps into the allocator prefix after16382 complete
	 *1x1 rows, not an empty ffff-height image. Destination byte offsets
	 * wrap separately. No zero-valid-image or upfront capacity shortcut. */
	memset(f->bytes,0,sizeof(f->bytes)); put_word(f->bytes+2,65535);
	CHECK(st_big_image_bind(&f->image,f->bytes,sizeof(f->bytes),f->pens,sizeof(f->pens))==0); f->count=0;
	CHECK(paste(f,0,350,0)==IO_ERROR_OUT_OF_RANGE && f->count==131056 && height(f)==65535);
	/* Row start nearffff wraps the following row's WORD address. */
	image(f,9,2); CHECK(paste(f,0,819,0)==0 && f->count==32);
	CHECK(f->events[0].address==65520 && f->events[16].address==64);
	/* Missing source bytes fail before any destination access. */
	CHECK(st_big_image_bind(&f->image,f->bytes,4,f->pens,sizeof(f->pens))==0); f->count=0;
	CHECK(paste(f,0,0,0)==IO_ERROR_OUT_OF_RANGE && f->count==0);
	f->image.capacity=3; CHECK(paste(f,0,0,0)==IO_ERROR_OUT_OF_RANGE);
	free(f);
}
static void initial_and_bad_pixels(void)
{
	struct fixture *f=create(); memset(f->primary,6,sizeof(f->primary)); memset(f->alternate,9,sizeof(f->alternate));
	CHECK(paste(f,0,0,0)==0 && f->count==8 && f->primary[0]==0 && f->alternate[0]==0);
	for (unsigned i=1;i<8;++i) CHECK(f->primary[i]==6 && f->alternate[i]==9);
	image(f,9,5); f->primary[7]=16;
	CHECK(paste(f,0,348,0)==0); /* bad pen is outside reached byte addresses */
	image(f,9,5); f->primary[348*640+7]=16;
	CHECK(paste(f,0,348,0)==IO_ERROR_OUT_OF_RANGE && height(f)==1 && f->count==1);
	image(f,9,5); f->pens[0]=16;
	CHECK(paste(f,0,348,0)==IO_ERROR_OUT_OF_RANGE && height(f)==1 && f->count==0);
	CHECK(st_big_image_paste(NULL,&f->state,NULL,0,0,0)==IO_ERROR_INVALID_ARGUMENT);
	free(f);
}
int main(void)
{
	boolean_core(); normal_images(); cropped_errors(); geometry_and_dispatch();
	flag_domain(); source_extents_and_spill(); initial_and_bad_pixels(); return 0;
}

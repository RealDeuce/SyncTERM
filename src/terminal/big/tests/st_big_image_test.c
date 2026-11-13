/* Independent image bytes, addresses and failure prefixes, BIG25/40/56. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_image.h"
#include "io_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
struct fixture {
	struct st_big_state state;
	struct st_big_image image;
	uint8_t bytes[ST_BIG_IMAGE_CAPACITY], pens[ST_BIG_IMAGE_PENS], pixels[65536*8];
	uint16_t reads[140000];
	size_t count, fail;
	int failure;
};
static struct fixture *create(size_t capacity)
{
	struct fixture *f=calloc(1,sizeof(*f)); CHECK(f!=NULL);
	st_big_state_init(&f->state);
	f->state.bgi.width=640; f->state.bgi.height=350;
	f->state.bgi.viewport[2]=639; f->state.bgi.viewport[3]=349;
	f->state.bgi.x=1; f->state.bgi.y=2; f->state.bgi.ddo_x=3; f->state.bgi.ddo_y=4;
	f->state.bgi.device_x=5; f->state.bgi.device_y=6;
	f->fail=SIZE_MAX; f->failure=IO_ERROR_IO;
	CHECK(st_big_image_bind(&f->image,f->bytes,capacity,f->pens,sizeof(f->pens))==0);
	return f;
}
static int read_byte(void *context, uint16_t address, uint8_t pens[8])
{
	struct fixture *f=context;
	CHECK(f->count<sizeof(f->reads)/sizeof(f->reads[0]));
	f->reads[f->count]=address;
	if (f->count++==f->fail) return f->failure;
	memcpy(pens,f->pixels+(size_t)address*8,8); return 0;
}
static int capture(struct fixture *f, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
	const struct st_big_memory_sink sink={.context=f,.read=read_byte};
	return st_big_image_capture(&f->image,&f->state,&sink,x1,y1,x2,y2);
}
static void size_queries(void)
{
	CHECK(st_big_image_size(0,0,639,199)==64006);
	CHECK(st_big_image_size(0,0,639,349)==0);
	CHECK(st_big_image_size(0,0,639,479)==0);
	CHECK(st_big_image_size(0,0,0,0)==10);
	CHECK(st_big_image_size(0,0,3,0)==10);
	CHECK(st_big_image_size(0,0,8,0)==14);
	CHECK(st_big_image_size(0,0,65535,65535)==14);
	CHECK(st_big_image_size(0,0,32768,0)==16394);
	CHECK(st_big_image_size(0,0,0,16380)==65530);
	CHECK(st_big_image_size(0,0,0,16381)==65534);
	CHECK(st_big_image_size(0,0,0,16382)==0);
	/* Every WORD delta, against modular distance rather than flag logic. */
	static const unsigned orthogonal[]={0,1,7,8,639,199,349,32768,65535};
	for (unsigned delta=0;delta<65536;++delta) for (size_t c=0;c<sizeof(orthogonal)/sizeof(orthogonal[0]);++c) {
		unsigned a=delta<=32768?delta:65536-delta;
		unsigned b=orthogonal[c]<=32768?orthogonal[c]:65536-orthogonal[c];
		unsigned long pixels=(unsigned long)((a+8)/8)*4*(b+1)+6;
		uint16_t expected=pixels<=65535?(uint16_t)pixels:0;
		CHECK(st_big_image_size(123,456,(uint16_t)(123+delta),(uint16_t)(456+orthogonal[c]))==expected);
		CHECK(st_big_image_size((uint16_t)(123+delta),(uint16_t)(456+orthogonal[c]),123,456)==expected);
		pixels=(unsigned long)((b+8)/8)*4*(a+1)+6;
		expected=pixels<=65535?(uint16_t)pixels:0;
		CHECK(st_big_image_size(0,0,(uint16_t)orthogonal[c],(uint16_t)delta)==expected);
	}
}
static void format_and_import(void)
{
	struct fixture *f=create(ST_BIG_IMAGE_CAPACITY);
	/* Native defined initial bytes are supplied by the owner. No valid flag:
	 * an all-zero untouched buffer already denotes a1x1 image with pen0. */
	uint16_t width=99,height=99;
	CHECK(st_big_image_header(&f->image,&width,&height)==0 && width==0 && height==0);
	const uint8_t *pens=NULL; uint8_t plane=99;
	CHECK(st_big_image_source(&f->image,8,&pens,&plane)==0 && plane==3);
	for (unsigned i=0;i<8;++i) CHECK(pens[i]==0);
	static const uint8_t literal[]={3,0,0,0,0x10,0x20,0x40,0x80};
	memcpy(f->bytes,literal,sizeof(literal)); CHECK(st_big_image_import(&f->image)==0);
	for (unsigned p=0;p<4;++p) {
		CHECK(st_big_image_source(&f->image,(uint16_t)(8+p),&pens,&plane)==0 && plane==3-p);
		CHECK(pens==f->pens && memcmp(pens,"\1\2\4\10\0\0\0\0",8)==0);
	}
	/* A second row and trailing plane bytes exist beyond stored height0. */
	f->bytes[8]=0x80; f->bytes[9]=0x40; f->bytes[10]=0x20; f->bytes[11]=0x10;
	CHECK(st_big_image_import(&f->image)==0);
	CHECK(st_big_image_source(&f->image,12,&pens,&plane)==0 && plane==3 && memcmp(pens,"\10\4\2\1\0\0\0\0",8)==0);
	/* Source reads consume cached indices; only an explicit edge import
	 * converts a newly supplied packed representation. */
	f->bytes[4]=0;
	CHECK(st_big_image_source(&f->image,8,&pens,&plane)==0 && pens[3]==8);
	CHECK(st_big_image_import(&f->image)==0);
	CHECK(st_big_image_source(&f->image,8,&pens,&plane)==0 && pens[3]==0);
	static const uint8_t nine[]={8,0,0,0,0,0x80,0,0,0,0,0x80,0};
	memcpy(f->bytes,nine,sizeof(nine)); CHECK(st_big_image_import(&f->image)==0);
	CHECK(st_big_image_source(&f->image,8,&pens,&plane)==0 && pens[0]==1 && pens[8]==8);
	for (unsigned i=1;i<16;++i) if (i!=8) CHECK(pens[i]==0);
	/* Full-width malformed headers still have bounded indexed storage. */
	f->bytes[0]=f->bytes[1]=f->bytes[2]=f->bytes[3]=255;
	CHECK(st_big_image_import(&f->image)==0 && f->image.plane_bytes==8192 && f->image.pen_count==131072);
	CHECK(st_big_image_source(&f->image,65535,&pens,&plane)==0 && plane==0);
	/*8190-byte planes need three padded rows for the accessible prefix;
	 * this is close to the maximum indexed-cache extent, despite height. */
	f->bytes[0]=0xef; f->bytes[1]=0xff;
	CHECK(st_big_image_import(&f->image)==0 && f->image.plane_bytes==8190 && f->image.pen_count==196560);
	const uint8_t *saved=pens; uint8_t old_plane=plane;
	for (unsigned cursor=0;cursor<8;++cursor) {
		CHECK(st_big_image_source(&f->image,(uint16_t)cursor,&pens,&plane)==IO_ERROR_OUT_OF_RANGE);
		CHECK(pens==saved && plane==old_plane);
	}
	CHECK(f->image.pen_count<=sizeof(f->pens)); free(f);
}
static void capture_literals(void)
{
	struct fixture *f=create(ST_BIG_IMAGE_CAPACITY);
	memset(f->bytes,0xa5,sizeof(f->bytes));
	f->state.application.write_mode=1; f->state.bgi.write_mode=1; f->state.bgi.error=-11;
	f->pixels[0]=1; f->pixels[1]=2; f->pixels[2]=4; f->pixels[3]=8;
	struct st_big_state before=f->state;
	CHECK(capture(f,3,0,0,0)==0);
	static const uint8_t four[]={3,0,0,0,0x10,0x20,0x40,0x80};
	CHECK(memcmp(f->bytes,four,sizeof(four))==0 && f->bytes[8]==0xa5 && f->bytes[9]==0xa5);
	CHECK(memcmp(&before,&f->state,sizeof(before))==0 && f->count==8);
	for (unsigned i=0;i<8;++i) CHECK(f->reads[i]==i%2);
	memset(f->pixels,0,sizeof(f->pixels)); f->pixels[0]=1; f->pixels[8]=8;
	CHECK(capture(f,0,0,8,0)==0);
	static const uint8_t nine[]={8,0,0,0,0,0x80,0,0,0,0,0x80,0};
	CHECK(memcmp(f->bytes,nine,sizeof(nine))==0 && f->bytes[12]==0xa5 && f->bytes[13]==0xa5);
	static const uint8_t values[]={1,2,4,8,15,0,7,9,2};
	memcpy(f->pixels+5,values,sizeof(values)); f->count=0;
	CHECK(capture(f,5,0,13,0)==0);
	static const uint8_t shifted[]={8,0,0,0,0x19,0,0x2a,0,0x4a,0x80,0x8b,0};
	CHECK(memcmp(f->bytes,shifted,sizeof(shifted))==0 && f->count==12);
	for (unsigned i=0;i<12;++i) CHECK(f->reads[i]==i%3);
	/* Start translation, unsigned sort, no viewport clipping or point update. */
	f->state.bgi.viewport[0]=5; f->state.bgi.viewport[1]=1;
	f->state.bgi.viewport[2]=5; f->state.bgi.viewport[3]=1;
	memcpy(f->pixels+80*8+5,values,sizeof(values)); f->count=0; before=f->state;
	CHECK(capture(f,8,0,0,0)==0 && memcmp(f->bytes,shifted,sizeof(shifted))==0);
	CHECK(memcmp(&before,&f->state,sizeof(before))==0);
	for (unsigned i=0;i<12;++i) CHECK(f->reads[i]==80+i%3);
	f->state.bgi.viewport[0]=f->state.bgi.viewport[1]=65535;
	f->pixels[0]=1; f->pixels[1]=2; f->pixels[2]=4; f->pixels[3]=8; f->count=0;
	CHECK(capture(f,1,1,4,1)==0 && memcmp(f->bytes,four,sizeof(four))==0 && f->count==8);
	for (unsigned i=0;i<8;++i) CHECK(f->reads[i]==i%2);
	free(f);
}
static void alignment_and_rows(void)
{
	struct fixture *f=create(ST_BIG_IMAGE_CAPACITY);
	for (size_t i=0;i<sizeof(f->pixels);++i) f->pixels[i]=(uint8_t)((i*7+i/13)%16);
	static const unsigned widths[]={1,2,7,8,9,15,16,17,31,32,33,639,640};
	static const unsigned heights[]={200,350,480};
	for (size_t mode=0;mode<3;++mode) for (unsigned shift=0;shift<8;++shift) for (size_t w=0;w<sizeof(widths)/sizeof(widths[0]);++w) {
		unsigned width=widths[w], first_y=heights[mode]-1;
		f->state.bgi.height=heights[mode]; f->count=0;
		memset(f->bytes,0xa5,sizeof(f->bytes));
		/* Includes row one beyond the physical bottom; the memory adapter,
		 * not the image kernel, decides actual aperture availability. */
		CHECK(capture(f,(uint16_t)shift,(uint16_t)(first_y+1),(uint16_t)(shift+width-1),(uint16_t)first_y)==0);
		unsigned columns=(width+7)/8; size_t index=4;
		for (unsigned y=first_y;y<=first_y+1;++y) for (unsigned plane=4;plane--!=0;) for (unsigned byte=0;byte<columns;++byte) {
			uint8_t expected=0;
			for (unsigned pixel=0;pixel<8;++pixel) {
				unsigned x=byte*8+pixel;
				if (x<width && (f->pixels[(size_t)y*640+shift+x]&(1u<<plane))!=0) expected|=(uint8_t)(0x80u>>pixel);
			}
			CHECK(f->bytes[index++]==expected);
		}
		CHECK(f->bytes[index]==0xa5 && f->bytes[index+1]==0xa5);
		CHECK(f->count==8u*(columns+1));
		for (unsigned row=0;row<2;++row) for (unsigned p=0;p<4;++p) for (unsigned read=0;read<=columns;++read)
			CHECK(f->reads[(row*4u+p)*(columns+1)+read]==(first_y+row)*80+read);
		/* Indexed cache includes the padded low pixels as zero. */
		for (unsigned row=0;row<2;++row) for (unsigned x=0;x<columns*8;++x)
			CHECK(f->pens[row*columns*8+x]==(x<width?f->pixels[(first_y+row)*640+shift+x]:0));
	}
	free(f);
}
static void error_prefixes(void)
{
	static const uint8_t values[]={1,2,4,8,15,0,7,9,2};
	static const uint8_t payload[]={0x19,0,0x2a,0,0x4a,0x80,0x8b,0};
	struct fixture *f=create(ST_BIG_IMAGE_CAPACITY);
	memcpy(f->pixels+5,values,sizeof(values));
	for (unsigned fail=0;fail<12;++fail) {
		memset(f->bytes,0xa5,sizeof(f->bytes)); f->count=0; f->fail=fail;
		CHECK(capture(f,5,0,13,0)==IO_ERROR_IO && f->count==fail+1);
		CHECK(f->bytes[0]==8 && f->bytes[1]==0 && f->bytes[2]==0 && f->bytes[3]==0);
		size_t stored=fail/3u*2u+(fail%3u==2?1u:0u);
		CHECK(memcmp(f->bytes+4,payload,stored)==0);
		for (size_t i=4+stored;i<sizeof(f->bytes);++i) CHECK(f->bytes[i]==0xa5);
		for (size_t i=0;i<f->count;++i) CHECK(f->reads[i]==i%3);
		for (unsigned pixel=0;pixel<16;++pixel) {
			uint8_t expected=0;
			for (unsigned p=0;p<4;++p) {
				size_t byte=p*2u+pixel/8;
				uint8_t value=byte<stored?payload[byte]:0xa5;
				if ((value&(0x80u>>(pixel%8)))!=0) expected|=(uint8_t)(8u>>p);
			}
			CHECK(f->pens[pixel]==expected);
		}
	}
	f->count=0; f->fail=1; f->failure=IO_ERROR_IN_PROGRESS;
	CHECK(capture(f,0,0,0,0)==IO_ERROR_PROTOCOL && f->count==2);
	f->count=0; f->fail=SIZE_MAX; f->pixels[8]=16;
	CHECK(capture(f,0,0,0,0)==IO_ERROR_OUT_OF_RANGE && f->count==2);
	f->count=0; f->state.bgi.state=0; f->state.bgi.error=-11;
	CHECK(capture(f,8,6,2,3)==IO_ERROR_PROTOCOL && f->count==0 && f->state.bgi.error==-1);
	CHECK(memcmp(f->bytes,"\6\0\3\0",4)==0);
	free(f);
}
static void actual_extents_and_wrap(void)
{
	struct fixture *f=create(5); memset(f->pixels,15,sizeof(f->pixels));
	memset(f->bytes,0xa5,sizeof(f->bytes));
	CHECK(capture(f,0,0,0,0)==IO_ERROR_OUT_OF_RANGE && f->count==4 && f->bytes[4]==0x80 && f->bytes[5]==0xa5);
	CHECK(f->pens[0]==8 && f->pens[1]==0);
	const uint8_t *pens=f->pens; uint8_t plane=2;
	CHECK(st_big_image_source(&f->image,9,&pens,&plane)==IO_ERROR_OUT_OF_RANGE && pens==f->pens && plane==2);
	/* ffff height enters a65536-row loop; a small owned extent stops it
	 * on the fourth row, after the three complete encoded rows. */
	CHECK(st_big_image_bind(&f->image,f->bytes,16,f->pens,sizeof(f->pens))==0); f->count=0;
	CHECK(capture(f,0,0,0,65535)==IO_ERROR_OUT_OF_RANGE && f->count==26);
	CHECK(f->reads[24]==240 && f->reads[25]==241 && f->bytes[2]==255 && f->bytes[3]==255);
	f->count=0;
	CHECK(capture(f,0,0,65535,0)==IO_ERROR_OUT_OF_RANGE && f->count==14);
	CHECK(memcmp(f->bytes,"\377\377\0\0",4)==0);
	for (unsigned i=0;i<14;++i) CHECK(f->reads[i]==i);
	for (unsigned i=4;i<16;++i) CHECK(f->bytes[i]==255);
	CHECK(st_big_image_bind(&f->image,f->bytes,sizeof(f->bytes),f->pens,sizeof(f->pens))==0);
	f->count=0; CHECK(capture(f,0,819,128,819)==0 && f->count==72);
	for (unsigned p=0;p<4;++p) for (unsigned i=0;i<18;++i) CHECK(f->reads[p*18+i]==(uint16_t)(65520u+i));
	/* Full screen query0 is not a preflight rejection.204 rows plus248
	 * plane bytes are accepted before the next store reaches B:0000.
	 *65529 lookaheads plus820 initial plane reads precede that store. */
	memset(f->bytes,0xa5,sizeof(f->bytes)); f->count=0;
	CHECK(capture(f,0,0,639,349)==IO_ERROR_OUT_OF_RANGE && f->count==66349);
	CHECK(memcmp(f->bytes,"\177\2\135\1",4)==0 && f->reads[f->count-1]==16329);
	for (size_t i=4;i<65532;++i) CHECK(f->bytes[i]==255);
	for (size_t i=65532;i<sizeof(f->bytes);++i) CHECK(f->bytes[i]==0xa5);
	free(f);
}
static void invalid_bindings(void)
{
	struct fixture *f=create(ST_BIG_IMAGE_CAPACITY); struct st_big_image before=f->image;
	CHECK(st_big_image_bind(&f->image,f->bytes,3,f->pens,sizeof(f->pens))==IO_ERROR_OUT_OF_RANGE);
	CHECK(memcmp(&before,&f->image,sizeof(before))==0);
	CHECK(st_big_image_bind(&f->image,f->bytes,sizeof(f->bytes),f->pens,7)==IO_ERROR_OUT_OF_RANGE);
	CHECK(memcmp(&before,&f->image,sizeof(before))==0);
	CHECK(st_big_image_bind(&f->image,f->bytes,ST_BIG_IMAGE_CAPACITY+1,f->pens,sizeof(f->pens))==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_image_bind(NULL,f->bytes,4,f->pens,sizeof(f->pens))==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_image_import(NULL)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_image_source(&f->image,8,NULL,NULL)==IO_ERROR_INVALID_ARGUMENT);
	/* A failed new layout publishes its header but cannot expose the old
	 * cache. Restoring the real cache extent permits explicit reimport. */
	f->image.pen_capacity=8;
	CHECK(capture(f,0,0,8,0)==IO_ERROR_OUT_OF_RANGE && f->count==0 && f->bytes[0]==8);
	const uint8_t *pens=f->pens; uint8_t plane=99;
	CHECK(st_big_image_source(&f->image,8,&pens,&plane)==IO_ERROR_OUT_OF_RANGE && pens==f->pens && plane==99);
	f->image.pen_capacity=sizeof(f->pens); CHECK(st_big_image_import(&f->image)==0);
	f->image.capacity=3; uint16_t width=99,height=99;
	CHECK(st_big_image_header(&f->image,&width,&height)==IO_ERROR_OUT_OF_RANGE && width==99 && height==99);
	CHECK(capture(f,0,0,0,0)==IO_ERROR_OUT_OF_RANGE && f->count==0);
	free(f);
}
int main(void)
{
	size_queries(); format_and_import(); capture_literals(); alignment_and_rows();
	error_prefixes(); actual_extents_and_wrap(); invalid_bindings(); return 0;
}

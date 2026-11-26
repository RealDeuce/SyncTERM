/* Published metric/cache vectors and independent ownership/failure checks. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_outline_cache.h"
#include "st_rip3_outline_font.h"
#include "st_rip3_font_data.h"
#include "io_error.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr,"check failed at line %d: %s\n",__LINE__,#expression); \
		return 1; \
	} \
} while (0)

extern bool st_rip3_outline_cache_test_fail;
extern size_t st_rip3_outline_cache_test_allocations;

static int
width(const struct st_rip3_outline_cache *cache, const uint8_t *text, size_t length,
    int64_t wide, int word)
{
	struct st_rip3_outline_width value;
	CHECK(st_rip3_outline_cache_measure(&value,cache,text,length)==0
	    && value.advance==wide && value.cursor==word);
	return 0;
}

static void
word(uint8_t *field, unsigned value)
{
	field[0]=(uint8_t)value;
	field[1]=(uint8_t)(value>>8);
}

static int
bounded(void)
{
	/* Independent PR #225 vectors. At 72 DPI each glyph steps one pixel
	 * per point, and the logical height increases at the same rate. */
	const struct st_rip3_font_asset *asset=st_rip3_font_asset_at(5);
	uint8_t *copy=malloc(asset->size);
	CHECK(copy!=NULL);
	memcpy(copy,asset->data,asset->size);
	struct st_rip3_rff file;
	CHECK(st_rip3_rff_open(&file,copy,asset->size)==0 && file.units_per_em==1000);
	copy[file.style_offset+3]=100;
	word(copy+file.base+0x16,0);
	word(copy+file.base+0x18,65536-1000);
	word(copy+file.base+0x1a,2000);
	word(copy+file.base+0x1c,0);
	for (unsigned i=0;i<file.characters;i++)
		word(copy+file.advances+2*i,1000);
	struct st_rip3_frame frame;
	CHECK(st_rip3_frame_init(&frame,ST_RIP3_CANVAS_640_480)==0
	    && st_rip3_frame_world(&frame,640,480)==0);
	struct st_rip3_outline_cache *cache=NULL;
	const uint8_t text[]="AB";
	const struct {
		unsigned box_width, box_height, shadow, flags, points, spacing, advance;
	} cases[]={
		{40,24,0,0,20,100,40},
		{14,8,0,0,7,100,14},
		{26,10,0,0,10,130,26},
		{38,20,2,0,18,100,36},
		{21,7,0,0,7,157,20},
		{10000,1,0,0,1,4095,80},
		{26,10,0,0x400,10,115,26},
	};
	for (size_t i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
		struct st_rip3_outline_font font;
		unsigned spacing=100;
		CHECK(st_rip3_outline_font_bind(&font,&file,0,&frame,10,cases[i].flags,0)==0);
		CHECK(st_rip3_outline_cache_fit(&cache,&font,&spacing,&frame,cases[i].shadow,
		    cases[i].box_width,cases[i].box_height,text,2)==0);
		CHECK(font.points==cases[i].points && spacing==cases[i].spacing);
		CHECK(width(cache,text,2,cases[i].advance,(int)cases[i].advance)==0
		    && st_rip3_outline_cache_height(cache)==cases[i].points);
	}
	struct st_rip3_outline_font font;
	unsigned spacing=100;
	CHECK(st_rip3_outline_font_bind(&font,&file,0,&frame,10,0,0)==0);
	CHECK(st_rip3_outline_cache_fit(&cache,&font,&spacing,&frame,0,1,1,text,2)==IO_ERROR_OUT_OF_RANGE);
	CHECK(font.points==10 && spacing==100 && width(cache,text,2,26,26)==0
	    && st_rip3_outline_cache_height(cache)==10);
	/* One design unit per em and the full u16 advance make the unscaled
	 * logical-width times DPI exceed uint64_t. The pixel result fits. */
	word(copy+44,1);
	word(copy+file.base+12,1);
	word(copy+file.advances+2*('A'-file.first_character),65535);
	CHECK(st_rip3_frame_init_device(&frame,59000000,480)==0 && st_rip3_frame_world(&frame,1,480)==0);
	CHECK(st_rip3_outline_font_bind(&font,&file,0,&frame,4095,0,0)==0);
	CHECK(st_rip3_outline_cache_prepare(cache,&font,100)==0);
	CHECK(width(cache,text,1,INT64_C(65535)*4095*59000000,17600)==0);
	st_rip3_outline_cache_destroy(cache);
	free(copy);
	return 0;
}

int
main(void)
{
	struct st_rip3_outline_cache *cache=NULL,*other=NULL;
	struct st_rip3_outline_font font;
	struct st_rip3_frame frame;
	CHECK(st_rip3_outline_cache_create(&cache)==0 && !st_rip3_outline_cache_valid(cache) && st_rip3_outline_cache_height(cache)==0
	    && st_rip3_outline_cache_height(NULL)==0);
	CHECK(width(cache,(const uint8_t *)"F",1,0,0)==0 && width(NULL,(const uint8_t *)"F",1,0,0)==0);
	CHECK(st_rip3_frame_init(&frame,ST_RIP3_CANVAS_640_480)==0 && st_rip3_frame_world(&frame,640,960)==0);
	for (unsigned rotation=0; rotation<4; rotation++) {
		CHECK(st_rip3_outline_font_builtin(&font,"Marin",&frame,90,0,rotation)==0);
		CHECK(st_rip3_outline_cache_prepare(cache,&font,100)==0 && st_rip3_outline_cache_valid(cache)
		    && st_rip3_outline_cache_height(cache)==52);
		for (unsigned string=0; string<4; string++) {
			int32_t ordinary;
			CHECK(st_rip3_outline_font_advances(&ordinary,&font,900*string,(const uint8_t *)"F",1)==0
			    && ordinary==(string%2==0 ? 50 : 25));
			CHECK(width(cache,(const uint8_t *)"F",1,50,50)==0);
		}
	}
	const uint8_t high[]={0x80,0xff};
	CHECK(width(cache,high,2,105,105)==0);
	const uint8_t terminated[]={'F',0,'F'};
	CHECK(width(cache,terminated,sizeof(terminated),50,50)==0);
	uint8_t text[700];memset(text,'F',sizeof(text));
	CHECK(width(cache,text,260,13000,13000)==0 && width(cache,text,700,35000,-30536)==0);
	CHECK(st_rip3_outline_font_builtin(&font,"Marin",&frame,90,0,0)==0);
	CHECK(st_rip3_outline_cache_prepare(cache,&font,125)==0 && width(cache,text,1,62,62)==0);
	font.flags|=0x400;
	CHECK(st_rip3_outline_cache_prepare(cache,&font,125)==0 && width(cache,text,1,75,75)==0);
	const uint8_t missing[]={31};
	CHECK(width(cache,missing,1,25,25)==0);
	CHECK(st_rip3_outline_cache_prepare(cache,&font,50)==0 && width(cache,text,1,-1,-1)==0);
	CHECK(width(cache,missing,1,-51,-51)==0);
	CHECK(st_rip3_outline_cache_prepare(cache,&font,1)==0 && width(cache,text,700,-35000,30536)==0);
	/* Font selection/frame changes do not prepare or alter the snapshot. */
	CHECK(st_rip3_outline_font_builtin(&font,"Marin",&frame,90,0,0)==0);
	CHECK(st_rip3_outline_cache_prepare(cache,&font,100)==0);
	CHECK(st_rip3_frame_world(&frame,1280,960)==0);
	CHECK(st_rip3_outline_font_builtin(&font,"Marin Cn",&frame,90,0,0)==0);
	CHECK(width(cache,text,1,50,50)==0);
	CHECK(st_rip3_outline_cache_prepare(cache,&font,100)==0 && width(cache,text,1,18,18)==0);
	/* Every preparation failure retains the complete old DPI/face/spacing. */
	for (unsigned which=0; which<7; which++) {
		struct st_rip3_outline_font bad=font;unsigned spacing=125;
		switch (which) {
			case 0:bad.points=0;break; case 1:bad.file.size=1;break;
			case 2:bad.bounds.xmin=1;bad.bounds.xmax=0;break;
			case 3:bad.bounds.xmin=INT64_MIN;bad.bounds.xmax=INT64_MAX;break;
			case 4:spacing=0;break;case 5:spacing=4096;break;
			default:bad.flags|=0x400;bad.bounds.xmin=0;bad.bounds.xmax=INT64_MAX;spacing=4095;break;
		}
		CHECK(st_rip3_outline_cache_prepare(cache,&bad,spacing)!=0 && width(cache,text,1,18,18)==0
		    && st_rip3_outline_cache_height(cache)==52);
	}
	CHECK(st_rip3_outline_font_builtin(&font,"absent",&frame,90,0,0)==IO_ERROR_NOT_FOUND);
	CHECK(width(cache,text,1,18,18)==0);
	CHECK(st_rip3_outline_cache_create(&other)==0 && width(other,text,1,0,0)==0);
	st_rip3_outline_cache_reset(cache);
	CHECK(st_rip3_outline_cache_height(cache)==0);
	CHECK(!st_rip3_outline_cache_valid(cache) && width(cache,text,700,0,0)==0);
	/* Snapshot owns metrics even after the entire backing font is freed; its
	 * F outline is deliberately damaged while the declared advance stays 556. */
	const struct st_rip3_font_asset *asset=st_rip3_font_asset_at(5);
	uint8_t *copy=malloc(asset->size);struct st_rip3_rff file;
	CHECK(copy!=NULL);memcpy(copy,asset->data,asset->size);
	CHECK(st_rip3_rff_open(&file,copy,asset->size)==0);
	memset(copy+file.offsets+4*('F'-file.first_character),255,4);
	CHECK(st_rip3_frame_world(&frame,640,480)==0);
	CHECK(st_rip3_outline_font_bind(&font,&file,0,&frame,90,15,0)==0);
	CHECK(st_rip3_outline_cache_prepare(cache,&font,100)==0);free(copy);
	CHECK(width(cache,text,1,50,50)==0 && width(other,text,1,0,0)==0);
	/* Large prepared spans exercise exact spacing overflow guards and a late
	 * sum overflow without forcing a huge allocation or invalid source read. */
	CHECK(st_rip3_outline_font_builtin(&font,"Marin",&frame,90,0x400,0)==0);
	font.bounds.xmin=0;font.bounds.xmax=INT64_MAX;
	CHECK(st_rip3_outline_cache_prepare(cache,&font,101)==0);
	CHECK(width(cache,text,1,INT64_C(92233720368547808),18400)==0);
	struct st_rip3_outline_width value,before;memset(&value,0x5a,sizeof(value));memcpy(&before,&value,sizeof(value));
	CHECK(st_rip3_outline_cache_measure(&value,cache,text,100)==IO_ERROR_OUT_OF_RANGE
	    && memcmp(&value,&before,sizeof(value))==0 && st_rip3_outline_cache_valid(cache));
	CHECK(st_rip3_outline_cache_measure(&value,cache,NULL,1)==IO_ERROR_INVALID_ARGUMENT
	    && memcmp(&value,&before,sizeof(value))==0);
	CHECK(st_rip3_outline_cache_measure(NULL,cache,text,1)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_outline_cache_prepare(NULL,&font,100)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_outline_cache_prepare(cache,NULL,100)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(width(cache,NULL,0,0,0)==0);
	/* Positive WORLD dimensions can truncate effective DPI to zero. These
	 * are valid metric snapshots, distinct from a zero WORLD failure. */
	CHECK(st_rip3_frame_world(&frame,65535,65535)==0);
	CHECK(st_rip3_outline_font_builtin(&font,"Marin",&frame,90,0,0)==0
	    && font.dpi_x==0 && font.dpi_y==0);
	CHECK(st_rip3_outline_cache_prepare(cache,&font,100)==0
	    && st_rip3_outline_cache_valid(cache) && st_rip3_outline_cache_height(cache)==0
	    && width(cache,text,1,0,0)==0);
	/* Factory failure preserves an already-owned pointer. */
	st_rip3_outline_cache_test_fail=true;
	struct st_rip3_outline_cache *saved=other;
	CHECK(st_rip3_outline_cache_create(&other)==IO_ERROR_OUT_OF_MEMORY && other==saved);
	st_rip3_outline_cache_test_fail=false;
	CHECK(st_rip3_outline_cache_create(NULL)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_outline_cache_test_allocations==3);
	st_rip3_outline_cache_reset(NULL);st_rip3_outline_cache_destroy(NULL);
	st_rip3_outline_cache_destroy(other);st_rip3_outline_cache_destroy(cache);
	return bounded();
}

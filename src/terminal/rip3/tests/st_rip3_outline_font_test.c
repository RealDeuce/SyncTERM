/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_outline_font.h"
#include "st_rip3_font_data.h"
#include "io_error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr,"check failed at line %d: %s\n",__LINE__,#expression); \
		return 1; \
	} \
} while (0)

static int
catalogue(void)
{
	static const char *const families[]={"Brush","Cobb","DEFAULT","Dixon","Eureka","Marin","Oakland","Symbol"};
	static const char *const suffixes[]={""," Th"," Cn"," Wd"," Ex"," Ho"," HT"," HC"," HW"," HE"};
	static const unsigned percent[]={100,85,75,125,150,100,85,75,125,150};
	struct st_rip3_frame frame;
	CHECK(st_rip3_frame_init(&frame,ST_RIP3_CANVAS_640_480)==0 && st_rip3_frame_world(&frame,640,480)==0);
	for (size_t family=0; family<8; family++) for (size_t face=0; face<10; face++) {
		char name[40];
		struct st_rip3_outline_font font;
		const struct st_rip3_font_asset *asset=st_rip3_font_asset_at(family);
		(void)snprintf(name,sizeof(name),"%s%s",families[family],suffixes[face]);
		/* Mixed case deliberately differs from both historical converter runs. */
		for (size_t i=0; name[i]!=0; i++) {
			if (i%2==0 && name[i]>='a' && name[i]<='z') name[i]-='a'-'A';
			else if (i%2!=0 && name[i]>='A' && name[i]<='Z') name[i]+='a'-'A';
		}
		CHECK(st_rip3_outline_font_builtin(&font,name,&frame,90,0,0)==0);
		CHECK(font.file.data==asset->data && font.file.size==asset->size && font.face==face
		    && font.points==90 && font.dpi_x==72 && font.dpi_y==72 && font.orientation==0 && font.flags==0);
		CHECK(font.file.bold_hollow_strength==4 && font.file.hollow_strength==2);
		CHECK(font.style.hollow==(face>=5) && font.style.horizontal_percent==(family==2 && face==4 ? 140 : percent[face]));
		CHECK(font.scale_x==0.09 && font.scale_y==0.09 && font.hollow_thickness==(face>=5 ? 2u : 0u));
		/* A prepared view does not borrow the mutable lookup string. */
		memset(name,'?',sizeof(name)); CHECK(font.style.name[0]!='?');
	}
	return 0;
}

static int
sizes(void)
{
	static const unsigned points[]={1,25,50,90,120,200,250,4095};
	static const unsigned normal[]={1,1,2,2,3,5,5,5};
	static const unsigned bold[]={2,2,3,4,5,8,8,8};
	struct st_rip3_frame frame;
	struct st_rip3_outline_font font;
	CHECK(st_rip3_frame_init(&frame,ST_RIP3_CANVAS_800_600)==0 && st_rip3_frame_world(&frame,800,600)==0);
	for (size_t i=0; i<sizeof(points)/sizeof(points[0]); i++) for (unsigned orientation=0; orientation<4; orientation++) {
		CHECK(st_rip3_outline_font_builtin(&font,"Marin HO",&frame,points[i],0,orientation)==0 && font.hollow_thickness==normal[i]);
		CHECK(st_rip3_outline_font_builtin(&font,"Marin HO",&frame,points[i],1,orientation)==0 && font.hollow_thickness==bold[i]);
		CHECK(st_rip3_outline_font_builtin(&font,"Marin HO",&frame,points[i],2,orientation)==0 && font.hollow_thickness==normal[i]);
	}
	/* Published width independence: 90-point 150% hollow remains diameter 2. */
	CHECK(st_rip3_outline_font_builtin(&font,"Marin HE",&frame,90,0,0)==0
	    && font.style.horizontal_percent==150 && font.hollow_thickness==2);
	/* DPI truncation precedes point scaling: 72*600/350 truncates to 123. */
	CHECK(st_rip3_frame_world(&frame,640,350)==0);
	CHECK(st_rip3_outline_font_builtin(&font,"Cobb",&frame,25,0,0)==0 && font.dpi_x==90 && font.dpi_y==123);
	CHECK(font.scale_x==25.0*90/(72*1000) && font.scale_y==25.0*123/(72*1000));
	/* Anisotropic DPI, including quarter-turn selection and BOLD caps. */
	CHECK(st_rip3_frame_world(&frame,800,300)==0);
	for (unsigned orientation=0; orientation<4; orientation++) {
		CHECK(st_rip3_outline_font_builtin(&font,"Dixon HT",&frame,90,0,orientation)==0
		    && font.dpi_x==72 && font.dpi_y==144 && font.hollow_thickness==(orientation%2==0 ? 2u : 4u));
	}
	/* The recovered low-WORD coefficient/DPI product is observable at extreme
	 * legal world frames; omitting it would incorrectly select capped stamps. */
	CHECK(st_rip3_frame_init(&frame,ST_RIP3_CANVAS_640_480)==0 && st_rip3_frame_world(&frame,1,1)==0);
	CHECK(st_rip3_outline_font_builtin(&font,"Marin Ho",&frame,1,0,0)==0 && font.hollow_thickness==4);
	CHECK(st_rip3_outline_font_builtin(&font,"Marin Ho",&frame,1,0,1)==0 && font.hollow_thickness==1);
	CHECK(st_rip3_outline_font_builtin(&font,"Marin Ho",&frame,1,1,0)==0 && font.hollow_thickness==8);
	CHECK(st_rip3_outline_font_builtin(&font,"Marin Ho",&frame,1,1,1)==0 && font.hollow_thickness==2);
	for (unsigned canvas=0; canvas<3; canvas++) {
		CHECK(st_rip3_frame_init(&frame,(enum st_rip3_canvas)canvas)==0
		    && st_rip3_frame_world(&frame,32767,32767)==0);
		CHECK(st_rip3_outline_font_builtin(&font,"Marin",&frame,4095,0,0)==0 && font.dpi_x>=1 && font.dpi_y>=1);
	}
	return 0;
}

static int
coefficients(void)
{
	const struct st_rip3_font_asset *asset=st_rip3_font_asset_at(5); /* MARIN */
	uint8_t *bytes=malloc(asset->size);
	struct st_rip3_rff file;
	struct st_rip3_frame frame;
	struct st_rip3_outline_font font;
	CHECK(bytes!=NULL);memcpy(bytes,asset->data,asset->size);
	CHECK(st_rip3_rff_open(&file,bytes,asset->size)==0);
	bytes[file.base+0x12]=6;bytes[file.base+0x13]=3;
	CHECK(st_rip3_frame_init(&frame,ST_RIP3_CANVAS_640_480)==0 && st_rip3_frame_world(&frame,640,480)==0);
	CHECK(st_rip3_outline_font_bind(&font,&file,5,&frame,100,0,0)==0
	    && font.file.bold_hollow_strength==6 && font.file.hollow_strength==3 && font.hollow_thickness==4);
	CHECK(st_rip3_outline_font_bind(&font,&file,5,&frame,100,1,0)==0 && font.hollow_thickness==7);
	free(bytes);return 0;
}

static int
errors(void)
{
	struct st_rip3_frame frame;
	struct st_rip3_outline_font font, saved;
	CHECK(st_rip3_frame_init(&frame,ST_RIP3_CANVAS_1024_768)==0);
	CHECK(st_rip3_outline_font_builtin(&font,"DEFAULT Ex",&frame,25,0xffffff,3)==0);
	memcpy(&saved,&font,sizeof(saved));
	static const char *const absent[]={"","missing","Marin.RFF"," Marin","Marin ","Marin Thin"};
	for (size_t i=0; i<sizeof(absent)/sizeof(absent[0]); i++) {
		CHECK(st_rip3_outline_font_builtin(&font,absent[i],&frame,25,0,0)==IO_ERROR_NOT_FOUND);
		CHECK(memcmp(&font,&saved,sizeof(font))==0);
	}
	for (unsigned which=0; which<7; which++) {
		struct st_rip3_frame bad=frame;
		unsigned size=25,orientation=0;uint32_t flags=0;
		switch (which) {
			case 0:size=0;break;case 1:size=4096;break;case 2:orientation=4;break;
			case 3:flags=0x1000000;break;case 4:bad.world_width=0;break;
			case 5:bad.world_height=65536;break;default:bad.device_width=0;break;
		}
		CHECK(st_rip3_outline_font_builtin(&font,"Marin",&bad,size,flags,orientation)!=0 && memcmp(&font,&saved,sizeof(font))==0);
	}
	struct st_rip3_frame custom;
	CHECK(st_rip3_frame_init_device(&custom,720,400)==0);
	struct st_rip3_outline_font custom_font;
	CHECK(st_rip3_outline_font_builtin(&custom_font,"Marin",&custom,25,0,0)==0
	    && custom_font.dpi_x==81 && custom_font.dpi_y==82);
	CHECK(st_rip3_frame_world(&custom,32768,16384)==0);
	CHECK(st_rip3_outline_font_builtin(&custom_font,"Marin",&custom,25,0,0)==0
	    && custom_font.dpi_x==1 && custom_font.dpi_y==1);
	struct st_rip3_rff file=saved.file;file.units_per_em=1; /* Revalidate bytes, not caller metadata. */
	CHECK(st_rip3_outline_font_bind(&font,&file,0,&frame,25,0,0)==0 && font.file.units_per_em==1000);
	memcpy(&saved,&font,sizeof(saved));
	CHECK(st_rip3_outline_font_bind(&font,&file,10,&frame,25,0,0)==IO_ERROR_NOT_FOUND && memcmp(&font,&saved,sizeof(font))==0);
	file.size=1;
	CHECK(st_rip3_outline_font_bind(&font,&file,0,&frame,25,0,0)==IO_ERROR_INVALID_FORMAT && memcmp(&font,&saved,sizeof(font))==0);
	CHECK(st_rip3_outline_font_builtin(NULL,"Marin",&frame,25,0,0)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_outline_font_builtin(&font,NULL,&frame,25,0,0)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_outline_font_builtin(&font,"Marin",NULL,25,0,0)==IO_ERROR_INVALID_ARGUMENT);
	return 0;
}

static void
put16(uint8_t *bytes, unsigned value)
{
	bytes[0]=(uint8_t)value;bytes[1]=(uint8_t)(value>>8);
}

static int
bounds(void)
{
	/* Device Y stays down in our geometric renderer. Nearest-edge rounding,
	 * half ties positive, is an explicit policy, not a native hinting oracle. */
	static const int expected[][4]={
		{-7,-82,95,23},{-22,-7,82,95},{-95,-22,7,82},{-82,-95,23,7},
	};
	static const int unequal[][4]={
		{-14,-82,191,23},{-45,-7,165,95},{-191,-22,14,82},{-165,-95,45,7},
	};
	struct st_rip3_frame frame;
	struct st_rip3_outline_font font,saved;
	int32_t width=77;
	CHECK(st_rip3_frame_init(&frame,ST_RIP3_CANVAS_640_480)==0
	    && st_rip3_frame_world(&frame,640,480)==0);
	for (unsigned orientation=0; orientation<4; orientation++) {
		CHECK(st_rip3_outline_font_builtin(&font,"Marin",&frame,90,0,orientation)==0);
		CHECK(font.bounds.xmin==expected[orientation][0] && font.bounds.ymin==expected[orientation][1]
		    && font.bounds.xmax==expected[orientation][2] && font.bounds.ymax==expected[orientation][3]);
		CHECK(st_rip3_outline_font_bbox_width(&width,&font)==0
		    && width==expected[orientation][2]-expected[orientation][0]);
		CHECK(st_rip3_frame_world(&frame,320,480)==0);
		CHECK(st_rip3_outline_font_builtin(&font,"Marin",&frame,90,0,orientation)==0);
		CHECK(font.bounds.xmin==unequal[orientation][0] && font.bounds.ymin==unequal[orientation][1]
		    && font.bounds.xmax==unequal[orientation][2] && font.bounds.ymax==unequal[orientation][3]);
		CHECK(st_rip3_frame_world(&frame,640,480)==0);
	}
	CHECK(st_rip3_outline_font_builtin(&font,"Marin HE",&frame,90,0,0)==0);
	CHECK(font.bounds.xmin==-11 && font.bounds.xmax==143 && font.hollow_thickness==2);
	CHECK(st_rip3_outline_font_bbox_width(&width,&font)==0 && width==154);
	CHECK(st_rip3_outline_font_builtin(&font,"Marin HE",&frame,90,0,3)==0);
	CHECK(font.bounds.xmin==-82 && font.bounds.xmax==23 && font.bounds.ymin==-143 && font.bounds.ymax==11);
	CHECK(st_rip3_outline_font_bbox_width(&width,&font)==0 && width==105);
	const struct st_rip3_font_asset *asset=st_rip3_font_asset_at(5);
	uint8_t *bytes=malloc(asset->size);
	CHECK(bytes!=NULL);memcpy(bytes,asset->data,asset->size);
	struct st_rip3_rff file;
	CHECK(st_rip3_rff_open(&file,bytes,asset->size)==0);
	const int negative[]={-10,-20,-2,-4};
	for (unsigned i=0; i<4; i++) put16(bytes+file.base+0x16+2*i,(unsigned)negative[i]&65535);
	CHECK(st_rip3_outline_font_bind(&font,&file,0,&frame,1000,0,0)==0);
	CHECK(font.file.xmax==-2 && font.file.ymax==-4 && font.bounds.xmin==-10 && font.bounds.ymin==-20
	    && font.bounds.xmax==-2 && font.bounds.ymax==-4);
	CHECK(st_rip3_outline_font_bind(&font,&file,0,&frame,1000,0,1)==0);
	CHECK(font.bounds.xmin==4 && font.bounds.ymin==-10 && font.bounds.xmax==20 && font.bounds.ymax==-2);
	put16(bytes+file.base+0x16,65536-5);put16(bytes+file.base+0x18,65536-5);
	put16(bytes+file.base+0x1a,5);put16(bytes+file.base+0x1c,5);
	CHECK(st_rip3_outline_font_bind(&font,&file,0,&frame,500,0,0)==0);
	CHECK(font.bounds.xmin==-2 && font.bounds.ymin==-2 && font.bounds.xmax==3 && font.bounds.ymax==3);
	CHECK(st_rip3_frame_world(&frame,32767,32767)==0);
	CHECK(st_rip3_outline_font_bind(&font,&file,0,&frame,1,0,0)==0);
	CHECK(st_rip3_outline_font_bbox_width(&width,&font)==0 && width==0);
	/* Legal custom font units and world extremes exceed int32 DEVICE span.
	 * Font realization retains wide bounds; the narrow layout consumer fails
	 * without wrapping or modifying its prior width. */
	put16(bytes+44,1);put16(bytes+file.base+12,1);
	put16(bytes+file.base+0x16,32768);put16(bytes+file.base+0x18,32768);
	put16(bytes+file.base+0x1a,32767);put16(bytes+file.base+0x1c,32767);
	/* Scale a near-maximum representable DPI without allocating a raster.
	 * The raw edge numerator exceeds int64_t even though the edge fits. */
	CHECK(st_rip3_frame_init_device(&frame,59000000,480)==0);
	CHECK(st_rip3_frame_world(&frame,1,1)==0);
	CHECK(st_rip3_outline_font_bind(&font,&file,0,&frame,4095,0,0)==0);
	CHECK(font.bounds.xmax==INT64_C(32767)*4095*59000000
	    && font.bounds.xmin==-INT64_C(32768)*4095*59000000);
	width=77;CHECK(st_rip3_outline_font_bbox_width(&width,&font)==IO_ERROR_OUT_OF_RANGE && width==77);
	memcpy(&saved,&font,sizeof(saved));
	put16(bytes+file.base+0x16,1);put16(bytes+file.base+0x1a,0);
	CHECK(st_rip3_outline_font_bind(&font,&file,0,&frame,90,0,0)==IO_ERROR_INVALID_FORMAT
	    && memcmp(&font,&saved,sizeof(font))==0);
	font.bounds.xmin=INT64_MIN;font.bounds.xmax=INT64_MAX;
	CHECK(st_rip3_outline_font_bbox_width(&width,&font)==IO_ERROR_OUT_OF_RANGE && width==77);
	font.bounds.xmin=1;font.bounds.xmax=0;
	CHECK(st_rip3_outline_font_bbox_width(&width,&font)==IO_ERROR_OUT_OF_RANGE && width==77);
	CHECK(st_rip3_outline_font_bbox_width(NULL,&font)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_outline_font_bbox_width(&width,NULL)==IO_ERROR_INVALID_ARGUMENT);
	free(bytes);
	return 0;
}

static int
advances(void)
{
	struct st_rip3_frame frame;
	struct st_rip3_outline_font font;
	int32_t values[4]={77,77,77,77};
	CHECK(st_rip3_frame_init(&frame,ST_RIP3_CANVAS_640_480)==0
	    && st_rip3_frame_world(&frame,640,480)==0);
	/* Published unmodified Marin oracle: F is 1001 logical units, then 50
	 * DEVICE units at 90 points/72 DPI. Character rotation does not rotate
	 * the scalar advance; string layout chooses its direction separately. */
	for (unsigned orientation=0; orientation<4; orientation++) {
		CHECK(st_rip3_outline_font_builtin(&font,"Marin",&frame,90,0,orientation)==0);
		CHECK(st_rip3_outline_font_advances(values,&font,0,(const uint8_t *)"FF",2)==0
		    && values[0]==50 && values[1]==50);
	}
	CHECK(st_rip3_outline_font_builtin(&font,"Marin HE",&frame,90,0,0)==0);
	CHECK(st_rip3_outline_font_advances(values,&font,0,(const uint8_t *)"F",1)==0 && values[0]==75);
	CHECK(st_rip3_outline_font_builtin(&font,"Marin Cn",&frame,90,0,0)==0);
	CHECK(st_rip3_outline_font_advances(values,&font,0,(const uint8_t *)"F",1)==0 && values[0]==37);
	CHECK(st_rip3_frame_world(&frame,32,480)==0); /* X=1440, Y=72 DPI. */
	CHECK(st_rip3_outline_font_builtin(&font,"Marin",&frame,90,0,3)==0);
	CHECK(st_rip3_outline_font_advances(values,&font,0,(const uint8_t *)"F",1)==0 && values[0]==1001);
	const struct st_rip3_font_asset *asset=st_rip3_font_asset_at(5);
	uint8_t *bytes=malloc(asset->size);
	struct st_rip3_rff file;
	CHECK(bytes!=NULL);memcpy(bytes,asset->data,asset->size);
	CHECK(st_rip3_rff_open(&file,bytes,asset->size)==0);
	/* Authored advances exercise half-up logical rounding before DEVICE
	 * truncation. 975 units at one point is .975 DEVICE, but 19.5 logical
	 * rounds to 20 first, so its DEVICE result is 1, not zero. */
	put16(bytes+file.advances+2*('A'-file.first_character),0);
	put16(bytes+file.advances+2*('B'-file.first_character),25);
	put16(bytes+file.advances+2*('C'-file.first_character),975);
	put16(bytes+file.advances+2*('D'-file.first_character),65535);
	CHECK(st_rip3_outline_font_bind(&font,&file,0,&frame,1,0,0)==0);
	CHECK(st_rip3_outline_font_advances(values,&font,0,(const uint8_t *)"ABCD",4)==0
	    && values[0]==0 && values[1]==1 && values[2]==20 && values[3]==1311);
	CHECK(st_rip3_frame_world(&frame,640,480)==0);
	CHECK(st_rip3_outline_font_bind(&font,&file,0,&frame,1,0,0)==0);
	CHECK(st_rip3_outline_font_advances(values,&font,0,(const uint8_t *)"ABCD",4)==0
	    && values[0]==0 && values[1]==0 && values[2]==1 && values[3]==65);
	put16(bytes+file.advances+2*('F'-file.first_character),6);
	CHECK(st_rip3_outline_font_bind(&font,&file,0,&frame,90,0,0)==0);
	CHECK(st_rip3_outline_font_advances(values,&font,0,(const uint8_t *)"FF",2)==0
	    && values[0]==0 && values[1]==0); /* Do not scale the string sum. */
	/* Raw high bytes are unsigned font codes; missing codes are zero, while
	 * damaged glyph records keep their declared advance without needing ink. */
	const uint8_t sparse[]={0,31,128,'F'};
	put16(bytes+file.advances+2*(128-file.first_character),1234);
	memset(bytes+file.offsets+4*('F'-file.first_character),255,4);
	CHECK(st_rip3_outline_font_bind(&font,&file,0,&frame,1000,0,0)==0);
	CHECK(st_rip3_outline_font_advances(values,&font,0,sparse,4)==0
	    && values[0]==0 && values[1]==0 && values[2]==1234 && values[3]==6);
	/* This is an unkerned service: raw pair-table data does not change it. */
	put16(bytes+file.advances+2*('A'-file.first_character),600);
	put16(bytes+file.advances+2*('V'-file.first_character),500);
	CHECK(st_rip3_outline_font_advances(values,&font,0,(const uint8_t *)"AV",2)==0
	    && values[0]==600 && values[1]==500);
	uint8_t long_text[512];
	int32_t long_advances[512];
	memset(long_text,'F',sizeof(long_text));
	CHECK(st_rip3_outline_font_advances(long_advances,&font,0,long_text,sizeof(long_text))==0);
	for (size_t i=0; i<512; i++) CHECK(long_advances[i]==6);
	CHECK(st_rip3_outline_font_advances(NULL,&font,0,NULL,0)==0);
	for (unsigned which=0; which<8; which++) {
		struct st_rip3_outline_font bad=font;
		switch (which) {
			case 0:bad.points=0;break; case 1:bad.points=4096;break;
			case 2:bad.dpi_x=0;break; case 3:bad.dpi_y=0;break;
			case 4:bad.orientation=4;break; case 5:bad.flags=0x1000000;break;
			case 6:bad.face=10;break; case 7:bad.file.size=1;break;
		}
		values[0]=77;values[1]=88;
		CHECK(st_rip3_outline_font_advances(values,&bad,0,(const uint8_t *)"AV",2)!=0
		    && values[0]==77 && values[1]==88);
	}
	/* The last byte's oversized custom-unit metric must not publish the
	 * first byte's valid result. All intermediate arithmetic is wide. */
	put16(bytes+44,1);put16(bytes+file.base+12,1);
	put16(bytes+file.advances+2*('A'-file.first_character),0);
	CHECK(st_rip3_frame_world(&frame,1,1)==0);
	CHECK(st_rip3_outline_font_bind(&font,&file,0,&frame,4095,0,0)==0);
	values[0]=77;values[1]=88;
	CHECK(st_rip3_outline_font_advances(values,&font,0,(const uint8_t *)"AD",2)==IO_ERROR_OUT_OF_RANGE
	    && values[0]==77 && values[1]==88);
	/* C fits logical int32, but its DEVICE conversion exceeds int32. */
	CHECK(st_rip3_outline_font_advances(values,&font,0,(const uint8_t *)"AC",2)==IO_ERROR_OUT_OF_RANGE
	    && values[0]==77 && values[1]==88);
	/* Logical advance 5,367,316,500 is legal and must not narrow to int32
	 * before conversion. At one DPI the DEVICE result fits the layout API. */
	CHECK(st_rip3_frame_world(&frame,32767,32767)==0);
	CHECK(st_rip3_outline_font_bind(&font,&file,0,&frame,4095,0,0)==0);
	CHECK(st_rip3_outline_font_advances(values,&font,0,(const uint8_t *)"AD",2)==0
	    && values[0]==0 && values[1]==3727303);
	values[0]=77;
	CHECK(st_rip3_outline_font_advances(values,&font,1,(const uint8_t *)"A",1)==IO_ERROR_OUT_OF_RANGE && values[0]==77);
	CHECK(st_rip3_outline_font_advances(NULL,&font,0,(const uint8_t *)"A",1)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_outline_font_advances(values,NULL,0,(const uint8_t *)"A",1)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_outline_font_advances(values,&font,0,NULL,1)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_outline_font_advances(values,&font,0,(const uint8_t *)"A",SIZE_MAX)==IO_ERROR_OUT_OF_RANGE);
	free(bytes);
	return 0;
}

static int
advance_policy(void)
{
	struct st_rip3_frame frame;
	struct st_rip3_outline_font font;
	int32_t values[2];
	static const struct {const char *name;int32_t horizontal,vertical;} cases[]={
		{"Marin",50,25},{"Marin HE",75,37},
	};
	CHECK(st_rip3_frame_init(&frame,ST_RIP3_CANVAS_640_480)==0
	    && st_rip3_frame_world(&frame,640,960)==0); /* Dx=72, Dy=36. */
	for (size_t face=0; face<2; face++) for (unsigned effects=0; effects<16; effects++)
	for (unsigned character=0; character<4; character++) for (unsigned string=0; string<4; string++) {
		CHECK(st_rip3_outline_font_builtin(&font,cases[face].name,&frame,90,effects,character)==0);
		CHECK(st_rip3_outline_font_advances(values,&font,900*string,(const uint8_t *)"F",1)==0
		    && values[0]==(string%2==0 ? cases[face].horizontal : cases[face].vertical));
	}
	const uint8_t high[]={0x80,0xff};
	CHECK(st_rip3_outline_font_builtin(&font,"Marin",&frame,90,0,0)==0);
	CHECK(st_rip3_outline_font_advances(values,&font,0,high,sizeof(high))==0 && values[0]==15 && values[1]==90);
	CHECK(st_rip3_frame_world(&frame,1280,960)==0);
	CHECK(st_rip3_outline_font_builtin(&font,"Marin",&frame,10,0,0)==0);
	CHECK(st_rip3_outline_font_advances(values,&font,0,(const uint8_t *)"FF",2)==0
	    && values[0]==2 && values[1]==2); /* Whole-string conversion would yield 5. */
	return 0;
}

static int
column_metrics(void)
{
	static const struct {
		const char *face;
		unsigned points, world_height;
		int64_t baseline, fit, advance;
	} cases[] = {
		{"Dixon", 22, 480, 16, 22, 55},
		{"Dixon", 22, 350, 21, 28, 73},
		{"Dixon Wd", 48, 480, 34, 45, 119},
		{"Marin", 22, 480, 15, 21, 54},
		{"DEFAULT", 22, 480, 14, 18, 53},
		{"Symbol", 22, 480, 14, 22, 64},
	};
	struct st_rip3_frame frame;
	CHECK(st_rip3_frame_init(&frame, ST_RIP3_CANVAS_640_480)==0);
	for (size_t i=0; i<sizeof(cases)/sizeof(*cases); ++i) {
		struct st_rip3_outline_font font;
		CHECK(st_rip3_frame_world(&frame, 640, cases[i].world_height)==0);
		CHECK(st_rip3_outline_font_builtin(&font, cases[i].face, &frame, cases[i].points, 0, 0)==0);
		struct st_rip3_outline_column_metrics metrics=st_rip3_outline_font_column_metrics(&font);
		CHECK(metrics.baseline==cases[i].baseline && metrics.fit==cases[i].fit && metrics.advance==cases[i].advance);
	}
	return 0;
}

int
main(void)
{
	CHECK(catalogue()==0 && sizes()==0 && coefficients()==0 && errors()==0 && bounds()==0 && advances()==0 && advance_policy()==0);
	CHECK(column_metrics()==0);
	return 0;
}

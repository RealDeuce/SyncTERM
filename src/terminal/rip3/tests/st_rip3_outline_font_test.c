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
			case 5:bad.world_height=32768;break;default:bad.device_width=641;break;
		}
		CHECK(st_rip3_outline_font_builtin(&font,"Marin",&bad,size,flags,orientation)!=0 && memcmp(&font,&saved,sizeof(font))==0);
	}
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

int
main(void)
{
	CHECK(catalogue()==0 && sizes()==0 && coefficients()==0 && errors()==0);
	return 0;
}

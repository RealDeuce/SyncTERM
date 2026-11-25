/* Authored rectangular glyphs and independent ordinary-outline state vectors. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_commands.h"
#include "st_rip3_text_commands.h"
#include "st_rip3_outline_cache.h"
#include "st_rip3_rff.h"
#include "st_rip3_styles.h"
#include "st_rip3_environments.h"
#include "st_rip3_ports.h"
#include "st_rip3_geometry.h"
#include "st_rip3_palettes.h"
#include "st_rip3_bitmap.h"
#include "st_rip3_stream.h"
#include "st_rip3_number.h"
#include "st_pixels.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1; } } while (0)
extern bool st_rip3_text_commands_test_fail_allocate;
extern size_t st_rip3_outline_test_allocations,st_rip3_outline_test_fail_at;

struct fixture {
	struct st_rip3_command_host host;
	struct st_rip3_text_context text;
	struct st_rip3_rff font;
	uint8_t bytes[2000];
	unsigned errors;
	enum st_rip3_command_diagnostic kind;
};

static void
word(uint8_t *p, unsigned n)
{
	p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);
}

static void
dword(uint8_t *p, uint32_t n)
{
	word(p,n&65535);word(p+2,n>>16);
}

static void
diagnostic(void *context, enum st_rip3_command_diagnostic kind)
{
	struct fixture *f=context;f->errors++;f->kind=kind;
}

static void
command(void *context, const uint8_t *bytes, size_t length)
{
	struct fixture *f=context;const struct st_rip3_command_sink sink={f,diagnostic};
	st_rip3_commands_process(&f->host,&sink,bytes,length);
}

static int
process(struct fixture *f, const char *body, int error)
{
	unsigned before=f->errors;command(f,(const uint8_t *)body,strlen(body));
	if (f->errors!=before+(error<0 ? 0u : 1u) || (error>=0 && (int)f->kind!=error))
		fprintf(stderr,"body %s: errors %u kind %d expected %d\n",body,f->errors-before,(int)f->kind,error);
	CHECK(f->errors==before+(error<0 ? 0u : 1u) && (error<0 || (int)f->kind==error));return 0;
}

static int
select_font(struct fixture *f, const char *name, unsigned points, unsigned s, unsigned r,
    uint32_t flags, unsigned spacing, unsigned baseline, unsigned shadow)
{
	static const size_t widths[]={1,1,4,2,2,2,2,2,2,2,6};
	const uint64_t values[]={0,0,flags,points,shadow,s*900,r*900,baseline,spacing,0,0};
	char body[100]={'y'};size_t at=1;
	for (size_t i=0; i<sizeof(widths)/sizeof(widths[0]); i++) {
		CHECK(st_rip3_number_encode(body+at,sizeof(body)-at,values[i],widths[i],64)==0);at+=widths[i];
	}
	CHECK(at+strlen(name)<sizeof(body));memcpy(body+at,name,strlen(name)+1);
	CHECK(st_rip3_styles_protect(f->host.styles,3,false)==0 && process(f,body,-1)==0
	    && st_rip3_styles_protect(f->host.styles,3,true)==0);return 0;
}

static int
xy(struct fixture *f, unsigned x, unsigned y, const char *text, int error)
{
	char body[900]={'@'};
	CHECK(strlen(text)<sizeof(body)-5);
	CHECK(st_rip3_number_encode(body+1,2,x,2,64)==0 && st_rip3_number_encode(body+3,2,y,2,64)==0);
	memcpy(body+5,text,strlen(text)+1);return process(f,body,error);
}

static int
cursor(struct fixture *f, int64_t x, int64_t y)
{
	struct st_rip3_port_info info;CHECK(st_rip3_ports_info(f->host.ports,1,&info)==0);
	if (info.cursor.x!=x || info.cursor.y!=y) fprintf(stderr,"cursor %lld,%lld expected %lld,%lld\n",
	    (long long)info.cursor.x,(long long)info.cursor.y,(long long)x,(long long)y);
	CHECK(info.cursor.x==x && info.cursor.y==y);return 0;
}

static int
create(struct fixture *f, enum st_rip3_canvas canvas)
{
	/* One declared style, all 256 raw slots. A/B advance 5/7; every ink glyph
	 * is the half-open design rectangle [0,3) x [-4,0). Global bounds are
	 * deliberately larger and asymmetric; they must never relocate the anchor. */
	static const uint8_t signature[]={0x10,0,2,2,0x2e,0,0x36,0,2,4,0x98,0x44};
	static const uint8_t path[]={0,0,0,252,255,1,3,0,252,255,1,3,0,0,0,1,0,0,0,0};
	uint8_t *b=f->bytes;memcpy(b+16,signature,sizeof(signature));
	word(b+36,256);word(b+42,1);word(b+44,72);word(b+52,0x5426);
	b[57]=100;memcpy(b+66,"Fixture",8);word(b+100,1);word(b+102,112);
	word(b+112,0xf31e);word(b+114,0x100);word(b+124,72);word(b+128,256);
	word(b+134,65536-10);word(b+136,65536-20);word(b+138,20);word(b+140,20);
	word(b+150,76);word(b+158,588);
	for (unsigned i=0; i<256; i++) {word(b+188+2*i,5);dword(b+700+4*i,1740-112);}
	word(b+188+2*'B',7);word(b+188+2*' ',3);word(b+188+2*0x82,6);
	dword(b+700+4*' ',1780-112);word(b+1740,1);word(b+1744,65536-4);word(b+1746,3);
	word(b+1750,sizeof(path));memcpy(b+1754,path,sizeof(path));word(b+1780,1);
	CHECK(st_rip3_rff_open(&f->font,b,sizeof(f->bytes))==0);
	struct st_rip3_style initial;CHECK(st_rip3_style_defaults(&initial)==0);
	initial.drawing_color=12;initial.background_color=8;
	CHECK(st_rip3_styles_create(&f->host.styles,&initial)==0
	    && st_rip3_styles_define(f->host.styles,3,&initial)==0 && st_rip3_styles_switch(f->host.styles,3)==0);
	CHECK(st_rip3_environments_create(&f->host.environments,canvas)==0 && st_rip3_ports_create(&f->host.ports,canvas)==0
	    && st_rip3_palettes_create(&f->host.palettes)==0);
	struct st_rip3_frame frame;struct st_rip3_environment env;
	CHECK(st_rip3_frame_init(&frame,canvas)==0 && st_rip3_environments_get(f->host.environments,0,&env)==0);
	env.base_math=64;env.world_width=frame.device_width;env.world_height=frame.device_height;
	CHECK(st_rip3_environments_set(f->host.environments,0,&env)==0);
	const struct st_rip3_rect bounds={3,4,102,103},view={2,3,81,72};
	CHECK(st_rip3_ports_define(f->host.ports,1,&bounds,false)==0 && st_rip3_ports_switch(f->host.ports,1)==0
	    && st_rip3_ports_viewport(f->host.ports,&view)==0 && st_rip3_ports_erase(f->host.ports,85)==0
	    && st_rip3_ports_protect(f->host.ports,1,true)==0);
	f->text.encoding=ST_RIP3_TEXT_CP437;f->text.outline_font=&f->font;f->host.text=&f->text;
	return select_font(f,"fixture",72,0,0,0,100,0,0);
}

static void
destroy(struct fixture *f)
{
	st_rip3_outline_cache_destroy(f->text.outline_cache);st_rip3_styles_destroy(f->host.styles);
	st_rip3_environments_destroy(f->host.environments);st_rip3_ports_destroy(f->host.ports);st_rip3_palettes_destroy(f->host.palettes);
}

static int
capture(struct fixture *f, const struct st_rip3_point origins[2], unsigned r, uint8_t ink, bool blank)
{
	static const int rectangles[][4]={{0,-4,3,0},{-4,-3,0,0},{-3,0,0,4},{0,0,4,3}};
	struct st_rip3_palette palette;struct st_rip3_bitmap *image=NULL;struct st_rip3_port_info info;
	CHECK(st_rip3_palettes_get(f->host.palettes,0,&palette)==0 && st_rip3_bitmap_capture(&image,f->host.ports,1,NULL,&palette)==0
	    && st_rip3_ports_info(f->host.ports,1,&info)==0);
	CHECK(image->pixels->width==80 && image->pixels->height==70);
	for (int y=0; y<70; y++) for (int x=0; x<80; x++) {
		bool hit=false;
		if (!blank) for (unsigned i=0; i<2; i++)
			hit|=x>=origins[i].x+rectangles[r][0] && x<origins[i].x+rectangles[r][2]
			    && y>=origins[i].y+rectangles[r][1] && y<origins[i].y+rectangles[r][3];
		CHECK(image->pixels->primary[(size_t)y*80+(size_t)x]==(hit ? ink : 85));
	}
	CHECK(info.viewport_active);st_rip3_bitmap_destroy(image);return 0;
}

static int
matrix(enum st_rip3_canvas canvas)
{
	static const int direction[][2]={{1,0},{0,-1},{-1,0},{0,1}};
	static const int baseline[][4][2]={{{0,3},{3,0},{0,0},{0,0}},{{3,0},{3,0},{0,0},{0,0}},
	    {{0,0},{3,0},{0,-3},{0,0}},{{0,3},{0,0},{0,0},{0,0}}};
	static const uint8_t ink[]={12,89,93,4,170};
	struct fixture f={0};CHECK(create(&f,canvas)==0);
	for (unsigned s=0; s<4; s++) for (unsigned r=0; r<4; r++) for (unsigned j=0; j<4; j++)
		for (unsigned negative=0; negative<2; negative++) for (unsigned mode=0; mode<5; mode++) {
			uint32_t flags=(j<<4)|(negative ? 0x500 : 0);
			CHECK(select_font(&f,"FIXTURE",72,s,r,flags,negative ? 10 : 100,3,0)==0);
			struct st_rip3_style style;CHECK(st_rip3_styles_get(f.host.styles,3,&style)==0);
			style.write_mode=(enum st_rip3_write_mode)mode;
			CHECK(st_rip3_styles_protect(f.host.styles,3,false)==0 && st_rip3_styles_set(f.host.styles,3,&style)==0
			    && st_rip3_styles_protect(f.host.styles,3,true)==0 && st_rip3_ports_erase(f.host.ports,85)==0);
			int total=negative ? (r%2 ? -60 : -42) : 12;
			int align=j==1 ? total : j==2 ? total/2 : 0,sign=negative ? -1 : 1;
			int first=negative ? (r%2 ? -31 : -22) : 5;
			struct st_rip3_point origins[2]={{40-direction[s][0]*align+sign*baseline[s][r][0],30-direction[s][1]*align+sign*baseline[s][r][1]},{0,0}};
			origins[1].x=origins[0].x+direction[s][0]*first;origins[1].y=origins[0].y+direction[s][1]*first;
			CHECK(xy(&f,40,30,"AB",-1)==0 && cursor(&f,45+total,37)==0
			    && capture(&f,origins,r,ink[mode],false)==0);
		}
	destroy(&f);return 0;
}

static int
lifecycle(enum st_rip3_canvas canvas)
{
	struct fixture f={0};CHECK(create(&f,canvas)==0);
	CHECK(select_font(&f,"missing",90,0,0,0,100,0,0)==0 && f.text.outline_cache==NULL);
	CHECK(xy(&f,100,80,"F",ST_RIP3_COMMAND_OPERATION_FAILED)==0 && cursor(&f,105,87)==0 && f.text.outline_cache==NULL);
	CHECK(select_font(&f,"Marin",90,0,0,0,100,0,0)==0 && xy(&f,100,80,"F",-1)==0
	    && cursor(&f,155,87)==0 && st_rip3_outline_cache_valid(f.text.outline_cache));
	struct st_rip3_environment env;CHECK(st_rip3_environments_get(f.host.environments,0,&env)==0);
	env.world_width*=2;env.world_height*=2;CHECK(st_rip3_environments_set(f.host.environments,0,&env)==0);
	CHECK(select_font(&f,"missing",90,3,1,0x20,125,0,0)==0
	    && xy(&f,100,80,"F",ST_RIP3_COMMAND_OPERATION_FAILED)==0 && cursor(&f,105,47)==0);
	CHECK(st_rip3_styles_protect(f.host.styles,3,false)==0 && process(&f,"Y00000100",-1)==0);
	CHECK(xy(&f,100,80,"F",-1)==0 && cursor(&f,155,87)==0);
	CHECK(select_font(&f,"Marin",90,1,0,0x20,100,0,0)==0 && xy(&f,100,80,"F",-1)==0 && cursor(&f,80,47)==0);
	CHECK(st_rip3_styles_protect(f.host.styles,3,false)==0 && process(&f,"Y00000100",-1)==0
	    && xy(&f,100,80,"F",-1)==0 && cursor(&f,130,87)==0);
	CHECK(select_font(&f,"Marin",90,0,0,0,100,0,0)==0);
	char long_text[702]={'T'};memset(long_text+1,'F',700);long_text[701]=0;
	struct st_rip3_cursor start={300,400};CHECK(st_rip3_ports_cursor(f.host.ports,1,&start)==0);
	CHECK(process(&f,long_text,-1)==0 && cursor(&f,17800,400)==0); /* Dx=36: 700*25. */
	env.world_width/=2;env.world_height/=2;CHECK(st_rip3_environments_set(f.host.environments,0,&env)==0);
	CHECK(st_rip3_ports_cursor(f.host.ports,1,&start)==0 && process(&f,long_text,-1)==0 && cursor(&f,-30236,400)==0);
	st_rip3_outline_cache_reset(f.text.outline_cache);
	CHECK(select_font(&f,"missing",90,0,0,0,100,0,0)==0 && process(&f,"TF",ST_RIP3_COMMAND_OPERATION_FAILED)==0
	    && cursor(&f,-30236,400)==0 && !st_rip3_outline_cache_valid(f.text.outline_cache));
	destroy(&f);return 0;
}

static int
anisotropic(void)
{
	struct fixture f={0};CHECK(create(&f,ST_RIP3_CANVAS_640_480)==0);
	struct st_rip3_environment env;CHECK(st_rip3_environments_get(f.host.environments,0,&env)==0);
	env.world_width=320;CHECK(st_rip3_environments_set(f.host.environments,0,&env)==0);
	CHECK(select_font(&f,"Fixture",72,1,1,0x10,100,0,0)==0 && xy(&f,20,30,"AB",-1)==0 && cursor(&f,69,37)==0);
	/* Dy=72 gives ordinary vertical steps 5/7 and end offset +12; Dx=144
	 * gives cached X width 24. Glyph rotation precedes the device-axis scales. */
	struct st_rip3_palette palette;struct st_rip3_bitmap *image=NULL;
	CHECK(st_rip3_palettes_get(f.host.palettes,0,&palette)==0 && st_rip3_bitmap_capture(&image,f.host.ports,1,NULL,&palette)==0);
	for (int y=0; y<70; y++) for (int x=0; x<80; x++) {
		bool hit=x>=32 && x<40 && ((y>=39 && y<42) || (y>=34 && y<37));
		CHECK(image->pixels->primary[(size_t)y*80+(size_t)x]==(hit ? 12 : 85));
	}
	st_rip3_bitmap_destroy(image);destroy(&f);return 0;
}

static int
real_faces(void)
{
	/* Independent original MARIN metrics: F=556/U=1000, Q=90, D=72.
	 * The suffix percentages are 100/75/125/150; only the last is hollow. */
	static const struct {const char *name;int width;} faces[]={
		{"Marin",50},{"Marin Cn",37},{"Marin Wd",62},{"Marin HE",75}};
	struct fixture f={0};CHECK(create(&f,ST_RIP3_CANVAS_640_480)==0);
	for (size_t i=0; i<sizeof(faces)/sizeof(faces[0]); i++) {
		CHECK(select_font(&f,faces[i].name,90,0,0,0,100,0,0)==0 && st_rip3_ports_erase(f.host.ports,85)==0
		    && xy(&f,20,50,"F",-1)==0 && cursor(&f,25+faces[i].width,57)==0);
		struct st_rip3_palette palette;struct st_rip3_bitmap *image=NULL;
		CHECK(st_rip3_palettes_get(f.host.palettes,0,&palette)==0 && st_rip3_bitmap_capture(&image,f.host.ports,1,NULL,&palette)==0);
		bool ink=false;
		for (size_t pixel=0; pixel<80*70; pixel++) ink|=image->pixels->primary[pixel]==12;
		CHECK(ink);st_rip3_bitmap_destroy(image);
	}
	destroy(&f);return 0;
}

static int
shadows_and_rgb(void)
{
	struct fixture f={0};CHECK(create(&f,ST_RIP3_CANVAS_800_600)==0);
	for (unsigned which=0; which<4; which++) {
		CHECK(select_font(&f,"Fixture",72,0,0,which<<6,100,0,2)==0 && st_rip3_ports_erase(f.host.ports,85)==0
		    && xy(&f,40,30,"A",-1)==0 && cursor(&f,50,37)==0);
		struct st_rip3_palette palette;struct st_rip3_bitmap *image=NULL;
		CHECK(st_rip3_palettes_get(f.host.palettes,0,&palette)==0 && st_rip3_bitmap_capture(&image,f.host.ports,1,NULL,&palette)==0);
		int dx=(which&1) ? 2 : -2,dy=(which&2) ? 2 : -2;
		for (int y=0; y<70; y++) for (int x=0; x<80; x++) {
			bool fg=x>=40 && x<43 && y>=26 && y<30;
			bool bg=x>=40+dx && x<43+dx && y>=26+dy && y<30+dy;
			CHECK(image->pixels->primary[(size_t)y*80+(size_t)x]==(fg ? 12 : bg ? 8 : 85));
		}
		st_rip3_bitmap_destroy(image);
	}
	struct st_rip3_environment env;struct st_rip3_style style;
	CHECK(st_rip3_environments_get(f.host.environments,0,&env)==0 && st_rip3_styles_get(f.host.styles,3,&style)==0);
	env.rgb_bits=8;style.drawing_color=0x0c0c0c;style.background_color=0x080808;style.outline.shadow=0;
	uint32_t colors[256];for (unsigned i=0; i<256; i++) colors[i]=i*UINT32_C(0x010101);
	CHECK(st_rip3_palettes_rgb(f.host.palettes,0,8,colors,256)==0 && st_rip3_environments_set(f.host.environments,0,&env)==0
	    && st_rip3_styles_protect(f.host.styles,3,false)==0 && st_rip3_styles_set(f.host.styles,3,&style)==0
	    && st_rip3_ports_erase(f.host.ports,85)==0 && xy(&f,40,30,"A",-1)==0);
	const struct st_rip3_point origins[2]={{40,30},{40,30}};
	CHECK(capture(&f,origins,0,12,false)==0);
	/* Drawing NOT needs neither foreground nor an unused shadow palette. */
	style.write_mode=ST_RIP3_NOT;CHECK(st_rip3_styles_set(f.host.styles,3,&style)==0 && st_rip3_ports_erase(f.host.ports,85)==0);
	struct st_rip3_palettes *palettes=f.host.palettes;f.host.palettes=NULL;
	CHECK(xy(&f,40,30,"A",-1)==0);f.host.palettes=palettes;
	CHECK(capture(&f,origins,0,170,false)==0);
	destroy(&f);return 0;
}

static int
profile_quad(double x, double y, const struct st_rip3_point *origin,
    unsigned rotation, unsigned sy, bool italic, double width, double top, double bottom)
{
	/* Authored rectangle/sheared parallelogram, sampled at pixel centers.
	 * The four quarter-turn equations are independent expected geometry. */
	double px[4]={0,width,width,0},py[4]={top,top,bottom,bottom};
	for (unsigned i=0; i<4; i++) {
		if (italic) px[i]-=py[i]/4;
		double u=px[i],v=py[i];
		switch (rotation) {
			case 0:break;case 1:px[i]=v;py[i]=-u;break;
			case 2:px[i]=-u;py[i]=-v;break;default:px[i]=-v;py[i]=u;break;
		}
		px[i]+=origin->x;py[i]=origin->y+py[i]*sy;
	}
	bool inside=false;
	for (unsigned i=0,j=3; i<4; j=i++) {
		if ((py[i]>y)!=(py[j]>y) && x<(px[j]-px[i])*(y-py[i])/(py[j]-py[i])+px[i]) inside=!inside;
	}
	return inside;
}

static bool
profile_ink(double x, double y, const struct st_rip3_point origins[2], unsigned r, unsigned sy, unsigned flags)
{
	bool italic=(flags&2)!=0,hit=false;
	for (int dx=(flags&1)!=0 ? -1 : 0; dx<=((flags&1)!=0 ? 1 : 0); dx++)
		hit|=profile_quad(x+dx,y,&origins[0],r,sy,italic,3,-4,0)!=0;
	for (unsigned i=0; i<2; i++) {
		if ((flags&4)!=0) hit|=profile_quad(x,y,&origins[i],r,sy,italic,i==0 ? 5 : 3,0,1)!=0;
		if ((flags&8)!=0) hit|=profile_quad(x,y,&origins[i],r,sy,italic,i==0 ? 5 : 3,-2,-1)!=0;
	}
	return hit;
}

static int
effects(enum st_rip3_canvas canvas)
{
	static const int direction[][2]={{1,0},{0,-1},{-1,0},{0,1}};
	static const uint8_t foreground[]={3,6,7,1,250},background[]={8,13,13,0,250};
	struct fixture f={0};CHECK(create(&f,canvas)==0);
	/* Keep the same authored contours/advances, with one font unit per DEVICE
	 * pixel. U=8 gives one-unit decoration bands at y=0 and y=-2. */
	word(f.bytes+44,8);word(f.bytes+124,8);CHECK(st_rip3_rff_open(&f.font,f.bytes,sizeof(f.bytes))==0);
	for (unsigned sy=1; sy<=2; sy++) {
		struct st_rip3_environment env;CHECK(st_rip3_environments_get(f.host.environments,0,&env)==0);
		struct st_rip3_frame frame;CHECK(st_rip3_environments_frame(f.host.environments,0,&frame)==0);
		env.world_height=frame.device_height/sy;CHECK(st_rip3_environments_set(f.host.environments,0,&env)==0);
		for (unsigned s=0; s<4; s++) for (unsigned r=0; r<4; r++)
		for (unsigned flags=0; flags<16; flags++) for (unsigned gap=0; gap<2; gap++) for (unsigned mode=0; mode<5; mode++) {
			unsigned bbox=(r&1)!=0 ? 40 : 30,which=(s+r)%4;
			int step=(int)(5*((s&1)!=0 ? sy : 1))-(gap!=0 ? (int)(bbox*9/10) : 0);
			const struct st_rip3_point origins[]={{40,30},{40+direction[s][0]*step,30+direction[s][1]*step}};
			CHECK(select_font(&f,"Fixture",8,s,r,flags|(gap!=0 ? 0x400 : 0)|(which<<6),gap!=0 ? 10 : 100,0,gap!=0 ? 3 : 0)==0);
			struct st_rip3_style style;CHECK(st_rip3_styles_get(f.host.styles,3,&style)==0);
			style.drawing_color=3;style.fill_color=14;style.line_type=4;
			style.pen.pattern=0xaaaa;style.pen.thickness=9;style.pen.draw_background=true;style.borders=false;
			CHECK(st_rip3_fill_solid(&style.fill,false)==0);style.write_mode=(enum st_rip3_write_mode)mode;
			CHECK(st_rip3_styles_protect(f.host.styles,3,false)==0 && st_rip3_styles_set(f.host.styles,3,&style)==0
			    && st_rip3_styles_protect(f.host.styles,3,true)==0 && st_rip3_ports_erase(f.host.ports,5)==0
			    && xy(&f,40,30/sy,"A ",-1)==0);
			struct st_rip3_palette palette;struct st_rip3_bitmap *image=NULL;struct st_rip3_port_info info;
			CHECK(st_rip3_palettes_get(f.host.palettes,0,&palette)==0 && st_rip3_bitmap_capture(&image,f.host.ports,1,NULL,&palette)==0
			    && st_rip3_ports_info(f.host.ports,1,&info)==0 && info.cursor.x==45+8-(gap!=0 ? 2*(int)(bbox*9/10) : 0) && info.cursor.y==37);
			int shadow_x=(which&1)!=0 ? 3 : -3,shadow_y=(which&2)!=0 ? 3 : -3;
			for (int y=0; y<70; y++) for (int x=0; x<80; x++) {
				bool hit=profile_ink(x+.5,y+.5,origins,r,sy,flags);
				bool shade=gap!=0 && profile_ink(x+.5-shadow_x,y+.5-shadow_y,origins,r,sy,flags);
				uint32_t expected=hit ? foreground[mode] : shade ? background[mode] : 5;
				if (image->pixels->primary[(size_t)y*80+(size_t)x]!=expected) {
					fprintf(stderr,"effect scene canvas%u sy%u S%u R%u flags%u gap%u W%u at %d,%d\n",(unsigned)canvas,sy,s,r,flags,gap,mode,x,y);
					st_rip3_bitmap_destroy(image);destroy(&f);return 1;
				}
			}
			st_rip3_bitmap_destroy(image);
		}
	}
	/* The same accented byte reaches effected drawing through CP437 and
	 * UTF-8. q accumulates the four low flags before rendering. */
	struct st_rip3_environment env;struct st_rip3_frame frame;
	CHECK(st_rip3_environments_get(f.host.environments,0,&env)==0
	    && st_rip3_environments_frame(f.host.environments,0,&frame)==0);
	env.world_height=frame.device_height;CHECK(st_rip3_environments_set(f.host.environments,0,&env)==0);
	for (unsigned utf8=0; utf8<2; utf8++) {
		CHECK(select_font(&f,"Fixture",8,0,0,0,100,0,0)==0 && st_rip3_styles_protect(f.host.styles,3,false)==0
		    && process(&f,"q01",-1)==0 && process(&f,"q02",-1)==0 && process(&f,"q04",-1)==0 && process(&f,"q08",-1)==0);
		struct st_rip3_style style;CHECK(st_rip3_styles_get(f.host.styles,3,&style)==0 && style.outline.flags==15);
		style.write_mode=ST_RIP3_XOR;CHECK(st_rip3_styles_set(f.host.styles,3,&style)==0
		    && st_rip3_styles_protect(f.host.styles,3,true)==0 && st_rip3_ports_erase(f.host.ports,5)==0);
		f.text.encoding=utf8!=0 ? ST_RIP3_TEXT_UTF8 : ST_RIP3_TEXT_CP437;
		CHECK(xy(&f,40,30,utf8!=0 ? "\xc3\xa9" : "\x82",-1)==0 && cursor(&f,51,37)==0);
		struct st_rip3_palette palette;struct st_rip3_bitmap *image=NULL;const struct st_rip3_point origin={40,30};
		CHECK(st_rip3_palettes_get(f.host.palettes,0,&palette)==0 && st_rip3_bitmap_capture(&image,f.host.ports,1,NULL,&palette)==0);
		for (int y=0; y<70; y++) for (int x=0; x<80; x++) {
			bool hit=profile_quad(x+.5,y+.5,&origin,0,1,true,6,0,1)!=0
			    || profile_quad(x+.5,y+.5,&origin,0,1,true,6,-2,-1)!=0;
			for (int dx=-1; dx<=1; dx++) hit|=profile_quad(x+.5+dx,y+.5,&origin,0,1,true,3,-4,0)!=0;
			CHECK(image->pixels->primary[(size_t)y*80+(size_t)x]==(hit ? 6u : 5u));
		}
		st_rip3_bitmap_destroy(image);
	}
	destroy(&f);return 0;
}

static int
errors_and_encoding(void)
{
	struct fixture f={0};CHECK(create(&f,ST_RIP3_CANVAS_640_480)==0);
	const struct st_rip3_point origin[2]={{40,30},{40,30}};
	const struct st_rip3_cursor start={45,37};
	const struct st_rip3_point pair[2]={{40,30},{45,30}};
	CHECK(st_rip3_ports_cursor(f.host.ports,1,&start)==0 && process(&f,"TAB",-1)==0
	    && cursor(&f,57,37)==0 && capture(&f,pair,0,12,false)==0 && st_rip3_ports_erase(f.host.ports,85)==0);
	CHECK(xy(&f,40,30," ",-1)==0 && cursor(&f,48,37)==0 && capture(&f,origin,0,12,true)==0);
	CHECK(xy(&f,40,30,"\x82",-1)==0 && cursor(&f,51,37)==0 && capture(&f,origin,0,12,false)==0);
	f.text.encoding=ST_RIP3_TEXT_UTF8;
	CHECK(st_rip3_ports_erase(f.host.ports,85)==0 && xy(&f,40,30,"\xc3\xa9",-1)==0
	    && cursor(&f,51,37)==0 && capture(&f,origin,0,12,false)==0);
	f.text.encoding=ST_RIP3_TEXT_CP437;
	CHECK(st_rip3_ports_erase(f.host.ports,85)==0);
	st_rip3_text_commands_test_fail_allocate=true;
	CHECK(xy(&f,40,30,"A",ST_RIP3_COMMAND_RESOURCE_FAILURE)==0 && cursor(&f,50,37)==0
	    && st_rip3_outline_cache_valid(f.text.outline_cache) && capture(&f,origin,0,12,true)==0);
	st_rip3_outline_test_allocations=0;st_rip3_outline_test_fail_at=0;
	CHECK(xy(&f,40,30,"A",ST_RIP3_COMMAND_RESOURCE_FAILURE)==0 && cursor(&f,50,37)==0 && capture(&f,origin,0,12,true)==0);
	st_rip3_outline_test_fail_at=SIZE_MAX;
	CHECK(select_font(&f,"Fixture",72,0,0,1,100,0,0)==0 && xy(&f,40,30,"A",-1)==0
	    && cursor(&f,50,37)==0 && st_rip3_ports_erase(f.host.ports,85)==0);
	CHECK(select_font(&f,"Fixture",72,0,0,0,100,0,0)==0);
	f.bytes[16]=0;
	CHECK(xy(&f,40,30,"A",ST_RIP3_COMMAND_OPERATION_FAILED)==0 && cursor(&f,50,37)==0);
	f.bytes[16]=0x10;
	CHECK(process(&f,"T$RESX$",ST_RIP3_COMMAND_UNSUPPORTED)==0 && cursor(&f,50,37)==0);
	f.text.encoding=ST_RIP3_TEXT_UTF8;
	CHECK(process(&f,"T\xc3",ST_RIP3_COMMAND_INVALID)==0 && cursor(&f,50,37)==0);
	CHECK(process(&f,"T",-1)==0 && xy(&f,0,0,"",ST_RIP3_COMMAND_INVALID)==0 && cursor(&f,50,37)==0);
	/* Following valid split input recovers without a terminal/session error. */
	struct st_rip3_stream *stream=NULL;const struct st_rip3_stream_sink sink={.context=&f,.command=command, .diagnostic=diagnostic};
	CHECK(st_rip3_stream_create(&stream,&sink, f.host.environments)==0);
	const uint8_t wire[]="!|m0|T\xc3|m0e0U|TA|";unsigned before=f.errors;
	for (size_t i=0; i<sizeof(wire)-1; i++) st_rip3_stream_process(stream,wire+i,1);
	CHECK(f.errors==before+2 && cursor(&f,50,37)==0);st_rip3_stream_destroy(stream);
	CHECK(st_rip3_ports_protect(f.host.ports,1,false)==0 && st_rip3_ports_activate(f.host.ports,false)==0);
	struct st_rip3_styles *styles=f.host.styles;f.host.text=NULL;f.host.styles=NULL;
	CHECK(process(&f,"TA",-1)==0 && xy(&f,0,0,"A",-1)==0 && cursor(&f,50,37)==0);
	f.host.styles=styles;destroy(&f);return 0;
}

int
main(void)
{
	for (unsigned canvas=0; canvas<3; canvas++) {
		CHECK(matrix((enum st_rip3_canvas)canvas)==0 && lifecycle((enum st_rip3_canvas)canvas)==0
		    && effects((enum st_rip3_canvas)canvas)==0);
	}
	CHECK(anisotropic()==0 && real_faces()==0 && shadows_and_rgb()==0 && errors_and_encoding()==0);return 0;
}

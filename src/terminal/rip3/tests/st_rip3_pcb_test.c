/* Independent logical pair, physical-boundary, overlap and lifecycle vectors. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_ports.h"
#include "st_rip3_backup.h"
#include "st_rip3_clipboard.h"
#include "st_rip3_host_variables.h"
#include "st_rip3_table_host.h"
#include "st_rip3_styles.h"
#include "st_rip3_builtin_reference.h"
#include "st_pixels.h"
#include "io_error.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do {if (!(x)) {fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}} while (0)
extern bool st_rip3_copy_test_fail_malloc,st_rip3_clipboard_test_fail_copy;

static bool equal(struct st_rip3_copy_extent a,struct st_rip3_copy_extent b)
{
	return a.x0==b.x0 && a.y0==b.y0 && a.x1==b.x1 && a.y1==b.y1;
}
static int pair(struct st_rip3_ports *p,unsigned n,bool set,
    struct st_rip3_copy_extent source,struct st_rip3_copy_extent destination)
{
	struct st_rip3_port_copy_pair actual;
	CHECK(st_rip3_ports_copy_pair(p,n,&actual)==0 && actual.set==set);
	CHECK(!set || (equal(actual.source,source) && equal(actual.destination,destination)));return 0;
}
static int pixels(struct st_rip3_ports *p,unsigned n,const uint8_t *expected,size_t count)
{
	for (size_t x=0;x<count;x++) {uint8_t value;CHECK(st_rip3_ports_read(p,n,(int32_t)x,0,&value)==0 && value==expected[x]);}
	return 0;
}
static int overlap_and_failure(void)
{
	struct st_rip3_ports *p=NULL;const struct st_rip3_rect bounds={0,0,3,0};unsigned one=1;
	const struct st_rip3_copy_extent a={0,0,3,1},b={1,0,4,1};
	CHECK(st_rip3_ports_create_device(&p,16,8)==0 && st_rip3_ports_define(p,1,&bounds,true)==0);
	CHECK(pair(p,1,false,a,b)==0 && st_rip3_ports_switch(p,1)==0);
	CHECK(st_rip3_ports_copy_device(p,1,&a,1,&b,ST_RIP3_TRANSFER_XOR,NULL)==0);
	for (int32_t x=0;x<4;x++) CHECK(st_rip3_ports_plot(p,x,0,(uint8_t)(x+1),ST_RIP3_COPY)==0);
	CHECK(pair(p,1,true,a,b)==0 && st_rip3_ports_protect(p,1,true)==0);
	CHECK(st_rip3_ports_paste(p,&one)==0 && pixels(p,1,(const uint8_t[]){2,3,4,4},4)==0);
	CHECK(pair(p,1,true,b,a)==0);
	CHECK(st_rip3_ports_paste(p,&one)==0 && pixels(p,1,(const uint8_t[]){2,2,3,4},4)==0);
	CHECK(pair(p,1,true,a,b)==0);
	st_rip3_copy_test_fail_malloc=true;
	CHECK(st_rip3_ports_paste(p,&one)==IO_ERROR_OUT_OF_MEMORY);
	CHECK(pair(p,1,true,a,b)==0 && pixels(p,1,(const uint8_t[]){2,2,3,4},4)==0);
	CHECK(st_rip3_ports_paste(p,&one)==0 && pair(p,1,true,b,a)==0);
	const struct st_rip3_copy_extent cell={0,0,1,1};
	CHECK(st_rip3_ports_copy_device(p,1,&cell,1,&cell,ST_RIP3_TRANSFER_COPY,NULL)==0 && pair(p,1,true,cell,cell)==0);
	st_rip3_ports_destroy(p);return 0;
}
static struct st_rip3_copy_extent extent(unsigned axis,int64_t start,int64_t end)
{
	return axis ? (struct st_rip3_copy_extent){0,start,1,end} : (struct st_rip3_copy_extent){start,0,end,1};
}
static int physical_boundaries(void)
{
	/* Logical metadata is independent of sampler coverage, including zero.
	 * The rows are the PR147 vectors; transpose each onto the Y axis too. */
	static const struct {unsigned sp,sw,dp,dw,s0,s1,d0,d1,es0,es1,ed0,ed1;} cases[]={
	    {2,4,3,3,0,4,0,3,0,4,0,3},{2,4,4,4,0,4,0,4,0,4,0,4},{4,4,1,3,0,4,0,3,0,4,0,3},
	    {2,4,3,3,2,4,0,3,2,4,0,3},{2,2,1,4,0,2,2,4,0,2,2,4},
	    {2,4,3,3,0,4,0,4,0,3,0,3},{2,4,3,3,0,5,0,4,0,4,0,3},
	    {2,2,2,2,0,4,0,2,0,2,0,2},{4,4,2,2,0,4,1,5,0,1,1,2},{4,4,2,2,0,4,0,2,0,4,0,2}};
	for (unsigned axis=0;axis<2;axis++) for (size_t i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
		struct st_rip3_ports *p=NULL;
		struct st_rip3_rect sb=axis ? (struct st_rip3_rect){0,0,0,(int32_t)cases[i].sp-1} : (struct st_rip3_rect){0,0,(int32_t)cases[i].sp-1,0};
		struct st_rip3_rect db=axis ? (struct st_rip3_rect){0,0,0,(int32_t)cases[i].dp-1} : (struct st_rip3_rect){0,0,(int32_t)cases[i].dp-1,0};
		CHECK(st_rip3_ports_create_device(&p,16,8)==0);
		CHECK(st_rip3_ports_define(p,1,&sb,true)==0 && st_rip3_ports_define(p,2,&db,true)==0);
		CHECK(st_rip3_ports_switch(p,1)==0 && st_rip3_ports_erase(p,7)==0);
		sb=axis ? (struct st_rip3_rect){0,0,0,(int32_t)cases[i].sw-1} : (struct st_rip3_rect){0,0,(int32_t)cases[i].sw-1,0};
		db=axis ? (struct st_rip3_rect){0,0,0,(int32_t)cases[i].dw-1} : (struct st_rip3_rect){0,0,(int32_t)cases[i].dw-1,0};
		CHECK(st_rip3_ports_viewport_device(p,&sb)==0 && st_rip3_ports_switch(p,2)==0);
		CHECK(st_rip3_ports_erase(p,9)==0 && st_rip3_ports_viewport_device(p,&db)==0);
		struct st_rip3_copy_extent a=extent(axis,cases[i].s0,cases[i].s1),b=extent(axis,cases[i].d0,cases[i].d1);
		struct st_rip3_copy_extent ea=extent(axis,cases[i].es0,cases[i].es1),eb=extent(axis,cases[i].ed0,cases[i].ed1);
		CHECK(st_rip3_ports_copy_device(p,1,&a,2,&b,ST_RIP3_TRANSFER_COPY,NULL)==0 && pair(p,2,true,ea,eb)==0);
		if (i==3 || i==4) {uint8_t pixel;CHECK(st_rip3_ports_read(p,2,0,0,&pixel)==0 && pixel==9);}
		if (i==0) {
			CHECK(st_rip3_ports_blit(p,1,NULL,2,0,0,ST_RIP3_TRANSFER_COPY)==0);
			ea=eb=extent(axis,0,3);CHECK(pair(p,2,true,ea,eb)==0);
		}
		if (i==4) {
			CHECK(st_rip3_ports_blit(p,1,NULL,2,axis ? 0 : 2,axis ? 2 : 0,ST_RIP3_TRANSFER_COPY)==0);
			CHECK(pair(p,2,true,ea,eb)==0);
		}
		if (i==9) {
			const unsigned two=2;
			CHECK(st_rip3_ports_switch(p,0)==0 && st_rip3_ports_paste(p,&two)==0);
			CHECK(pair(p,0,true,eb,ea)==0 && st_rip3_ports_switch(p,2)==0);
		}
		struct st_rip3_copy_extent outside=extent(axis,10,12);
		CHECK(st_rip3_ports_copy_device(p,1,&outside,2,&b,ST_RIP3_TRANSFER_COPY,NULL)==0 && pair(p,2,true,ea,eb)==0);
		const unsigned target=2;
		CHECK(st_rip3_ports_operate_views(p,&target,1,ST_RIP3_VIEW_DEACTIVATE,0)==0);
		CHECK(st_rip3_ports_copy_device(p,1,&a,2,&b,ST_RIP3_TRANSFER_COPY,NULL)==0 && pair(p,2,true,ea,eb)==0);
		st_rip3_ports_destroy(p);
	}
	return 0;
}
struct native_reader {unsigned calls;int status;uint32_t color,width;int32_t x;};
static int read_native(void *context,const struct st_rip3_rect *area,struct st_pixel_buffer *pixels)
{
	struct native_reader *r=context;r->calls++;
	CHECK(area->x0==r->x && area->y0==3 && pixels->width==r->width && pixels->height==1);
	for (size_t i=0;i<(size_t)pixels->width*pixels->height;i++) pixels->primary[i]=r->color;
	return r->status;
}
static int native_screen_sources(void)
{
	struct st_rip3_ports *p=NULL;struct native_reader r={.color=ST_PIXEL_COLOR_RGB_TAG|0x123456,.width=2,.x=2};
	const struct st_rip3_rect bitmap={0,0,1,0},wide={0,0,3,0};
	const struct st_rip3_copy_extent a={2,3,4,4},b={0,0,2,1},c={2,0,4,1};
	CHECK(st_rip3_ports_create_device(&p,16,8)==0 && st_rip3_ports_define(p,5,&bitmap,true)==0);
	st_rip3_ports_bind_screen_reader(p,&r,read_native);
	CHECK(st_rip3_ports_copy_device(p,0,&a,5,&b,ST_RIP3_TRANSFER_COPY,NULL)==0 && r.calls==1);
	uint32_t color;CHECK(st_rip3_ports_read_color(p,5,0,0,false,&color)==0 && color==(ST_PIXEL_COLOR_RGB_TAG|0x123456));
	r.status=IO_ERROR_IO;r.color=ST_PIXEL_COLOR_RGB_TAG|0x654321;
	CHECK(st_rip3_ports_copy_device(p,0,&a,5,&(struct st_rip3_copy_extent){0,0,1,1},ST_RIP3_TRANSFER_COPY,NULL)==IO_ERROR_IO && r.calls==2);
	CHECK(pair(p,5,true,a,b)==0 && st_rip3_ports_read_color(p,5,0,0,false,&color)==0 && color==(ST_PIXEL_COLOR_RGB_TAG|0x123456));
	const unsigned five=5;
	CHECK(st_rip3_ports_paste(p,&five)==0 && r.calls==2);
	CHECK(st_rip3_ports_read_color(p,0,2,3,false,&color)==0 && color==(ST_PIXEL_COLOR_RGB_TAG|0x123456));
	CHECK(st_rip3_ports_switch(p,5)==0 && st_rip3_ports_viewport_device(p,&wide)==0 && st_rip3_ports_switch(p,0)==0);
	CHECK(st_rip3_ports_copy_device(p,0,&a,5,&c,ST_RIP3_TRANSFER_COPY,NULL)==0 && r.calls==2 && pair(p,5,true,a,c)==0);
	/* A screen viewport may exceed its native backing. Preserve the 4-to-3
	 * logical map while the bounded read contains only two physical pixels. */
	const struct st_rip3_rect screen_view={0,0,19,7};
	const struct st_rip3_copy_extent edge={14,3,18,4},three={0,0,3,1};
	CHECK(st_rip3_ports_viewport_device(p,&screen_view)==0);
	r.status=0;r.x=14;
	CHECK(st_rip3_ports_copy_device(p,0,&edge,5,&three,ST_RIP3_TRANSFER_COPY,NULL)==0 && r.calls==3);
	CHECK(pair(p,5,true,edge,three)==0);
	CHECK(st_rip3_ports_read_color(p,5,0,0,false,&color)==0 && color==r.color);
	CHECK(st_rip3_ports_read_color(p,5,1,0,false,&color)==0 && color==(ST_PIXEL_COLOR_RGB_TAG|0x123456));
	st_rip3_ports_destroy(p);return 0;
}
static int native_screen_destination(void)
{
	struct st_rip3_ports *p=NULL;
	struct st_rip3_surface *bitmap=NULL;
	struct native_reader r={.color=ST_PIXEL_COLOR_RGB_TAG|0xa0b0c0,.width=1,.x=14};
	uint32_t source[]={ST_PIXEL_COLOR_RGB_TAG|0x112233,ST_PIXEL_COLOR_RGB_TAG|0x445566};
	const struct st_pixel_buffer pixels={.width=2,.height=1,.primary=source};
	const struct st_rip3_rect source_view={0,0,3,0},target_view={0,0,19,7};
	const struct st_rip3_copy_extent four={0,0,4,1},three={14,3,17,4},one={14,3,15,4};
	CHECK(st_rip3_ports_create_device(&p,16,8)==0);
	CHECK(st_rip3_surface_create(&bitmap,2,1)==0
	    && st_rip3_surface_restore_raw(bitmap,&pixels)==0
	    && st_rip3_ports_install_bitmap(p,5,&bitmap)==0);
	CHECK(st_rip3_ports_switch(p,5)==0 && st_rip3_ports_viewport_device(p,&source_view)==0);
	CHECK(st_rip3_ports_switch(p,0)==0 && st_rip3_ports_viewport_device(p,&target_view)==0);
	CHECK(st_rip3_ports_erase(p,9)==0);
	st_rip3_ports_bind_screen_reader(p,&r,read_native);
	/* Four to three samples 0, 2, 3. Only source sample 0 exists. The screen's
	 * narrower backing also leaves the last destination outside storage. */
	CHECK(st_rip3_ports_copy_device(p,5,&four,0,&three,ST_RIP3_TRANSFER_XOR,NULL)==0 && r.calls==1);
	CHECK(pair(p,0,true,four,three)==0);
	uint32_t color;
	CHECK(st_rip3_ports_read_color(p,0,14,3,false,&color)==0 && color==(ST_PIXEL_COLOR_RGB_TAG|0xb192f3));
	CHECK(st_rip3_ports_read_color(p,0,15,3,false,&color)==0 && color==9);
	/* Four to one samples 2. Publish the logical pair with no native read
	 * or physical write, then fail an eligible read and retain that pair. */
	CHECK(st_rip3_ports_copy_device(p,5,&four,0,&one,ST_RIP3_TRANSFER_XOR,NULL)==0 && r.calls==1);
	CHECK(pair(p,0,true,four,one)==0);
	r.status=IO_ERROR_IO;
	CHECK(st_rip3_ports_copy_device(p,5,&four,0,&three,ST_RIP3_TRANSFER_XOR,NULL)==IO_ERROR_IO && r.calls==2);
	CHECK(pair(p,0,true,four,one)==0);
	CHECK(st_rip3_ports_read_color(p,0,14,3,false,&color)==0 && color==(ST_PIXEL_COLOR_RGB_TAG|0xb192f3));
	st_rip3_ports_destroy(p);
	return 0;
}
static int put_image_origin(void)
{
	struct st_rip3_ports *p=NULL;
	const struct st_rip3_rect bitmap={0,0,7,7},view={3,4,4,4};
	const struct st_rip3_copy_extent a={0,0,2,1},b={7,8,9,9};
	CHECK(st_rip3_ports_create_device(&p,16,16)==0 && st_rip3_ports_define(p,5,&bitmap,true)==0);
	CHECK(st_rip3_ports_switch(p,5)==0 && st_rip3_ports_erase(p,7)==0 && st_rip3_ports_viewport_device(p,&view)==0);
	CHECK(st_rip3_ports_switch(p,0)==0 && st_rip3_ports_blit(p,5,NULL,0,7,8,ST_RIP3_TRANSFER_COPY)==0);
	CHECK(pair(p,0,true,a,b)==0);
	uint8_t color;CHECK(st_rip3_ports_read(p,0,7,8,&color)==0 && color==7);
	st_rip3_ports_destroy(p);return 0;
}
static int moved_origins(void)
{
	struct st_rip3_ports *p=NULL;unsigned five=5;
	const struct st_rip3_rect bitmap={0,0,7,5},initial={100,50,199,99},ink={2,3,5,4};
	const struct st_rip3_copy_extent a={2,3,6,5},b={0,0,4,2};
	CHECK(st_rip3_ports_create_device(&p,256,128)==0 && st_rip3_ports_define(p,5,&bitmap,true)==0);
	CHECK(st_rip3_ports_viewport_device(p,&initial)==0 && st_rip3_ports_fill(p,&ink,7,ST_RIP3_COPY)==0);
	CHECK(st_rip3_ports_copy_device(p,0,&a,5,&b,ST_RIP3_TRANSFER_COPY,NULL)==0 && pair(p,5,true,a,b)==0);
	CHECK(st_rip3_ports_erase(p,0)==0 && st_rip3_ports_paste(p,&five)==0);
	uint8_t color;CHECK(st_rip3_ports_read(p,0,102,53,&color)==0 && color==7);
	const struct st_rip3_rect moved={120,70,219,119};
	CHECK(st_rip3_ports_viewport_device(p,&moved)==0 && st_rip3_ports_paste(p,&five)==0);
	CHECK(st_rip3_ports_read(p,0,122,73,&color)==0 && color==7);
	CHECK(st_rip3_ports_switch(p,5)==0 && st_rip3_ports_erase(p,9)==0);
	const struct st_rip3_rect moved_source={1,1,8,6};
	CHECK(st_rip3_ports_viewport_device(p,&moved_source)==0 && st_rip3_ports_protect(p,5,true)==0);
	CHECK(st_rip3_ports_switch(p,0)==0 && st_rip3_ports_paste(p,&five)==0);
	for (int32_t y=73;y<75;y++) for (int32_t x=122;x<126;x++)
		CHECK(st_rip3_ports_read(p,0,x,y,&color)==0 && color==9);
	CHECK(pair(p,5,true,a,b)==0 && pair(p,0,true,b,a)==0);
	const struct st_rip3_rect screen={10,10,249,119};
	CHECK(st_rip3_ports_define(p,6,&screen,false)==0 && st_rip3_ports_protect(p,6,true)==0);
	CHECK(st_rip3_ports_switch(p,6)==0 && st_rip3_ports_paste(p,&five)==0);
	CHECK(st_rip3_ports_read(p,6,2,3,&color)==0 && color==9 && pair(p,6,true,b,a)==0);
	CHECK(pair(p,5,true,a,b)==0);
	st_rip3_ports_destroy(p);return 0;
}
static int producers_and_backups(void)
{
	struct st_rip3_ports *p=NULL;struct st_rip3_backup *backup=NULL;
	struct st_rip3_clipboard_selection cb={0};struct st_rip3_port_copy_pair saved;
	const struct st_rip3_clipboard_area area={1,1,4,4},button={2,2,6,5};
	const struct st_rip3_rect full={0,0,15,7};
	const struct st_rip3_copy_extent a={1,1,4,4},b={0,0,4,4},c={2,2,6,5},d={0,0,4,3};
	CHECK(st_rip3_ports_create_device(&p,16,8)==0 && st_rip3_ports_backup_create(&backup)==0);
	st_rip3_ports_bind_clipboard(p,&cb);
	CHECK(st_rip3_ports_erase(p,7)==0);
	CHECK(st_rip3_clipboard_capture(p,&cb,&area,10000)==0 && cb.set && cb.port==1);
	CHECK(pair(p,1,true,a,b)==0);
	CHECK(st_rip3_ports_erase(p,0)==0 && st_rip3_ports_paste(p,NULL)==0);
	uint8_t value;CHECK(st_rip3_ports_read(p,0,1,1,&value)==0 && value==7 && pair(p,0,true,b,a)==0);
	CHECK(st_rip3_ports_define(p,6,&full,false)==0);
	CHECK(st_rip3_backup_save(backup,ST_RIP3_BACKUP_BASE,0,p)==0);
	CHECK(st_rip3_clipboard_capture_button(p,&cb,&button,10000)==0 && pair(p,1,true,c,d)==0);
	CHECK(st_rip3_ports_copy_pair(p,1,&saved)==0);
	const struct st_rip3_rect face={0,0,4,3};
	CHECK(st_rip3_ports_button_clipboard(p,1,&face)==0 && pair(p,0,true,b,a)==0);
	struct st_pixel_buffer *screen=NULL;
	CHECK(st_rip3_ports_screen_capture(p,&screen)==0 && st_rip3_ports_erase(p,0)==0);
	CHECK(st_rip3_ports_screen_restore(p,screen)==0 && pair(p,0,true,b,a)==0 && pair(p,1,true,c,d)==0);
	st_pixel_buffer_destroy(screen);
	CHECK(st_rip3_ports_copy(p,1,NULL,6,NULL,ST_RIP3_TRANSFER_COPY)==0);
	CHECK(st_rip3_ports_define(p,7,&full,false)==0 && st_rip3_ports_protect(p,7,true)==0);
	CHECK(st_rip3_ports_copy(p,1,NULL,7,NULL,ST_RIP3_TRANSFER_COPY)==0);
	struct st_rip3_port_copy_pair survivor;CHECK(st_rip3_ports_copy_pair(p,7,&survivor)==0 && survivor.set);
	CHECK(st_rip3_backup_restore(backup,ST_RIP3_BACKUP_BASE,0,p)==0 && pair(p,1,true,a,b)==0);
	CHECK(pair(p,6,false,a,b)==0 && pair(p,7,true,survivor.source,survivor.destination)==0 && cb.set && cb.port==1);
	CHECK(st_rip3_ports_protect(p,1,true)==0 && st_rip3_ports_reset(p,false)==0);
	CHECK(pair(p,1,true,a,b)==0 && pair(p,7,true,survivor.source,survivor.destination)==0);
	CHECK(pair(p,0,false,a,b)==0);
	CHECK(st_rip3_ports_protect(p,1,false)==0);
	st_rip3_clipboard_test_fail_copy=true;
	CHECK(st_rip3_clipboard_capture(p,&cb,&area,10000)==0 && pair(p,1,false,a,b)==0);
	CHECK(st_rip3_clipboard_duplicate(p,&cb,5,&area,10000)==0);
	CHECK(pair(p,5,true,a,(struct st_rip3_copy_extent){0,0,3,3})==0);
	CHECK(st_rip3_ports_delete(p,5)==0 && pair(p,5,false,a,b)==0);
	st_rip3_backup_destroy(backup);st_rip3_ports_destroy(p);return 0;
}
struct observed {unsigned replies,errors;};
static void reply(void *context,const uint8_t *bytes,size_t length)
{
	(void)bytes;(void)length;((struct observed *)context)->replies++;
}
static void diagnostic(void *context,enum st_rip3_host_diagnostic kind)
{
	(void)kind;((struct observed *)context)->errors++;
}
static int query(const struct st_rip3_host_variables *host,const char *program,bool error)
{
	struct observed o={0};const struct st_rip3_host_sink sink={&o,reply,diagnostic};
	st_rip3_host_variables_process(host,&sink,(const uint8_t *)program,strlen(program));
	CHECK(o.replies==0 && o.errors==(error ? 1u : 0u));return 0;
}
static int selectors(void)
{
	struct st_rip3_ports *p=NULL;struct st_rip3_table_host tables={0};
	const struct st_rip3_host_variables host={.tables=&tables};
	struct st_rip3_clipboard_selection cb={0};
	struct st_rip3_styles *styles=NULL;struct st_rip3_style value;
	CHECK(st_rip3_style_defaults(&value)==0);value.write_mode=ST_RIP3_NOT;
	CHECK(st_rip3_styles_create(&styles,&value)==0);tables.tables[ST_RIP3_TABLE_STYLE].live=styles;
	CHECK(!st_rip3_builtin_known("P",1));
	CHECK(st_rip3_ports_create_device(&p,16,8)==0);st_rip3_ports_bind_clipboard(p,&cb);
	tables.tables[ST_RIP3_TABLE_PORT].live=p;
	CHECK(query(&host,"$PCB$",false)==0 && query(&host,"$PCB()$",false)==0);
	CHECK(query(&host,"$PCB( cUr )$",false)==0 && query(&host,"$PCB(00000)$",false)==0);
	const char *const invalid[]={"$PCB( )$","$PCB(\tcur\r)$","$PCB(-1)$","$PCB(+1)$","$PCB(0x5)$","$PCB(1.0)$","$PCB(ALL)$","$PCB(36)$","$PCB(1,2)$"};
	for (size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) CHECK(query(&host,invalid[i],true)==0);
	const struct st_rip3_rect bounds={0,0,3,0};
	const struct st_rip3_copy_extent a={0,0,3,1},b={1,0,4,1};
	CHECK(st_rip3_ports_define(p,5,&bounds,true)==0);
	CHECK(st_rip3_ports_copy_device(p,0,&a,5,&b,ST_RIP3_TRANSFER_COPY,NULL)==0);
	CHECK(query(&host,"$PCB(CUR)$",false)==0 && pair(p,0,false,a,b)==0);
	cb=(struct st_rip3_clipboard_selection){true,5};
	CHECK(query(&host,"$PCB(0005)$",false)==0 && pair(p,0,true,b,a)==0);
	CHECK(query(&host,"$PCB()$",false)==0 && pair(p,0,true,b,a)==0);
	cb.port=0;
	CHECK(query(&host,"$PCB$",false)==0 && cb.set && cb.port==0 && pair(p,0,true,a,b)==0);
	st_rip3_copy_test_fail_malloc=true;
	CHECK(query(&host,"$PCB(CUR)$",true)==0 && pair(p,0,true,a,b)==0);
	CHECK(query(&host,"$PCB(CUR)$",false)==0 && pair(p,0,true,b,a)==0);
	cb=(struct st_rip3_clipboard_selection){true,999};CHECK(query(&host,"$PCB$",false)==0 && cb.port==999);
	cb.port=35;CHECK(query(&host,"$PCB()$",false)==0 && cb.set && cb.port==35);
	CHECK(query(&host,"$PCB(,0005,,)$",false)==0);
	unsigned current;CHECK(st_rip3_ports_current(p,&current)==0 && current==0);
	CHECK(st_rip3_styles_get(styles,0,&value)==0 && value.write_mode==ST_RIP3_NOT);
	st_rip3_styles_destroy(styles);
	st_rip3_ports_destroy(p);return 0;
}
int main(void)
{
	CHECK(overlap_and_failure()==0 && physical_boundaries()==0 && native_screen_sources()==0
	    && native_screen_destination()==0 && put_image_origin()==0 && moved_origins()==0
	    && producers_and_backups()==0 && selectors()==0);
	return 0;
}

/* Native BIG active/display pages over indexed console and backing pixels. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_big_internal.h"
#include "cterm.h"
#include "cterm_internal.h"
#include "cterm_render.h"
#include "io_error.h"

static int ensure_backing(struct cterm_big *big)
{
	if (big->video_backing!=NULL) return 0;
	struct st_pixel_buffer *p=NULL;
	int status=st_pixel_buffer_create(&p,ST_BIG_VIDEO_PENS,1,true);
	if (status!=0) return status;
	for (size_t i=0;i<ST_BIG_VIDEO_PENS;++i) p->primary[i]=p->alternate[i]=CTERM_BIG_PALETTE_FIRST;
	status=st_big_video_bind(&big->video,p->primary,p->alternate,ST_BIG_VIDEO_PENS);
	if (status!=0) st_pixel_buffer_destroy(p); else big->video_backing=p;
	return status;
}
int cterm_big_video_prepare(struct cterm_big *big, bool full_memory)
{
	return full_memory || big->video_start!=0 || big->video_active_page!=0 || big->terminal->status_display_type>=1?ensure_backing(big):0;
}
void cterm_big_video_reset(struct cterm_big *big)
{
	big->video_start=0;
	big->video_active_page=big->video_visual_page=0;
	for (size_t i=0;i<4;++i) big->video_cursors[i][0]=big->video_cursors[i][1]=1;
	if (big->video_backing==NULL) { (void)st_big_video_bind(&big->video,NULL,NULL,0); return; }
	for (size_t i=0;i<ST_BIG_VIDEO_PENS;++i) big->video_backing->primary[i]=big->video_backing->alternate[i]=CTERM_BIG_PALETTE_FIRST;
	(void)st_big_video_bind(&big->video,big->video_backing->primary,big->video_backing->alternate,ST_BIG_VIDEO_PENS);
}
struct st_big_video_view cterm_big_video_view(const struct cterm_big *big, const struct st_pixel_buffer *p)
{
	const struct st_big_mode *mode=st_big_mode((unsigned)big->state.bgi.mode+1);
	uint32_t reserved=big->terminal->status_display_type>=1 && mode!=NULL?mode->cell_height:0;
	size_t count=p->height>=reserved?(size_t)p->width*(p->height-reserved):0;
	return (struct st_big_video_view){p->primary,p->alternate,count,big->video_start};
}
int cterm_big_video_segment(const struct cterm_big *big, uint16_t *segment)
{
	const struct st_big_mode *mode=st_big_mode((unsigned)big->state.bgi.mode+1);
	if (mode==NULL) return IO_ERROR_OUT_OF_RANGE;
	*segment=st_big_video_active_segment(big->video_active_page,(uint16_t)(mode->page_bytes/16)); return 0;
}
struct display { struct cterm_big *big; uint8_t page; uint16_t start; };
static int display(void *context, const struct st_pixel_buffer *p, struct st_console_rect *changed)
{
	const struct display *request=context; struct cterm_big *big=request->big;
	struct st_big_video_view view=cterm_big_video_view(big,p); size_t accepted;
	int status=st_big_video_save(&big->video,&view,&accepted); if (status!=0) return status;
	big->video_start=request->start; big->video_visual_page=request->page; big->state.bgi.visual_page=request->page;
	view.start=request->start;
	status=st_big_video_load(&big->video,&view,&accepted);
	if (accepted!=0) *changed=(struct st_console_rect){0,0,p->width,(uint32_t)((accepted+p->width-1)/p->width)};
	return status;
}
int cterm_big_page(struct cterm_big *big, bool visual, uint16_t requested)
{
	if (big->state.bgi.state==0) { big->state.bgi.error=-1; return IO_ERROR_PROTOCOL; }
	uint8_t page=(uint8_t)requested;
	const struct st_big_mode *mode=st_big_mode((unsigned)big->state.bgi.mode+1);
	if (mode==NULL) return IO_ERROR_OUT_OF_RANGE;
	if (!visual) {
		int status=page!=0?ensure_backing(big):0;
		if (status==0) { big->video_active_page=page; big->state.bgi.active_page=page; }
		return status;
	}
	uint16_t start=st_big_video_display_start(page,mode->visual_page_bytes);
	/* Wire G admits pages0..3. Do not turn a corrupted cursor-table index
	 * into an unchecked native access. */
	if (page>=4 || big->video_visual_page>=4) return IO_ERROR_OUT_OF_RANGE;
	big->video_cursors[big->video_visual_page][0]=(uint16_t)big->terminal->xpos;
	big->video_cursors[big->video_visual_page][1]=(uint16_t)big->terminal->ypos;
	int status=0;
	if (start!=big->video_start) {
		status=ensure_backing(big); if (status!=0) return status;
		struct display request={big,page,start};
		status=st_console_modify_pixels(big->terminal->screen.console,display,&request);
	} else { big->video_visual_page=page; big->state.bgi.visual_page=page; }
	if (status==0) {
		cterm_set_position(big->terminal,big->video_cursors[page][0],big->video_cursors[page][1]);
		status=cterm_render_take_error(big->terminal);
	}
	return status==IO_ERROR_IN_PROGRESS?IO_ERROR_PROTOCOL:status;
}

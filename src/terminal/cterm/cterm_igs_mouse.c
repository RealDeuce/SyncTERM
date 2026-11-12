/* ? queries and shared Atari marker/GEM pointer rendering. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_igs.h"
#include "cterm_igs_mouse.h"
#include "cterm_igs_mouse_forms.h"
#include "cterm.h"
#include "cterm_vt52.h"
#include "io_error.h"
#include <stdio.h>

static int host_pointer(struct cterm_igs *igs, bool visible)
{
	int status = st_console_set_pointer_visible(igs->terminal->screen.console, visible);
	return status == IO_ERROR_NOT_SUPPORTED ? 0 : status;
}
static int publish(struct cterm_igs *igs, const struct cterm_igs_pointer *p, const struct st_pixel_buffer *pixels)
{
	int drawable=igs->terminal->height*(igs->resolution==2?16:8);
	if (p->top>=drawable || p->left>=(int)igs->width) return 0;
	unsigned width=pixels->width, height=pixels->height;
	if (width>igs->width-(unsigned)p->left) width=igs->width-(unsigned)p->left;
	if (height>(unsigned)(drawable-p->top)) height=(unsigned)(drawable-p->top);
	/* A host status/viewport change must not let save-under touch UI rows. */
	const struct st_pixel_blit blit = {.width=width, .height=height,
	    .destination_x=p->left, .destination_y=p->top, .scale_x=1, .scale_y=1};
	return st_console_blit_pixels(igs->terminal->screen.console, pixels, NULL, &blit);
}
static int erase(struct cterm_igs *igs, struct cterm_igs_pointer *p)
{
	int status = 0;
	if (p->saved != NULL) {
		status = publish(igs, p, p->saved);
		st_pixel_buffer_destroy(p->saved); p->saved = NULL;
	} else if (p->drawn && !p->gem) status = cterm_igs_pmarker(igs, p->x, p->y);
	p->drawn = false;
	return status;
}
int cterm_igs_pointer_begin(struct cterm_igs *igs, struct cterm_igs_pointer *p, int style)
{
	/* The binary's wrong selector comparison sends invalid AES forms into
	 * surrounding memory. Accept only the documented built-in forms. */
	if (style > 10) return IO_ERROR_OUT_OF_RANGE;
	*p = (struct cterm_igs_pointer){.active=true, .gem=style>2, .style=style,
	    .saved_mode=igs->attributes.writing_mode, .x=10000, .y=10000};
	igs->attributes.writing_mode = 3;
	int status = st_console_get_pointer_visible(igs->terminal->screen.console, &p->host_visible);
	if (status == 0) { p->host_saved=true; status = host_pointer(igs, false); }
	return status;
}
int cterm_igs_pointer_move(struct cterm_igs *igs, struct cterm_igs_pointer *p, int x, int y)
{
	if (!p->active) return IO_ERROR_PROTOCOL;
	if (p->x == x && p->y == y) return 0;
	int status = erase(igs, p);
	if (status != 0) return status;
	p->x = x; p->y = y;
	if (!p->gem) {
		status = cterm_igs_pmarker(igs, x, y);
		if (status == 0) p->drawn = true;
		return status;
	}
	unsigned form = (unsigned)p->style-3;
	int left = x-mouse_forms[form].hot_x, top = y-mouse_forms[form].hot_y;
	int right = left+16, bottom = top+16;
	int height = igs->terminal->height*(igs->resolution==2?16:8);
	p->left = left < 0 ? 0 : left; p->top = top < 0 ? 0 : top;
	if (right > (int)igs->width) right = (int)igs->width;
	if (bottom > height) bottom = height;
	if (right <= p->left || bottom <= p->top) return 0;
	const struct st_console_rect area = {(unsigned)p->left,(unsigned)p->top,
	    (unsigned)(right-p->left),(unsigned)(bottom-p->top)};
	status = st_console_get_pixels(&p->saved, igs->terminal->screen.console, &area, false);
	struct st_pixel_buffer *pixels = NULL;
	if (status == 0) status = st_pixel_buffer_clone(&pixels, p->saved);
	if (status == 0) {
		unsigned mask = cterm_igs_plane_mask(igs->resolution);
		for (unsigned row=0;row<area.height;++row) for (unsigned col=0;col<area.width;++col) {
			unsigned y0=(unsigned)(p->top-top)+row, x0=(unsigned)(p->left-left)+col;
			bool clear = (mouse_forms[form].mask[y0] & (0x8000u>>x0)) != 0;
			bool set = (mouse_forms[form].data[y0] & (0x8000u>>x0)) != 0;
			size_t offset=(size_t)row*area.width+col;
			uint32_t *phases[]={pixels->primary,pixels->alternate};
			for (unsigned phase=0;phase<2;++phase) {
				if (phases[phase]==NULL) continue;
				uint32_t old=phases[phase][offset];
				if (old>=CTERM_IGS_PALETTE_FIRST && old<CTERM_IGS_PALETTE_FIRST+16)
					phases[phase][offset]=CTERM_IGS_PALETTE_FIRST
					    + (((old-CTERM_IGS_PALETTE_FIRST)&~(clear?mask:0u)) | (set?mask:0u));
			}
		}
		status = publish(igs, p, pixels);
		if (status == 0) p->drawn = true;
	}
	st_pixel_buffer_destroy(pixels);
	return status;
}
int cterm_igs_pointer_end(struct cterm_igs *igs, struct cterm_igs_pointer *p)
{
	if (!p->active) return 0;
	int status = erase(igs, p);
	/* Correct the original height-as-write-mode restore and GEM mode leak. */
	igs->attributes.writing_mode = p->saved_mode;
	int shown = p->host_saved ? host_pointer(igs, p->host_visible) : 0;
	p->active = false;
	return status != 0 ? status : shown;
}
static int coordinate(int value)
{
	return value < 0 ? 0 : value > 9999 ? 9999 : value;
}
static int reply(struct cterm_igs *igs, int selector, unsigned buttons, int x, int y)
{
	char bytes[32]; size_t count = 0;
	if (selector == 0) { bytes[0]='2'; bytes[1]='.'; bytes[2]='2'; bytes[3]='1'; count=4; }
	else if (selector == 1) {
		bytes[0]=(char)(igs->terminal->xpos+31); bytes[1]=(char)(igs->terminal->ypos+31);
		bytes[2]=(char)(buttons+32); count=3;
	} else if (selector == 2) {
		/* Correct missing thousands padding and asymmetric Y capping. */
		int n=snprintf(bytes,sizeof(bytes),"%d,%d,",coordinate(x),coordinate(y));
		if (n < 0 || (size_t)n+2 > sizeof(bytes)) return IO_ERROR_OUT_OF_RANGE;
		count=(size_t)n; bytes[count++]=(char)(buttons+48); bytes[count++]=':';
	} else if (selector == 3) { bytes[0]=(char)(igs->resolution+48); bytes[1]=':'; count=2; }
	for (size_t i=0;i<count;++i) {
		cterm_respond(igs->terminal,bytes+i,1);
		if (igs->reset_pending) return IO_ERROR_INTERRUPTED;
	}
	return 0;
}
int cterm_igs_query_sample(struct cterm_igs *igs, int x, int y, unsigned buttons)
{
	struct cterm_igs_query *q = &igs->query;
	if (!q->active || q->ready) return 0;
	q->x=x; q->y=y;
	int status = 0;
	if (q->release) {
		/* GEM is a live device pointer during release; the explicit XOR
		 * polymarker remains at its last press-loop position. */
		if (q->pointer.gem) status=cterm_igs_pointer_move(igs,&q->pointer,x,y);
		if (status!=0) { q->error=status; q->ready=true; }
		if (buttons == 0) q->ready=true;
		return status;
	}
	if (q->selector == 1) {
		const uint8_t movement[][2]={{27,'D'},{27,'C'},{27,'A'},{27,'B'}};
		if (x != q->previous_x) status=cterm_vt52_output(igs->terminal,movement[x<q->previous_x?0:1],2);
		if (status == 0 && !igs->reset_pending && y != q->previous_y)
			status=cterm_vt52_output(igs->terminal,movement[y<q->previous_y?2:3],2);
		q->previous_x=x; q->previous_y=y;
	} else status=cterm_igs_pointer_move(igs,&q->pointer,x,y);
	if (status == 0 && buttons != 0) { q->buttons=(int)buttons; q->release=true; }
	if (igs->reset_pending) status=IO_ERROR_INTERRUPTED;
	if (status != 0) { q->error=status; q->ready=true; }
	return status;
}
int cterm_igs_query_begin(struct cterm_igs *igs, int selector, int style)
{
	struct cterm_igs_query *q = &igs->query;
	if (q->active) return IO_ERROR_BUSY;
	q->selector=selector; q->style=style;
	if ((selector != 1 && selector != 2) || style == 0)
		return reply(igs,selector,igs->input.buttons,igs->input.mouse_x,igs->input.mouse_y);
	*q=(struct cterm_igs_query){.active=true,.selector=selector,.style=style,
	    .previous_x=igs->input.mouse_x,.previous_y=igs->input.mouse_y};
	int status=selector==2?cterm_igs_pointer_begin(igs,&q->pointer,style):0;
	if (status == 0) status=cterm_igs_query_sample(igs,igs->input.mouse_x,igs->input.mouse_y,igs->input.suppressed?0:igs->input.buttons);
	if (status != 0) { (void)cterm_igs_query_cancel(igs); return status; }
	return IO_ERROR_IN_PROGRESS;
}
int cterm_igs_query_finish(struct cterm_igs *igs)
{
	struct cterm_igs_query *q = &igs->query;
	if (!q->active || !q->ready) return IO_ERROR_PROTOCOL;
	int status=cterm_igs_pointer_end(igs,&q->pointer);
	if (status == 0) status=q->error;
	if (status == 0) status=reply(igs,q->selector,(unsigned)q->buttons,q->x,q->y);
	q->active=q->ready=q->release=false;
	return status;
}
int cterm_igs_query_cancel(struct cterm_igs *igs)
{
	int status=cterm_igs_pointer_end(igs,&igs->query.pointer);
	igs->query.configuring=igs->query.active=igs->query.ready=igs->query.release=false;
	return status;
}

/* BIG G0..3 acquisition prefixes and original image/page schedule25/56. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_big_internal.h"
#include "io_error.h"

static uint16_t mode(uint16_t value)
{
	return st_big_number_signed(value)<0 || value>4?0:value;
}
static int active(struct cterm_big *big, uint16_t page)
{
	++big->image_page_calls;
	return cterm_big_page(big,false,page);
}
int cterm_big_image_field(struct cterm_big *big, const struct st_graphics_command_view *view)
{
	if (view->count<1 || !view->arguments[0].has_value) return IO_ERROR_PROTOCOL;
	uint16_t operation=(uint16_t)view->arguments[0].value;
	if (view->count==1) {
		big->image_operation=operation;
		if (operation<3) return active(big,big->state.active_page);
	}
	if (view->count==2 && operation<4) {
		if (!view->arguments[1].has_value) return IO_ERROR_PROTOCOL;
		big->image_mode=(uint16_t)view->arguments[1].value;
		if (operation!=2) big->image_mode=mode(big->image_mode);
	}
	return 0;
}
static int size_query(struct cterm_big *big, const uint16_t words[4])
{
	/* Extended plane-count query precedes the feature gate. The original
	 * unsigned WORD >ffff guard cannot reject any returned size. */
	if (big->state.bgi.state==0) { big->state.bgi.error=-1; return IO_ERROR_PROTOCOL; }
	big->image_size=st_big_image_size(words[0],words[1],words[2],words[3]);
	return 0;
}
int cterm_big_image_command(struct cterm_big *big, const struct st_graphics_command_view *view)
{
	if (view->count<1 || !view->arguments[0].has_value) return IO_ERROR_PROTOCOL;
	uint16_t operation=(uint16_t)view->arguments[0].value;
	if (operation>=4) return IO_ERROR_PROTOCOL;
	size_t count=operation==2?4:operation==1?6:8;
	if (view->count!=count) return IO_ERROR_PROTOCOL;
	uint16_t words[6]={0};
	for (size_t i=1;i<count;++i) {
		if (!view->arguments[i].has_value) return IO_ERROR_PROTOCOL;
		if (i>=2) words[i-2]=(uint16_t)view->arguments[i].value;
	}
	if (operation==2) {
		big->image_mode=mode(big->image_mode);
		return big->image_gates[2][0]==big->image_gates[2][1]?cterm_big_draw_image(big,false,words):0;
	}
	int status=0;
	if (operation==3) status=active(big,big->state.memory_page);
	if (status==0) status=size_query(big,words);
	if (status!=0) return status;
	if (big->image_gates[operation][0]==big->image_gates[operation][1]) {
		status=cterm_big_draw_image(big,true,words); if (status!=0) return status;
		if (operation==0) return cterm_big_draw_image(big,false,words+4);
		if (operation==1 && big->numeric.context.resolution!=3) {
			status=active(big,big->state.memory_page);
			/* Original first corner, not capture's unsigned-normalized one. */
			if (status==0) status=cterm_big_draw_image(big,false,words);
			if (status==0) status=active(big,big->state.active_page);
			return status;
		}
		if (operation==3) {
			status=active(big,big->state.active_page);
			if (status==0) status=cterm_big_draw_image(big,false,words+4);
			if (status!=0) return status;
		}
	}
	/* This second active call is also reached when G3's gate is false.
	 * It is not coalesced with the pre-paste call or run after an error. */
	return operation==3?active(big,big->state.active_page):0;
}

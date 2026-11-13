/* X0's two-draw signed sampling and direct marker schedule, BIG22/58. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_spray.h"
#include "st_big_random.h"
#include "st_big_number.h"
#include "io_error.h"
int st_big_draw_spray(struct st_big_state *s, struct st_big_spray *r, uint32_t *random,
	const struct st_big_draw_sink *sink)
{
	if (s==NULL || r==NULL || random==NULL || sink==NULL || sink->pixel==NULL) return IO_ERROR_INVALID_ARGUMENT;
	r->counter=0;
	while (st_big_number_signed(r->counter)<st_big_number_signed(r->count)) {
		r->dx=(uint16_t)((int32_t)st_big_random_next(random)*st_big_number_signed(r->width)/32768);
		r->dy=(uint16_t)((int32_t)st_big_random_next(random)*st_big_number_signed(r->height)/32768);
		int status=0;
		if (s->application.marker==0) {
			uint16_t y=(uint16_t)(r->y+r->dy), x=(uint16_t)(r->x+r->dx);
			status=st_big_draw_point(s,x,y,s->application.colors[0],sink);
		}
		if (status==0 && s->application.marker!=0) {
			uint16_t y=(uint16_t)(r->y+r->dy), x=(uint16_t)(r->x+r->dx);
			status=st_big_draw_marker(s,x,y,sink);
		}
		if (status!=0) return status;
		r->counter=(uint16_t)(r->counter+1u);
	}
	return 0;
}

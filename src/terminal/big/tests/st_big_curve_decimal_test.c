/* Independent point-request traces from DDO0d70..0fa0, BIG13/61. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_curve.h"
#include "io_error.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
struct point { uint16_t x,y; };
struct fixture {
	struct point points[256]; size_t count,fail_at; int error;
	struct st_big_curve_decimal_frame frame;
	bool mutate;
};
static int point(void *context, uint16_t x, uint16_t y)
{
	struct fixture *f=context; CHECK(f->count<256); f->points[f->count++]=(struct point){x,y};
	if (f->mutate) memset(&f->frame,0xff,sizeof(f->frame));
	return f->count==f->fail_at?f->error:0;
}
static struct st_big_curve_decimal_frame full(uint16_t rx, uint16_t ry)
{
	return (struct st_big_curve_decimal_frame){.center={100,100},.radius={rx,ry},.start={1,0},.end={1,0}};
}
static int run(struct fixture *f)
{
	const struct st_big_curve_point_sink sink={f,point};
	return st_big_curve_decimal_outline(f->frame,&sink);
}
static void expect(struct fixture *f, const struct point *points, size_t count)
{
	CHECK(f->count==count);
	for (size_t i=0;i<count;++i) CHECK(f->points[i].x==points[i].x && f->points[i].y==points[i].y);
}
static void small_literal_traces(void)
{
	/* These literal sequences retain axis coincidences, each regime's
	 * body-first entry and the original four-candidate order. */
	static const struct point unit[]={ {100,101},{100,101},{100,99},{100,99},{101,100},{99,100},{101,100},{99,100} };
	static const struct point wide[]={ {100,101},{100,101},{100,99},{100,99}, {101,101},{99,101},{101,99},{99,99}, {102,100},{98,100},{102,100},{98,100} };
	static const struct point tall[]={ {100,102},{100,102},{100,98},{100,98}, {101,101},{99,101},{101,99},{99,99}, {101,100},{99,100},{101,100},{99,100} };
	static const struct point two[]={ {100,102},{100,102},{100,98},{100,98}, {101,102},{99,102},{101,98},{99,98}, {102,101},{98,101},{102,99},{98,99}, {102,100},{98,100},{102,100},{98,100} };
	static const struct point three[]={ {100,103},{100,103},{100,97},{100,97}, {101,103},{99,103},{101,97},{99,97}, {102,102},{98,102},{102,98},{98,98}, {103,101},{97,101},{103,99},{97,99}, {103,100},{97,100},{103,100},{97,100} };
	struct fixture f={.frame=full(1,1)}; CHECK(run(&f)==0); expect(&f,unit,8);
	f=(struct fixture){.frame=full(2,1)}; CHECK(run(&f)==0); expect(&f,wide,12);
	f=(struct fixture){.frame=full(1,2)}; CHECK(run(&f)==0); expect(&f,tall,12);
	f=(struct fixture){.frame=full(2,2)}; CHECK(run(&f)==0); expect(&f,two,16);
	f=(struct fixture){.frame=full(3,3)}; CHECK(run(&f)==0); expect(&f,three,20);
	f=(struct fixture){.frame=full(0,0)}; CHECK(run(&f)==0); expect(&f,unit,8);
	f=(struct fixture){.frame=full(1,1),.mutate=true}; CHECK(run(&f)==0); expect(&f,unit,8);
	/* Captured WORD center addition wraps; no clipping or CP mutation
	 * belongs to this request generator. */
	static const struct point wrapped[]={ {65535,1},{65535,1},{65535,65535},{65535,65535},{0,0},{65534,0},{0,0},{65534,0} };
	f=(struct fixture){.frame=full(1,1)}; f.frame.center[0]=65535; f.frame.center[1]=0;
	CHECK(run(&f)==0); expect(&f,wrapped,8);
}
static void limits_before_zero_and_signed_gates(void)
{
	struct fixture f={.frame=full(2,1)}; f.frame.start[0]=2; f.frame.end[0]=0; f.frame.end[1]=65535;
	static const struct point quarter[]={ {100,99},{100,99},{101,99},{102,100},{102,100} };
	CHECK(run(&f)==0); expect(&f,quarter,5);
	f=(struct fixture){.frame=full(0,1)}; f.frame.start[0]=0; f.frame.end[0]=0; f.frame.end[1]=65535;
	static const struct point top[]={ {100,99},{100,99} };
	CHECK(run(&f)==0); expect(&f,top,2); /* Limits0..1 precede radius0->1. */
	f=(struct fixture){.frame=full(1,1)}; f.frame.start[0]=0; f.frame.start[1]=65535; f.frame.end[0]=65535;
	static const struct point upper_left[]={ {100,99},{100,99},{99,100},{99,100} };
	CHECK(run(&f)==0); expect(&f,upper_left,4);
	f=(struct fixture){.frame=full(1,1)}; f.frame.start[0]=f.frame.start[1]=32768; f.frame.end[0]=0;
	static const struct point signed_inverse[]={ {100,101},{100,101},{101,100},{101,100} };
	CHECK(run(&f)==0); expect(&f,signed_inverse,4);
}
static void checked_products_and_pair_wrap(void)
{
	struct fixture f={.frame=full(6554,1)}; CHECK(run(&f)==0 && f.count==0);
	f=(struct fixture){.frame=full(1,65535)}; CHECK(run(&f)==0 && f.count==0);
	f=(struct fixture){.frame=full(6553,1),.fail_at=1,.error=IO_ERROR_IO}; CHECK(run(&f)==IO_ERROR_IO && f.count==1 && f.points[0].x==100 && f.points[0].y==101);
	/* B=4294180900, step=B, C100. 2*step wraps to signed-1572792,
	 * below threshold1310500, so the first two turns keep y6553. */
	static const struct point negative_compare[]={ {100,6653},{100,6653},{100,(uint16_t)-6453},{100,(uint16_t)-6453}, {101,6653},{99,6653},{101,(uint16_t)-6453},{99,(uint16_t)-6453} };
	f=(struct fixture){.frame=full(1,6553),.fail_at=8,.error=IO_ERROR_IO}; CHECK(run(&f)==IO_ERROR_IO); expect(&f,negative_compare,8);
	/* B2148322500. After first turn: error2147395600, accumulated1677704,
	 * delta926800. Next increment2150000204 logical-half plus error wraps
	 * to signed-1072571594<=926700, so the third turn has x2/y4633. */
	static const struct point logical_half[]={ {100,4735},{100,4735},{100,(uint16_t)-4535},{100,(uint16_t)-4535}, {101,4734},{99,4734},{101,(uint16_t)-4534},{99,(uint16_t)-4534}, {102,4733},{98,4733},{102,(uint16_t)-4533},{98,(uint16_t)-4533} };
	f=(struct fixture){.frame=full(1,4635),.fail_at=12,.error=IO_ERROR_IO}; CHECK(run(&f)==IO_ERROR_IO); expect(&f,logical_half,12);
}
static void error_prefixes(void)
{
	for (size_t fail=1;fail<=8;++fail) {
		struct fixture f={.frame=full(1,1),.fail_at=fail,.error=IO_ERROR_IO};
		CHECK(run(&f)==IO_ERROR_IO && f.count==fail);
	}
	struct fixture f={.frame=full(1,1),.fail_at=3,.error=IO_ERROR_IN_PROGRESS};
	CHECK(run(&f)==IO_ERROR_PROTOCOL && f.count==3);
	CHECK(st_big_curve_decimal_outline(full(1,1),NULL)==IO_ERROR_INVALID_ARGUMENT);
	const struct st_big_curve_point_sink invalid={0};
	CHECK(st_big_curve_decimal_outline(full(1,1),&invalid)==IO_ERROR_INVALID_ARGUMENT);
}
int main(void) { small_literal_traces(); limits_before_zero_and_signed_gates(); checked_products_and_pair_wrap(); error_prefixes(); return 0; }

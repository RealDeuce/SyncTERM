/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_arc.h"
#include "io_error.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { fprintf(stderr,"line %d: %s\n",__LINE__,#e); return 1; } } while (0)

int
main(void)
{
	/* Literal endpoint vectors from the reference's integer table, including
	 * independently reduced turns and the discontinuity just below 360. */
	static const struct {
		unsigned angle;
		int x, y;
	} rays[] = {
		{0,40,0},{1,39,0},{5,39,-3},{45,28,-28},{89,0,-39},
		{90,0,-40},{91,0,-39},{135,-28,-28},{179,-39,0},
		{180,-40,0},{181,-39,0},{225,-28,28},{269,0,39},
		{270,0,40},{271,0,39},{315,28,28},{359,39,0},
		{360,40,0},{450,0,-40},{719,39,0},{720,40,0},
		{810,0,-40},{1295,-32,22},{4095,-28,-28},
	};
	struct st_rip3_arc arc, before;
	for (size_t i=0; i<sizeof(rays)/sizeof(rays[0]); i++) {
		CHECK(st_rip3_arc_axis(&arc,40,40,rays[i].angle,rays[i].angle)==0);
		CHECK(arc.kind==ST_RIP3_ARC_POINT && arc.first.x==rays[i].x && arc.first.y==rays[i].y);
		CHECK(arc.last.x==arc.first.x && arc.last.y==arc.first.y);
	}
	CHECK(st_rip3_arc_axis(&arc,40,40,0,360)==0 && arc.kind==ST_RIP3_ARC_POINT);
	CHECK(st_rip3_arc_axis(&arc,40,40,450,810)==0 && arc.kind==ST_RIP3_ARC_POINT);
	CHECK(st_rip3_arc_axis(&arc,40,40,0,1)==0 && arc.kind==ST_RIP3_ARC_CURVE && arc.sweep==360);
	CHECK(st_rip3_arc_axis(&arc,40,40,0,719)==0 && arc.kind==ST_RIP3_ARC_CURVE && arc.sweep==360);
	CHECK(st_rip3_arc_axis(&arc,40,40,270,90)==0 && arc.start==270 && arc.sweep==180);
	/* On an ellipse, ray bearing is not the parametric angle: this ray
	 * intersects x=20*cos(t), y=-40*sin(t) at t=atan(1/2). */
	CHECK(st_rip3_arc_axis(&arc,20,40,45,90)==0 && arc.first.x==28 && arc.first.y==-28);
	CHECK(fabs(arc.start-26.56505117707799)<1e-10 && fabs(arc.sweep-63.43494882292201)<1e-10);
	CHECK(st_rip3_arc_axis(&arc,2,2,0,1)==0 && arc.kind==ST_RIP3_ARC_CURVE && arc.sweep==360);
	CHECK(st_rip3_arc_axis(&arc,1,1,0,1)==0 && arc.kind==ST_RIP3_ARC_EMPTY);
	CHECK(st_rip3_arc_axis(&arc,0,0,0,90)==0 && arc.kind==ST_RIP3_ARC_POINT);
	CHECK(st_rip3_arc_axis(&arc,0,40,0,90)==0 && arc.kind==ST_RIP3_ARC_CURVE && arc.sweep==90);
	CHECK(st_rip3_arc_axis(&arc,UINT64_C(1024)*INT32_MAX,1,0,1)==0 && arc.sweep==360);
	CHECK(st_rip3_arc_axis(&arc,UINT64_C(1024)*INT32_MAX,1,85,90)==0);
	CHECK(arc.kind==ST_RIP3_ARC_CURVE && arc.sweep>0 && arc.sweep<1e-10);
	before=arc;
	CHECK(st_rip3_arc_axis(&arc,UINT64_MAX,1,0,90)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(memcmp(&arc,&before,sizeof(arc))==0);
	CHECK(st_rip3_arc_axis(&arc,1,1,4096,90)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_arc_axis(NULL,1,1,0,90)==IO_ERROR_INVALID_ARGUMENT);
	return 0;
}

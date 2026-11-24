/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_skew.h"
#include "io_error.h"

#include <stdio.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { fprintf(stderr,"line %d: %s\n",__LINE__,#e);return 1; } } while (0)

int
main(void)
{
	static const struct { unsigned s,e,start,end; } cases[]={
		{0,0,0,0},{90,90,90,90},{270,90,270,450},{0,360,0,360},
		{360,0,360,360},{450,810,90,450},{0,720,0,360},
		{450,450,90,450},{0,719,0,719},{4095,4095,135,495},
		{4095,0,135,360},{0,4095,0,495},{810,450,90,450},
	};
	struct st_rip3_skew_path path={0},rotated={0};
	for (size_t i=0; i<sizeof(cases)/sizeof(cases[0]); i++) {
		unsigned s=cases[i].s,e=cases[i].e;
		CHECK(st_rip3_skew_span(&s,&e)==0 && s==cases[i].start && e==cases[i].end);
		for (enum st_rip3_skew_kind kind=ST_RIP3_SKEW_ARC; kind<=ST_RIP3_SKEW_PIE; kind++) {
			CHECK(st_rip3_skew_build(&path,10,20,40,20,0,cases[i].s,cases[i].e,kind)==0);
			size_t samples=(size_t)(e-s)+1;
			CHECK(path.count==samples+(kind==ST_RIP3_SKEW_ARC ? 0 : kind==ST_RIP3_SKEW_CHORD ? 1 : 2));
			if (kind!=ST_RIP3_SKEW_ARC) CHECK(path.points[path.count-1].x==path.points[0].x && path.points[path.count-1].y==path.points[0].y);
			if (kind==ST_RIP3_SKEW_PIE) CHECK(path.points[path.count-2].x==10 && path.points[path.count-2].y==20);
			st_rip3_skew_free(&path);
		}
	}
	CHECK(st_rip3_skew_build(&path,10,20,40,20,0,0,360,ST_RIP3_SKEW_OVAL)==0 && path.count==360);
	static const struct { unsigned angle; int x,y; } gold[]={
		{0,50,20},{30,44,11},{60,30,3},{90,10,0},{180,-30,20},{270,10,40},{330,44,30},
	};
	for (size_t i=0; i<sizeof(gold)/sizeof(gold[0]); i++)
		CHECK(path.points[gold[i].angle].x==gold[i].x && path.points[gold[i].angle].y==gold[i].y);
	st_rip3_skew_free(&path);
	CHECK(st_rip3_skew_build(&path,10,20,40,20,90,0,360,ST_RIP3_SKEW_OVAL)==0);
	CHECK(path.points[0].x==10 && path.points[0].y==-20 && path.points[90].x==-10 && path.points[90].y==20);
	CHECK(st_rip3_skew_build(&rotated,10,20,40,20,450,0,360,ST_RIP3_SKEW_OVAL)==0);
	CHECK(path.count==rotated.count && memcmp(path.points,rotated.points,path.count*sizeof(*path.points))==0);
	st_rip3_skew_free(&path);st_rip3_skew_free(&rotated);
	CHECK(st_rip3_skew_build(&path,10,20,40,20,45,0,360,ST_RIP3_SKEW_OVAL)==0);
	CHECK(path.points[0].x==38 && path.points[0].y==-8 && path.points[90].x==-5 && path.points[90].y==6);
	st_rip3_skew_free(&path);
	CHECK(st_rip3_skew_build(&path,0,0,3,3,0,0,719,ST_RIP3_SKEW_ARC)==0 && path.count==720);
	CHECK(memcmp(path.points,path.points+360,360*sizeof(*path.points))==0);st_rip3_skew_free(&path);
	struct st_rip3_vector sentinel={7,8};path=(struct st_rip3_skew_path){&sentinel,99,7,8};
	CHECK(st_rip3_skew_build(&path,0,0,0,0,4096,0,0,ST_RIP3_SKEW_ARC)==IO_ERROR_OUT_OF_RANGE);
	CHECK(path.points==&sentinel && path.count==99 && sentinel.x==7 && path.start==7);
	unsigned s=4096,e=0;
	CHECK(st_rip3_skew_span(&s,&e)==IO_ERROR_OUT_OF_RANGE && s==4096 && e==0);
	CHECK(st_rip3_skew_span(NULL,&e)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_skew_build(NULL,0,0,0,0,0,0,0,ST_RIP3_SKEW_ARC)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_marker_build(&path,36,0,0,10,10,0)==IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_marker_build(&path,1,0,0,10,10,360)==IO_ERROR_OUT_OF_RANGE);
	CHECK(path.points==&sentinel && path.count==99);
	static const unsigned counts[] = {
		360,12,15,12,12,20,3,4,4,4,4,16,16,24,7,13,4,10,24,48,
		3,4,5,6,7,8,9,10,10,12,16,20,24,28,32,16,
	};
	for (unsigned marker = 0; marker < sizeof(counts) / sizeof(counts[0]); ++marker) {
		CHECK(st_rip3_marker_build(&path, marker, 100, 100, 100, 60, 90) == 0);
		CHECK(path.count == counts[marker]);
		if (marker == 0)
			CHECK(path.points[0].x == 100 && path.points[0].y == 50);
		if (marker == 14)
			CHECK(path.points[0].x == 70 && path.points[0].y == 100);
		st_rip3_skew_free(&path);
	}
	CHECK(st_rip3_marker_build(&path, 21, 100, 100, 0, 0, 0) == 0);
	CHECK(path.points[0].x == 99 && path.points[0].y == 99
	    && path.points[2].x == 100 && path.points[2].y == 100);
	st_rip3_skew_free(&path);
	return 0;
}

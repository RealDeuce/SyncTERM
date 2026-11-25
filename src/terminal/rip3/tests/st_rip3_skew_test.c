/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_skew.h"
#include "io_error.h"

#include <stdio.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { fprintf(stderr,"line %d: %s\n",__LINE__,#e);return 1; } } while (0)


static int
check_affine(void)
{
	/* These counts, closure bits and coordinate hashes come from the
	 * RIPlib x86 probe of RIPtel's geometry function at RVA 0x0fa70,
	 * driver MD5 bade8b1f4e467ac7ad4edb2639738d4c. */
	static const struct {
		int xy[10];
		unsigned closed, counts[4];
		uint32_t hash[4];
	} vectors[] = {
		{{100,100,150,100,100,60,150,100,100,60}, 13, {72,19,21,20},
		    {0x2204b668u,0x0f678c4du,0x739940cfu,0x2a4ef7bfu}},
		{{100,100,150,100,100,60,150,100,150,100}, 15, {72,72,72,72},
		    {0x2204b668u,0x2204b668u,0x2204b668u,0x2204b668u}},
		{{100,100,150,100,100,60,100,100,100,100}, 13, {72,19,21,20},
		    {0x2204b668u,0x0f678c4du,0x739940cfu,0x2a4ef7bfu}},
		{{100,100,150,100,100,60,149,99,147,90}, 13, {72,4,6,5},
		    {0x2204b668u,0x8ee6541eu,0xfa8a6109u,0x8dd8bbc9u}},
		{{100,100,150,100,100,60,147,90,149,99}, 13, {72,72,74,73},
		    {0x2204b668u,0x37b0bcd3u,0x197de8e4u,0xeac4ce94u}},
		{{100,100,100,60,150,100,70,120,140,90}, 13, {72,34,36,35},
		    {0x2204b668u,0x8a8068f3u,0xe70666a5u,0x53390a95u}},
		{{100,100,140,80,110,60,140,80,110,60}, 13, {72,19,21,20},
		    {0x451c3a1du,0xb158b7beu,0x1ff57152u,0x0f154312u}},
		{{100,100,100,100,100,100,110,100,100,110}, 0, {1,1,1,1},
		    {0x5d1cd035u,0x5d1cd035u,0x5d1cd035u,0x5d1cd035u}},
		{{251,593,314,413,238,244,619,145,553,327}, 13, {72,2,4,3},
		    {0x84cc4257u,0x74e1408bu,0xe8055698u,0x10176ce0u}},
		{{100,100,150,100,200,100,150,100,100,120}, 13, {72,32,34,33},
		    {0xa8524c71u,0xa3b0038cu,0x228d23a6u,0x82df0c3eu}},
		{{100,100,100,100,100,150,110,90,90,110}, 15, {72,72,72,72},
		    {0xba98a121u,0xba98a121u,0xba98a121u,0xba98a121u}},
		{{100,100,200,99,250,98,150,100,100,120}, 13, {72,73,75,74},
		    {0xc0c203dfu,0xd07c2c93u,0xabb855c0u,0x8e56b770u}},
		{{100,100,150,100,200,100,100,120,150,100}, 13, {72,2,4,3},
		    {0xa8524c71u,0x01b98424u,0xaae72374u,0x8a9d922cu}},
	};
	static const enum st_rip3_skew_kind operations[] = {
		ST_RIP3_SKEW_OVAL, ST_RIP3_SKEW_ARC, ST_RIP3_SKEW_PIE, ST_RIP3_SKEW_CHORD
	};
	for (size_t i = 0; i < sizeof(vectors) / sizeof(vectors[0]); ++i) {
		struct st_rip3_vector points[5];
		for (size_t p = 0; p < 5; ++p)
			points[p] = (struct st_rip3_vector){vectors[i].xy[2*p], vectors[i].xy[2*p + 1]};
		for (unsigned mode = 0; mode < 4; ++mode) {
			struct st_rip3_skew_path path = {0};
			bool closed;
			CHECK(st_rip3_affine_build(&path, &closed, points, operations[mode]) == 0);
			CHECK(path.count == vectors[i].counts[mode] && closed == ((vectors[i].closed >> mode) & 1));
			uint32_t digest = UINT32_C(2166136261);
			for (size_t p = 0; p < path.count; ++p) {
				digest = (digest ^ (uint16_t)(int64_t)path.points[p].x) * UINT32_C(16777619);
				digest = (digest ^ (uint16_t)(int64_t)path.points[p].y) * UINT32_C(16777619);
			}
			if (digest != vectors[i].hash[mode])
				fprintf(stderr, "Affine vector %zu mode %u: %08x expected %08x\n",
				    i, (unsigned)mode, (unsigned)digest, (unsigned)vectors[i].hash[mode]);
			CHECK(digest == vectors[i].hash[mode]);
			st_rip3_skew_free(&path);
		}
	}
	return 0;
}

static int
check_q14(void)
{
	/* FNV over the 360 (cos[t], -sin[t]) low words in the adopted
	 * techspecs/data/skewed-ellipse-q14.json, revision acafaea. A radius
	 * of 16384 exposes each coefficient at rotation zero. */
	struct st_rip3_skew_path path = {0};
	CHECK(st_rip3_skew_build(&path, 0, 0, 16384, 16384, 0, 0, 360, ST_RIP3_SKEW_OVAL) == 0);
	CHECK(path.count == 360);
	uint32_t digest = UINT32_C(2166136261);
	for (size_t i = 0; i < path.count; ++i) {
		digest = (digest ^ (uint16_t)(int64_t)path.points[i].x) * UINT32_C(16777619);
		digest = (digest ^ (uint16_t)(int64_t)path.points[i].y) * UINT32_C(16777619);
	}
	CHECK(digest == UINT32_C(0x099b8da5));
	st_rip3_skew_free(&path);
	/* Published vectors cover both floors, wrapped angles and wide radii. */
	static const struct {
		int x, y;
		unsigned rx, ry, rotation, parameter;
		int expected_x, expected_y;
	} vectors[] = {
		{10,20,6,3,0,0,16,20},
		{10,20,6,3,0,90,10,17},
		{10,20,6,3,0,180,4,20},
		{10,20,6,3,0,270,10,23},
		{10,20,6,3,90,0,10,14},
		{100,80,13,7,30,30,108,72},
		{100,80,13,7,30,330,111,78},
		{100,80,13,7,390,390,108,72},
		{0,0,131072,3,0,0,131072,0},
	};
	for (size_t i = 0; i < sizeof(vectors) / sizeof(*vectors); i++) {
		unsigned angle = vectors[i].parameter;
		CHECK(st_rip3_skew_build(&path, vectors[i].x, vectors[i].y, vectors[i].rx,
		    vectors[i].ry, vectors[i].rotation, 0, angle, ST_RIP3_SKEW_ARC) == 0);
		CHECK(path.count == angle + 1);
		CHECK(path.points[angle].x == vectors[i].expected_x && path.points[angle].y == vectors[i].expected_y);
		st_rip3_skew_free(&path);
	}
	return 0;
}

int
main(void)
{
	CHECK(check_affine() == 0 && check_q14() == 0);
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

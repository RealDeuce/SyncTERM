/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_skew.h"
#include "io_error.h"

#include <stdlib.h>

#include "st_rip3_marker_data.inc"

int
st_rip3_skew_span(unsigned *start, unsigned *end)
{
	unsigned s,e,k;
	if (start==NULL || end==NULL || start==end) return IO_ERROR_INVALID_ARGUMENT;
	s=*start;e=*end;
	if (s>4095 || e>4095) return IO_ERROR_OUT_OF_RANGE;
	if (s<=360 && e<=360) {
		if (s>e) e+=360;
	}
	else if (s<e) {
		k=s/360;s-=360*k;e-=360*k;
		if (e>=720) e=360+(e-360)%360;
	}
	else {
		k=e/360;s-=360*k;e-=360*k;
		unsigned difference=s-e;
		if (difference>360) s-=360*((difference-1)/360);
		if (s>=360) s-=360;
		e+=360;
	}
	*start=s;*end=e;return 0;
}

/* Numeric constants from original sine RVA 0x7b638 / cosine 0x7b098.
 * The quarter table and three asymmetric entries reconstruct both complete
 * tables. Source identities/hashes are in the adopted #76 / PR #77 reference;
 * this is its optional quantized numeric profile. */
static int32_t
sine(unsigned angle)
{
	static const int32_t quarter[]={
		0,285,571,857,1142,1427,1712,1996,2280,2563,2845,3126,3406,3685,3963,
		4240,4516,4790,5062,5334,5603,5871,6137,6401,6663,6924,7182,7438,7691,7943,
		8191,8438,8682,8923,9161,9397,9630,9860,10086,10310,10531,10748,10963,11173,11381,
		11585,11785,11982,12175,12365,12550,12732,12910,13084,13254,13420,13582,13740,13894,14043,
		14188,14329,14466,14598,14725,14848,14967,15081,15190,15295,15395,15491,15582,15668,15749,
		15825,15897,15964,16025,16082,16135,16182,16224,16261,16294,16321,16344,16361,16374,16381,16384,
	};
	angle%=360;
	if (angle==330) return -8192;
	if (angle<=90) return quarter[angle];
	if (angle<=180) return quarter[180-angle];
	if (angle<=270) return -quarter[angle-180];
	return -quarter[360-angle];
}

static int32_t
cosine(unsigned angle)
{
	angle%=360;
	if (angle==60 || angle==300) return 8192;
	return sine(angle+90);
}

static int64_t
floor14(int64_t value)
{
	return value>=0 ? value/16384 : -((-value+16383)/16384);
}

int
st_rip3_skew_build(struct st_rip3_skew_path *result,
    int64_t x, int64_t y, uint64_t rx, uint64_t ry,
    unsigned rotation, unsigned start, unsigned end, enum st_rip3_skew_kind kind)
{
	struct st_rip3_skew_path path={0};
	const int64_t limit=INT64_C(512)*INT32_MAX;
	int status;
	if (result==NULL || kind<ST_RIP3_SKEW_ARC || kind>ST_RIP3_SKEW_OVAL) return IO_ERROR_INVALID_ARGUMENT;
	if (x < -limit || x>limit || y < -limit || y>limit || rx>(uint64_t)limit || ry>(uint64_t)limit
	    || rotation>4095 || start>4095 || end>4095) return IO_ERROR_OUT_OF_RANGE;
	if (kind==ST_RIP3_SKEW_OVAL) {start=0;end=359;}
	else {
		status=st_rip3_skew_span(&start,&end);
		if (status!=0) return status;
	}
	path.start=start;path.end=end;
	size_t samples=(size_t)(end-start)+1;
	path.count=samples+(kind==ST_RIP3_SKEW_CHORD ? 1 : kind==ST_RIP3_SKEW_PIE ? 2 : 0);
	path.points=malloc(path.count*sizeof(*path.points));
	if (path.points==NULL) return IO_ERROR_OUT_OF_MEMORY;
	int64_t cr=cosine(rotation),sr=sine(rotation);
	for (size_t i=0; i<samples; i++) {
		unsigned angle=start+(unsigned)i;
		int64_t a=floor14((int64_t)rx*cosine(angle));
		int64_t b=floor14((int64_t)ry*sine(angle));
		path.points[i]=(struct st_rip3_vector){(double)(x+floor14(cr*a-sr*b)),(double)(y-floor14(sr*a+cr*b))};
	}
	if (kind==ST_RIP3_SKEW_PIE) path.points[samples++]=(struct st_rip3_vector){(double)x,(double)y};
	if (kind==ST_RIP3_SKEW_PIE || kind==ST_RIP3_SKEW_CHORD) path.points[samples]=path.points[0];
	*result=path;return 0;
}

void
st_rip3_skew_free(struct st_rip3_skew_path *path)
{
	if (path==NULL) return;
	free(path->points);*path=(struct st_rip3_skew_path){0};
}

int
st_rip3_marker_build(struct st_rip3_skew_path *result, unsigned marker,
    int64_t x, int64_t y, uint64_t width, uint64_t height, unsigned rotation)
{
	const int64_t limit = INT64_C(512) * INT32_MAX;
	if (result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (marker >= sizeof(marker_contours) / sizeof(marker_contours[0]) || rotation >= 360
	    || x < -limit || x > limit || y < -limit || y > limit
	    || width > (uint64_t)limit || height > (uint64_t)limit)
		return IO_ERROR_OUT_OF_RANGE;
	if (width == 0)
		width = 1;
	if (height == 0)
		height = 1;
	if (marker == 0)
		return st_rip3_skew_build(result, x, y, width / 2, height / 2,
		    rotation, 0, 360, ST_RIP3_SKEW_OVAL);

	struct st_rip3_skew_path path = {.count = marker_contours[marker].count};
	path.points = malloc(path.count * sizeof(*path.points));
	if (path.points == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	const int8_t (*source)[2] = marker_vertices + marker_contours[marker].offset;
	int64_t sx = (int64_t)width * 16384 / 100;
	int64_t sy = (int64_t)height * 16384 / 100;
	/* Table ordinates are already screen-space Y. Rotate counterclockwise
	 * after scaling, using the original two distinct Q14 floors. */
	int64_t cr = cosine(360 - rotation), sr = sine(360 - rotation);
	for (size_t i = 0; i < path.count; ++i) {
		int64_t dx = floor14(source[i][0] * sx);
		int64_t dy = floor14(source[i][1] * sy);
		path.points[i] = (struct st_rip3_vector){
		    (double)(x + floor14(cr * dx - sr * dy)),
		    (double)(y + floor14(sr * dx + cr * dy)),
		};
	}
	*result = path;
	return 0;
}

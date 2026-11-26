/* Atomic logical outline width/height snapshots for the RIP3 contract. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_outline_cache.h"
#include "st_rip3_outline_font_internal.h"
#include "io_error.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct st_rip3_outline_cache {
	uint64_t logical[256], height;
	int64_t steps[256], bbox_width;
	uint32_t dpi_x;
	unsigned spacing;
	bool bounding_box_spacing, valid;
};

int
st_rip3_outline_cache_create(struct st_rip3_outline_cache **result)
{
	if (result==NULL) return IO_ERROR_INVALID_ARGUMENT;
	struct st_rip3_outline_cache *cache=calloc(1,sizeof(*cache));
	if (cache==NULL) return IO_ERROR_OUT_OF_MEMORY;
	*result=cache;
	return 0;
}

void
st_rip3_outline_cache_destroy(struct st_rip3_outline_cache *cache)
{
	free(cache);
}

void
st_rip3_outline_cache_reset(struct st_rip3_outline_cache *cache)
{
	if (cache!=NULL) *cache=(struct st_rip3_outline_cache){0};
}

bool
st_rip3_outline_cache_valid(const struct st_rip3_outline_cache *cache)
{
	return cache!=NULL && cache->valid;
}

static int
step(int64_t *result, uint64_t device, uint64_t reference, unsigned spacing)
{
	int difference=(int)spacing-100;
	unsigned magnitude=(unsigned)(difference<0 ? -difference : difference);
	/* Divide before multiplying, retaining the remainder term, so even a
	 * forged very wide bbox cannot overflow an intermediate product. */
	uint64_t whole=reference/100, fraction=(reference%100)*magnitude/100;
	if (magnitude!=0 && whole>UINT64_MAX/magnitude) return IO_ERROR_OUT_OF_RANGE;
	uint64_t gap=whole*magnitude;
	if (gap>UINT64_MAX-fraction) return IO_ERROR_OUT_OF_RANGE;
	gap+=fraction;
	if (device>INT64_MAX || gap>INT64_MAX) return IO_ERROR_OUT_OF_RANGE;
	if (difference>=0) {
		if (gap>(uint64_t)INT64_MAX-device) return IO_ERROR_OUT_OF_RANGE;
		*result=(int64_t)(device+gap);
	}
	else *result=(int64_t)device-(int64_t)gap;
	return 0;
}

static int
set_spacing(struct st_rip3_outline_cache *cache, unsigned spacing)
{
	for (size_t code=0; code<256; code++) {
		uint64_t device=st_rip3_outline_device_extent(cache->logical[code],cache->dpi_x);
		int status=step(&cache->steps[code],device,
		    cache->bounding_box_spacing ? (uint64_t)cache->bbox_width : device,spacing);
		if (status!=0) return status;
	}
	cache->spacing=spacing;
	return 0;
}

int
st_rip3_outline_cache_prepare(struct st_rip3_outline_cache *cache,
    const struct st_rip3_outline_font *font, unsigned spacing)
{
	if (cache==NULL || font==NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (spacing==0 || spacing>4095 || font->bounds.xmax<font->bounds.xmin)
		return IO_ERROR_OUT_OF_RANGE;
	uint64_t span=(uint64_t)font->bounds.xmax-(uint64_t)font->bounds.xmin;
	if (span>INT64_MAX) return IO_ERROR_OUT_OF_RANGE;
	struct st_rip3_outline_cache value={.bbox_width=(int64_t)span,.dpi_x=font->dpi_x,
	    .bounding_box_spacing=(font->flags&0x400)!=0,.valid=true};
	int status=st_rip3_outline_font_metric_map(value.logical,&value.height,font);
	if (status==0) status=set_spacing(&value,spacing);
	if (status!=0) return status;
	*cache=value;
	return 0;
}

int
st_rip3_outline_cache_measure(struct st_rip3_outline_width *result,
    const struct st_rip3_outline_cache *cache, const uint8_t *text, size_t length)
{
	if (result==NULL || (length!=0 && text==NULL)) return IO_ERROR_INVALID_ARGUMENT;
	int64_t total=0;
	if (st_rip3_outline_cache_valid(cache)) {
		for (size_t i=0; i<length && text[i]!=0; i++) {
			int64_t delta=cache->steps[text[i]];
			if ((delta>0 && total>INT64_MAX-delta) || (delta<0 && total<INT64_MIN-delta))
				return IO_ERROR_OUT_OF_RANGE;
			total+=delta;
		}
	}
	/* Conversion to unsigned is defined modulo 2^64, even for negative sums.
	 * Decode the low WORD explicitly before narrowing to a representable s16. */
	int64_t word=(int64_t)((uint64_t)total&UINT64_C(65535));
	if (word>=32768) word-=65536;
	*result=(struct st_rip3_outline_width){.advance=total,.cursor=(int16_t)word};
	return 0;
}

uint64_t
st_rip3_outline_cache_height(const struct st_rip3_outline_cache *cache)
{
	return st_rip3_outline_cache_valid(cache) ? cache->height : 0;
}

static int
fit_width(int64_t *width, const struct st_rip3_outline_cache *metrics,
    unsigned shadow, const uint8_t *text, size_t length)
{
	struct st_rip3_outline_width measured;
	int status=st_rip3_outline_cache_measure(&measured,metrics,text,length);
	if (status!=0) return status;
	if (measured.advance>INT64_MAX-(int64_t)shadow) return IO_ERROR_OUT_OF_RANGE;
	*width=measured.advance+shadow;
	return 0;
}

static int
fit_size(struct st_rip3_outline_cache *metrics, struct st_rip3_outline_font *face,
    const struct st_rip3_frame *frame, unsigned points, unsigned spacing)
{
	int status=0;
	if (face->points!=points)
		status=st_rip3_outline_font_bind(face,&face->file,face->face,frame,points,face->flags,0);
	if (status==0) status=st_rip3_outline_cache_prepare(metrics,face,spacing);
	return status;
}

int
st_rip3_outline_cache_fit(struct st_rip3_outline_cache **cache,
    struct st_rip3_outline_font *font, unsigned *spacing, const struct st_rip3_frame *frame,
    unsigned shadow, uint64_t box_width, uint64_t box_height, const uint8_t *text, size_t length)
{
	const unsigned limit=4095;
	if (cache==NULL || font==NULL || spacing==NULL || frame==NULL || text==NULL || length==0)
		return IO_ERROR_INVALID_ARGUMENT;
	if (box_width==0 || box_height==0 || box_width>UINT64_MAX/limit
	    || box_height>UINT64_MAX/limit || font->orientation!=0 || shadow>limit)
		return IO_ERROR_OUT_OF_RANGE;

	struct st_rip3_outline_cache metrics;
	struct st_rip3_outline_font face=*font;
	int64_t width;
	int status=st_rip3_outline_cache_prepare(&metrics,&face,*spacing);
	if (status==0) status=fit_width(&width,&metrics,shadow,text,length);
	if (status!=0) return status;
	uint64_t height=metrics.height+shadow;
	if (width<=0 || height==0) return IO_ERROR_OUT_OF_RANGE;
	uint64_t size_x=face.points*box_width/(uint64_t)width;
	uint64_t size_y=face.points*box_height/height;
	uint64_t proposal=size_x<size_y ? size_x : size_y;
	unsigned points=proposal<limit ? (unsigned)proposal : limit;

	unsigned fitted=0;
	while (points!=0) {
		if (face.points!=points) {
			status=fit_size(&metrics,&face,frame,points,*spacing);
			if (status==0) status=fit_width(&width,&metrics,shadow,text,length);
			if (status!=0) return status;
			height=metrics.height+shadow;
		}
		if (width>0 && (uint64_t)width<=box_width && height!=0 && height<=box_height) {
			fitted=points;
			if (points==limit) break;
			points++;
		}
		else if (fitted!=0) break;
		else points--;
	}
	if (fitted==0) return IO_ERROR_OUT_OF_RANGE;
	if (face.points!=fitted) {
		status=fit_size(&metrics,&face,frame,fitted,*spacing);
		if (status==0) status=fit_width(&width,&metrics,shadow,text,length);
		if (status!=0) return status;
	}
	while ((uint64_t)width<box_width && metrics.spacing<limit) {
		unsigned next=metrics.spacing+5;
		status=set_spacing(&metrics,next>limit ? limit : next);
		if (status==0) status=fit_width(&width,&metrics,shadow,text,length);
		if (status!=0) return status;
	}
	while ((uint64_t)width>box_width) {
		status=set_spacing(&metrics,metrics.spacing-1);
		if (status==0) status=fit_width(&width,&metrics,shadow,text,length);
		if (status!=0) return status;
	}
	if (*cache==NULL) {
		status=st_rip3_outline_cache_create(cache);
		if (status!=0) return status;
	}
	**cache=metrics;
	*font=face;
	*spacing=metrics.spacing;
	return 0;
}

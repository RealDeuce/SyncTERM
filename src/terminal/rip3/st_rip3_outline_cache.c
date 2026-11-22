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
	unsigned dpi_x, spacing;
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
	    .spacing=spacing,.bounding_box_spacing=(font->flags&0x400)!=0,.valid=true};
	int status=st_rip3_outline_font_metric_map(value.logical,&value.height,font);
	if (status!=0) return status;
	for (size_t code=0; code<256; code++) {
		/* The validated u16/Q/DPI/H ranges keep this product in uint64_t. */
		uint64_t device=value.logical[code]*value.dpi_x/1440;
		status=step(&value.steps[code],device,value.bounding_box_spacing ? span : device,spacing);
		if (status!=0) return status;
	}
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

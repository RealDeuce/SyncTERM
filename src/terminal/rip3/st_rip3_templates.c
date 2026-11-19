/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_templates.h"
#include "io_error.h"

#include <stdlib.h>
#include <string.h>

struct slot { uint8_t *bytes; size_t length; bool required; };
struct st_rip3_templates { struct slot slots[36]; };

int
st_rip3_templates_create(struct st_rip3_templates **result)
{
	if (result==NULL) return IO_ERROR_INVALID_ARGUMENT;
	struct st_rip3_templates *templates=calloc(1,sizeof(*templates));
	if (templates==NULL) return IO_ERROR_OUT_OF_MEMORY;
	*result=templates;return 0;
}

void
st_rip3_templates_destroy(struct st_rip3_templates *templates)
{
	if (templates==NULL) return;
	for (unsigned i=0; i<36; i++) free(templates->slots[i].bytes);
	free(templates);
}

int
st_rip3_templates_set(struct st_rip3_templates *templates, unsigned number,
    const uint8_t *bytes, size_t length)
{
	if (templates==NULL || (bytes==NULL && length!=0)) return IO_ERROR_INVALID_ARGUMENT;
	if (number>=36 || length==SIZE_MAX) return IO_ERROR_OUT_OF_RANGE;
	const uint8_t *end=length==0 ? NULL : memchr(bytes,0,length);
	if (end!=NULL) length=(size_t)(end-bytes);
	uint8_t *copy=NULL;
	if (length!=0) {
		copy=malloc(length);
		if (copy==NULL) return IO_ERROR_OUT_OF_MEMORY;
		memcpy(copy,bytes,length);
	}
	free(templates->slots[number].bytes);
	templates->slots[number].bytes=copy;templates->slots[number].length=length;
	return 0;
}

int
st_rip3_templates_required(struct st_rip3_templates *templates, unsigned number, bool required)
{
	if (templates==NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (number>=36) return IO_ERROR_OUT_OF_RANGE;
	templates->slots[number].required=required;return 0;
}

int
st_rip3_templates_resolve(const struct st_rip3_templates *templates, unsigned number,
    struct st_rip3_template_bytes *result)
{
	if (templates==NULL || result==NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (number>=36) return IO_ERROR_OUT_OF_RANGE;
	const struct slot *slot=&templates->slots[number];
	if (slot->length==0 && slot->required) return IO_ERROR_NOT_FOUND;
	*result=(struct st_rip3_template_bytes){slot->bytes,slot->length};return 0;
}

static void
append(uint8_t result[4096], size_t *length, const uint8_t *bytes, size_t count)
{
	if (count>4096-*length) count=4096-*length;
	if (count!=0) memcpy(result+*length,bytes,count);
	*length+=count;
}

int
st_rip3_templates_chain(const struct st_rip3_templates *templates,
    const uint8_t *numbers, size_t count, const uint8_t *feed, size_t feed_length,
    uint8_t *result, size_t capacity, size_t *length, unsigned *missing)
{
	if (templates==NULL || numbers==NULL || result==NULL || length==NULL
	    || (feed_length!=0 && feed==NULL)) return IO_ERROR_INVALID_ARGUMENT;
	if (count==0 || count>36 || capacity<4096) return IO_ERROR_OUT_OF_RANGE;
	for (size_t i=0; i<count; i++) if (numbers[i]>=36) return IO_ERROR_OUT_OF_RANGE;
	uint8_t buffers[2][4096];
	size_t used=0;unsigned current=0;
	append(buffers[current],&used,feed,feed_length);
	for (size_t i=0; i<count; i++) {
		struct st_rip3_template_bytes value;
		int status=st_rip3_templates_resolve(templates,numbers[i],&value);
		if (status!=0) {
			if (status==IO_ERROR_NOT_FOUND && missing!=NULL) *missing=numbers[i];
			return status;
		}
		unsigned next=current^1;size_t produced=0;
		for (size_t at=0; at<value.length && produced<4096;) {
			if (value.length-at>=3 && memcmp(value.bytes+at,"$?$",3)==0) {
				append(buffers[next],&produced,buffers[current],used);at+=3;
			}
			else append(buffers[next],&produced,value.bytes+at++,1);
		}
		current=next;used=produced;
	}
	if (used!=0) memcpy(result,buffers[current],used);
	*length=used;return 0;
}

/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_resident_query.h"
#include "st_rip3_graphical_bytes.h"
#include "io_error.h"
#include <stdlib.h>
#include <string.h>
void
st_rip3_resident_query_clear(struct st_rip3_resident_query *query)
{
	if (query!=NULL) {free(query->bytes);*query=(struct st_rip3_resident_query){0};}
}
int
st_rip3_resident_query_copy(struct st_rip3_resident_query *destination,const struct st_rip3_resident_query *source)
{
	if (destination==NULL || source==NULL || (source->length!=0 && source->bytes==NULL)) return IO_ERROR_INVALID_ARGUMENT;
	uint8_t *copy=source->length==0 ? NULL : malloc(source->length);
	if (source->length!=0 && copy==NULL) return IO_ERROR_OUT_OF_MEMORY;
	if (source->length!=0) memcpy(copy,source->bytes,source->length);
	size_t length=source->length;st_rip3_resident_query_clear(destination);
	*destination=(struct st_rip3_resident_query){copy,length};return 0;
}
int
st_rip3_resident_query_set(struct st_rip3_resident_query *query,const uint8_t *bytes,size_t length,bool converted)
{
	if (query==NULL || (bytes==NULL && length!=0)) return IO_ERROR_INVALID_ARGUMENT;
	if (length==SIZE_MAX) return IO_ERROR_OUT_OF_RANGE;
	const uint8_t *end=length==0 ? NULL : memchr(bytes,0,length);
	if (end!=NULL) length=(size_t)(end-bytes);
	if (length==0) return IO_ERROR_INVALID_FORMAT;
	if (st_rip3_graphical_units(bytes,length,converted)>1023) return IO_ERROR_OUT_OF_RANGE;
	bool off=length==5;static const char sentinel[]="$OFF$";
	for (size_t i=0;off && i<5;i++) {
		uint8_t c=bytes[i];if (c>='a' && c<='z') c-='a'-'A';off=c==(uint8_t)sentinel[i];
	}
	if (off) {st_rip3_resident_query_clear(query);return 0;}
	const struct st_rip3_resident_query source={(uint8_t *)bytes,length};
	return st_rip3_resident_query_copy(query,&source);
}

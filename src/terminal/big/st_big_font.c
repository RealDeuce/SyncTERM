/* Borrowed immutable font registration, original BIG30 startup subset. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_font.h"
#include "io_error.h"
#include <string.h>
static int invalid(int *error, int status) { *error=-13; return status; }
int st_big_font_register(struct st_big_fonts *fonts, const uint8_t *data, size_t size, int *error, uint16_t *id)
{
	if (fonts==NULL || data==NULL || error==NULL || id==NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (size<2) return invalid(error,IO_ERROR_OUT_OF_RANGE);
	if (data[0]!=0x50 || data[1]!=0x4b) return invalid(error,IO_ERROR_PROTOCOL);
	size_t limit=size<65535?size:65535;
	const uint8_t *marker=memchr(data,0x1a,limit);
	if (marker==NULL) return invalid(error,IO_ERROR_OUT_OF_RANGE);
	size_t after=(size_t)(marker-data)+1;
	if (size-after<11) return invalid(error,IO_ERROR_OUT_OF_RANGE);
	if (data[after+8]<1 || data[after+10]>1) return invalid(error,IO_ERROR_PROTOCOL);
	for (size_t slot=0;slot<ST_BIG_FONT_SLOTS;++slot) {
		if (memcmp(fonts->slots[slot].name,data+after+2,4)!=0) continue;
		size_t header=(size_t)(data[after]|(unsigned)data[after+1]<<8);
		if (header>size) return invalid(error,IO_ERROR_OUT_OF_RANGE);
		fonts->slots[slot].body=data+header; fonts->slots[slot].size=size-header;
		*id=(uint16_t)(slot+1); return 0;
	}
	*error=-11; return IO_ERROR_PROTOCOL;
}
int st_big_fonts_init(struct st_big_fonts *fonts, int *error)
{
	if (fonts==NULL || error==NULL) return IO_ERROR_INVALID_ARGUMENT;
	static const uint8_t names[10][4]={{'T','R','I','P'},{'L','I','T','T'},{'S','A','N','S'},{'G','O','T','H'},
	    {'S','C','R','I'},{'S','I','M','P'},{'T','S','C','R'},{'L','C','O','M'},{'E','U','R','O'},{'B','O','L','D'}};
	memset(fonts,0,sizeof(*fonts)); fonts->count=10;
	for (size_t i=0;i<10;++i) memcpy(fonts->slots[i].name,names[i],4);
	static const unsigned order[]={4,2,3,1};
	for (size_t i=0;i<4;++i) {
		size_t size; const uint8_t *data=st_big_font_data(order[i],&size); uint16_t id;
		int status=st_big_font_register(fonts,data,size,error,&id);
		if (status!=0) return status;
		if (id!=order[i]) return invalid(error,IO_ERROR_PROTOCOL);
	}
	return 0;
}

/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_host_internal.h"
#include "st_rip3_image_style.h"
#include "st_rip3_ports.h"
#include "io_error.h"

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool
equal(const char *text, const char *word)
{
	for (size_t i = 0; ; i++) {
		unsigned char c = (unsigned char)text[i];
		if (c >= 'a' && c <= 'z') c = (unsigned char)(c-'a'+'A');
		if (c != (unsigned char)word[i]) return false;
		if (c == 0) return true;
	}
}

static int
coordinate(const char *text, int32_t *result)
{
	const char *digit = text;
	if (*digit == '-' || *digit == '+') digit++;
	if (*digit < '0' || *digit > '9') return IO_ERROR_INVALID_FORMAT;
	for (; *digit != 0; digit++) if (*digit < '0' || *digit > '9') return IO_ERROR_INVALID_FORMAT;
	char *end; errno = 0;
	intmax_t value = strtoimax(text,&end,10);
	if (errno == ERANGE || *end != 0 || value < INT32_MIN || value > INT32_MAX) return IO_ERROR_OUT_OF_RANGE;
	*result = (int32_t)value; return 0;
}

int
st_rip3_image_style_host_execute(const struct st_rip3_host_variables *host,
    const struct st_rip3_builtin_reference *reference, char *result, size_t capacity)
{
	if (strcmp(reference->name,"IMGSTYLE") != 0) return IO_ERROR_NOT_SUPPORTED;
	struct st_rip3_builtin_reference words;
	int normalization=st_rip3_builtin_reference_words(&words,reference);
	if (normalization!=0) return normalization;
	reference=&words;
	if (capacity == 0) return IO_ERROR_OUT_OF_RANGE;
	bool setting = reference->count > 1;
	if (setting && reference->count < 5) return IO_ERROR_INVALID_FORMAT;
	unsigned number = 0, current, flags = 0;
	bool use_current = reference->count == 0 || equal(reference->parameters[0].text,"CUR");
	int status = 0;
	if (!use_current) status = st_rip3_host_number(reference->parameters[0].text,0,35,&number);
	struct st_rip3_rect world = {0};
	if (status == 0 && setting) {
		int32_t values[4];
		for (size_t i = 0; status == 0 && i < 4; i++) status = coordinate(reference->parameters[i+1].text,values+i);
		if (status != 0) return status;
		world = (struct st_rip3_rect){values[0],values[1],values[2],values[3]};
		for (size_t i = 5; i < reference->count; i++) {
			const char *word = reference->parameters[i].text;
			if (equal(word,"ASPECT")) flags |= 1;
			else if (equal(word,"DELETE")) flags |= 2;
			else if (equal(word,"NOCLEAR")) flags |= 4;
			else if (equal(word,"USEPAL")) flags |= 8;
			else return IO_ERROR_INVALID_FORMAT;
		}
	}
	if (status != 0) return status;
	if (host->tables == NULL || host->tables->tables[ST_RIP3_TABLE_PORT].live == NULL) return IO_ERROR_NOT_SUPPORTED;
	struct st_rip3_ports *ports = host->tables->tables[ST_RIP3_TABLE_PORT].live;
	status = st_rip3_ports_current(ports,&current);
	if (status != 0) return status;
	if (use_current) number = current;
	struct st_rip3_port_info info;
	status = st_rip3_ports_info(ports,number,&info);
	if (status != 0) return status;
	if (!info.defined || !info.viewport_active) {
		if (capacity < 3) return IO_ERROR_OUT_OF_RANGE;
		memcpy(result,"-1",3); return 0;
	}
	if (setting && info.protected) { result[0] = 0; return 0; }
	struct st_rip3_frame frame;
	status = st_rip3_port_query_frame(host,ports,&frame);
	if (status != 0) return status;
	if (setting) {
		status = st_rip3_ports_switch(ports,number);
		if (status == 0) status = st_rip3_image_style_define(ports,number,&frame,&world,flags);
		(void)st_rip3_ports_switch(ports,current);
		if (status == 0) result[0] = 0;
		return status;
	}
	struct st_rip3_point first = {info.image.area.x0,info.image.area.y0}, last = {info.image.area.x1,info.image.area.y1};
	status = st_rip3_device_to_world(&frame,&first,&first);
	if (status == 0) status = st_rip3_device_to_world(&frame,&last,&last);
	if (status != 0) return status;
	char text[64];
	int length = snprintf(text,sizeof(text),"%" PRId32 ":%" PRId32 ":%" PRId32 ":%" PRId32 ":%u",
	    first.x,first.y,last.x,last.y,info.image.flags);
	if (length < 0 || (size_t)length >= sizeof(text)) return IO_ERROR_IO;
	if ((size_t)length >= capacity) return IO_ERROR_OUT_OF_RANGE;
	memcpy(result,text,(size_t)length+1); return 0;
}

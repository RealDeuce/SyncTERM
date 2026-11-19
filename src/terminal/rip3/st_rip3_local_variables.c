/* Local queries, separated from platform-specific input/device mechanisms. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_host_internal.h"
#include "st_rip3_local_variables.h"
#include "st_rip3_environments.h"
#include "io_error.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static bool
word_equal(const char *text, const char *uppercase)
{
	for (size_t i=0; ; i++) {
		unsigned char c=(unsigned char)text[i];
		if (c>='a' && c<='z') c=(unsigned char)(c-'a'+'A');
		if (c!=(unsigned char)uppercase[i]) return false;
		if (c==0) return true;
	}
}

static int
frame(const struct st_rip3_host_variables *host, struct st_rip3_frame *result)
{
	const struct st_rip3_environments *env=host->tables==NULL ? NULL : host->tables->tables[ST_RIP3_TABLE_ENV].live;
	if (env!=NULL) {
		unsigned current;int status=st_rip3_environments_current(env,&current);
		return status==0 ? st_rip3_environments_frame(env,current,result) : status;
	}
	if (host->frame==NULL) return IO_ERROR_NOT_SUPPORTED;
	struct st_rip3_frame checked=*host->frame;
	int status=st_rip3_frame_world(&checked,checked.world_width,checked.world_height);
	if (status==0) *result=checked;
	return status;
}

static int64_t
signed_word(int64_t value)
{
	int64_t low=(int64_t)((uint64_t)value&UINT64_C(65535));
	return low>=32768 ? low-65536 : low;
}

static int64_t
cell(int32_t position, int32_t origin, unsigned size, unsigned count)
{
	int64_t value=((int64_t)position-origin)/size;
	return value<0 ? 0 : (uint64_t)value>=count ? (int64_t)count-1 : value;
}

int
st_rip3_local_variables_execute(const struct st_rip3_host_variables *host,
    const struct st_rip3_builtin_reference *reference, char *result, size_t capacity)
{
	if (host==NULL || reference==NULL || result==NULL) return IO_ERROR_INVALID_ARGUMENT;
	bool shift=strcmp(reference->name,"SHIFT")==0,ctrl=strcmp(reference->name,"CTRL")==0;
	bool mouse=strcmp(reference->name,"MSTAT")==0,colors=strcmp(reference->name,"COLORS")==0;
	bool position=strcmp(reference->name,"XY")==0;
	if (!shift && !ctrl && !mouse && !colors && !position) return IO_ERROR_NOT_SUPPORTED;
	if (reference->count>(position ? 1u : 0u)) return IO_ERROR_INVALID_FORMAT;
	struct st_rip3_builtin_reference words;
	bool device=false,world=false;
	if (position && reference->count==1) {
		int status=st_rip3_builtin_reference_words(&words,reference);
		if (status!=0) return status;
		device=word_equal(words.parameters[0].text,"DEVICE");world=word_equal(words.parameters[0].text,"WORLD");
		if (!device && !world) return IO_ERROR_INVALID_FORMAT;
	}
	const struct st_rip3_local_queries *local=host->local;
	if (local==NULL) return IO_ERROR_NOT_SUPPORTED;
	char text[32];int length;
	if (shift || ctrl) {
		if (capacity<2) return IO_ERROR_OUT_OF_RANGE;
		if (local->modifier==NULL) return IO_ERROR_NOT_SUPPORTED;
		bool down=false;
		int status=local->modifier(local->context,shift ? ST_RIP3_MODIFIER_SHIFT : ST_RIP3_MODIFIER_CTRL,&down);
		if (status!=0) return status;
		text[0]=down ? '1' : '0';text[1]=0;length=1;
	}
	else if (mouse) {
		if (local->mouse_installed==NULL) return IO_ERROR_NOT_SUPPORTED;
		const char *answer=*local->mouse_installed ? "YES" : "NO";
		length=(int)strlen(answer);memcpy(text,answer,(size_t)length+1);
	}
	else if (colors) {
		if (local->colors==NULL) return IO_ERROR_NOT_SUPPORTED;
		length=snprintf(text,sizeof(text),"%u",*local->colors);
	}
	else {
		if (local->mouse_position==NULL) return IO_ERROR_NOT_SUPPORTED;
		struct st_rip3_point captured=*local->mouse_position;
		int64_t x=captured.x,y=captured.y;int padding=4;
		if (!device && !world && local->text_window_query) {
			const struct st_rip3_query_cells *window=local->text_window;
			if (window==NULL) return IO_ERROR_NOT_SUPPORTED;
			if (window->cell_width==0 || window->cell_height==0 || window->columns==0 || window->rows==0)
				return IO_ERROR_CONFLICT;
			x=cell(captured.x,window->origin.x,window->cell_width,window->columns);
			y=cell(captured.y,window->origin.y,window->cell_height,window->rows);padding=2;
		}
		else if (!device) {
			struct st_rip3_frame current;int status=frame(host,&current);
			if (status!=0) return status;
			/* The XY variable truncates signed products, unlike pixel mapping's
			 * containing-cell floor rule. Narrow only after this conversion. */
			x=x*current.world_width/current.device_width;y=y*current.world_height/current.device_height;
		}
		length=snprintf(text,sizeof(text),"%0*" PRId64 ":%0*" PRId64,padding,signed_word(x),padding,signed_word(y));
	}
	if (length<0 || (size_t)length>=sizeof(text)) return IO_ERROR_IO;
	if ((size_t)length>=capacity) return IO_ERROR_OUT_OF_RANGE;
	memcpy(result,text,(size_t)length+1);return 0;
}

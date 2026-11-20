/* Preserve the ANSI interpreter across a RIP3 window's fresh rendering state. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_cterm.h"
#include "cterm.h"
#include "io_error.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

int
st_cterm_inherit_window_interpreter(st_cterm_t *destination, const st_cterm_t *source)
{
	if (destination==NULL || source==NULL || destination==source
	    || destination->input_depth!=0 || source->input_depth!=0
	    || destination->width<1 || destination->height<1 || !destination->started
	    || destination->accumulator!=NULL || destination->string!=0)
		return IO_ERROR_INVALID_ARGUMENT;
	if (destination->emulation!=CTERM_EMULATION_ANSI_BBS || source->emulation!=CTERM_EMULATION_ANSI_BBS
	    || destination->graphics_mode!=ST_CTERM_GRAPHICS_RIP_3 || source->graphics_mode!=ST_CTERM_GRAPHICS_RIP_3
	    || destination->rip_engine!=NULL || source->rip_engine!=NULL)
		return IO_ERROR_NOT_SUPPORTED;
	/* These optional media/download payloads carry resources beyond ordinary
	 * ANSI syntax. Retain their existing owner rather than copying half a
	 * payload and destroying its backing state during a definition. */
	if (source->sixel!=SIXEL_INACTIVE || source->macro!=MACRO_INACTIVE
	    || source->music!=0 || source->font_size!=0 || source->seq_trailing_remain!=0)
		return IO_ERROR_NOT_SUPPORTED;
	if (source->tab_count<0 || source->vtab_count<0 || source->tab_width<0
	    || (source->tab_count!=0 && source->tabs==NULL)
	    || (source->vtab_count!=0 && source->vtabs==NULL)
	    || (source->strbuflen!=0 && source->strbuf==NULL) || source->strbuflen==SIZE_MAX
	    || (source->strbuf!=NULL && source->strbuflen>=source->strbufsize))
		return IO_ERROR_INVALID_ARGUMENT;
	int first=source->tab_width==0 ? 0 : (source->tab_width-1)/8+1;
	int extra=destination->width>source->tab_width && source->default_tabs
	    ? (destination->width-1)/8+1-first : 0;
	size_t count=(size_t)source->tab_count+(size_t)extra;
	if (count>INT_MAX || count>SIZE_MAX/sizeof(int) || (size_t)source->vtab_count>SIZE_MAX/sizeof(int))
		return IO_ERROR_OUT_OF_RANGE;
	int *tabs=NULL,*vtabs=NULL;char *string=NULL;
	if (count!=0) tabs=malloc(count*sizeof(*tabs));
	if (source->vtab_count!=0) vtabs=malloc((size_t)source->vtab_count*sizeof(*vtabs));
	if (source->strbuf!=NULL) string=malloc(source->strbuflen+1);
	if ((count!=0 && tabs==NULL) || (source->vtab_count!=0 && vtabs==NULL)
	    || (source->strbuf!=NULL && string==NULL)) {
		free(tabs);free(vtabs);free(string);return IO_ERROR_OUT_OF_MEMORY;
	}
	if (source->tab_count!=0) memcpy(tabs,source->tabs,(size_t)source->tab_count*sizeof(*tabs));
	for (int i=0;i<extra;i++) tabs[source->tab_count+i]=1+8*(first+i);
	if (source->vtab_count!=0) memcpy(vtabs,source->vtabs,(size_t)source->vtab_count*sizeof(*vtabs));
	if (string!=NULL) {
		memcpy(string,source->strbuf,source->strbuflen);string[source->strbuflen]=0;
	}
	/* No destination mutation or received-input callback precedes successful
	 * allocation of every owned buffer. The source remains independently usable. */
	free(destination->tabs);free(destination->vtabs);free(destination->strbuf);
	destination->tabs=tabs;destination->tab_count=(int)count;
	destination->tab_width=source->tab_width>destination->width ? source->tab_width : destination->width;
	destination->default_tabs=source->default_tabs;
	destination->vtabs=vtabs;destination->vtab_count=source->vtab_count;
	destination->strbuf=string;destination->strbuflen=source->strbuflen;
	destination->strbufsize=string==NULL ? 0 : source->strbuflen+1;
	destination->sequence=source->sequence;destination->string=source->string;
	destination->escbufsz=source->escbufsz;
	memcpy(destination->escbuf,source->escbuf,sizeof(destination->escbuf));
	destination->seq_key=source->seq_key;destination->seq_phase=source->seq_phase;
	memcpy(destination->seq_params,source->seq_params,sizeof(destination->seq_params));
	memcpy(destination->seq_param_int,source->seq_param_int,sizeof(destination->seq_param_int));
	destination->seq_param_len=source->seq_param_len;destination->seq_param_count=source->seq_param_count;
	destination->seq_cur_value=source->seq_cur_value;
	destination->seq_cur_has_digits=source->seq_cur_has_digits;destination->seq_cur_has_sub=source->seq_cur_has_sub;
	destination->seq_overflow=source->seq_overflow;
	memcpy(destination->seq_consumed,source->seq_consumed,sizeof(destination->seq_consumed));
	destination->seq_consumed_any=source->seq_consumed_any;
	/* Completed handlers build these aliases afresh from seq_params. Never
	 * retain a pointer into the source object's array across its destruction. */
	memset(destination->seq_param_strs,0,sizeof(destination->seq_param_strs));
	destination->accumulator=source->accumulator;destination->saved_accumulator=source->saved_accumulator;
	destination->wire_encoding=source->wire_encoding;destination->screen.codepage=source->screen.codepage;
	/* A fresh RIP3 definition's saved (0,0) is one-based (1,1) to CTerm. */
	destination->save_xpos=destination->save_ypos=1;
	return 0;
}

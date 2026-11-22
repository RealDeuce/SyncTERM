/* Graphics metric control/width path. No drawing or cursor update is performed. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_text_metric.h"
#include "st_rip3_command_internal.h"
#include "st_rip3_text_commands.h"
#include "st_rip3_host_variables.h"
#include "st_rip3_environments.h"
#include "st_rip3_styles.h"
#include "st_rip3_outline_cache.h"
#include "st_rip3_outline_font.h"
#include "st_rip3_outline_layout.h"
#include "st_rip3_graphical_bytes.h"
#include "io_error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool
sentinel(const uint8_t *bytes,size_t length)
{
	static const char name[]="$TEXTDATA$";
	if (length!=sizeof(name)-1) return false;
	for (size_t i=0;i<length;i++) {
		uint8_t c=bytes[i];if (c>='a' && c<='z') c-='a'-'A';
		if (c!=(uint8_t)name[i]) return false;
	}
	return true;
}

int
st_rip3_metric_argument_prepare(const struct st_rip3_command_host *host,
    const uint8_t *command,size_t length,struct st_rip3_metric_argument *argument,size_t *offset)
{
	if (host==NULL || command==NULL || argument==NULL || offset==NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (length<7 || command[0]!='r') return IO_ERROR_INVALID_FORMAT;
	struct st_rip3_environment environment;unsigned number;
	int status=st_rip3_command_environment(host,&number,&environment);
	uint64_t mode,domain;
	if (status==0) status=st_rip3_command_field(&mode,command+1,1,environment.base_math);
	if (status==0) status=st_rip3_command_field(&domain,command+2,1,environment.base_math);
	if (status!=0) return status;
	if (mode>3 || domain>1) return IO_ERROR_OUT_OF_RANGE;
	const uint8_t *text=command+7;size_t count=length-7;
	const uint8_t *end=count==0 ? NULL : memchr(text,0,count);
	if (end!=NULL) count=(size_t)(end-text);
	*argument=(struct st_rip3_metric_argument){(unsigned)mode,(unsigned)domain,sentinel(text,count),count==0};
	*offset=7;return 0;
}

static int
word(uint64_t value)
{
	unsigned low=(unsigned)(value&65535u);
	return low>=32768 ? (int)low-65536 : (int)low;
}

static int
world_value(int value,uint32_t world,uint32_t device)
{
	int64_t converted=(int64_t)value*world/device;
	return word((uint64_t)converted);
}

/* Metric initialization has no string/ink operation. A failed outline
 * realization retains the existing context's complete logical width snapshot;
 * its documented caller proceeds with metric queries despite that failure. */
static void
prepare_outline(const struct st_rip3_command_host *host,const struct st_rip3_style *style,unsigned environment)
{
	struct st_rip3_frame frame;struct st_rip3_outline_layout layout;struct st_rip3_outline_font font;
	int status=st_rip3_command_frame(host,environment,&frame);
	if (status==0) status=st_rip3_outline_layout_prepare(&layout,&style->outline,&frame);
	if (status==0) status=st_rip3_command_outline_font(&font,host->text,&style->outline,&frame,layout.font_orientation);
	if (status==0 && host->text->outline_cache==NULL) status=st_rip3_outline_cache_create(&host->text->outline_cache);
	if (status==0) (void)st_rip3_outline_cache_prepare(host->text->outline_cache,&font,layout.spacing);
}

int
st_rip3_metric_complete(struct st_rip3_metric_state *state,
    const struct st_rip3_command_host *host,const struct st_rip3_metric_argument *argument,
    const uint8_t *bytes,size_t length,bool converted,const struct st_rip3_host_sink *sink)
{
	if (state==NULL || host==NULL || argument==NULL || sink==NULL || sink->reply==NULL
	    || (bytes==NULL && length!=0)) return IO_ERROR_INVALID_ARGUMENT;
	if (argument->mode>3 || argument->domain>1) return IO_ERROR_OUT_OF_RANGE;
	if (argument->retained) {
		/* The initial sentinel/reset policy is not specified yet (#130). */
		if (!state->defined) return IO_ERROR_NOT_SUPPORTED;
	}
	else {
		length=st_rip3_graphical_prefix(bytes,length,1022,converted);
		const uint8_t *end=length==0 ? NULL : memchr(bytes,0,length);
		if (end!=NULL) length=(size_t)(end-bytes);
		if (length!=0) memcpy(state->bytes,bytes,length);
		state->bytes[length]=0;state->length=length;state->defined=true;
	}
	/* Height/axis and retained-state reset rules need #130's portable
	 * font contract. Consume unsupported modes locally after their already
	 * completed expansion, without inventing a bbox/line-height reply. */
	if (argument->mode==1 || argument->mode==3) return IO_ERROR_NOT_SUPPORTED;
	if (host->text==NULL || host->styles==NULL) return IO_ERROR_NOT_SUPPORTED;
	struct st_rip3_style style;unsigned number,environment_number;
	struct st_rip3_environment environment;struct st_rip3_frame device;
	int status=st_rip3_styles_current(host->styles,&number);
	if (status==0) status=st_rip3_styles_get(host->styles,number,&style);
	if (status==0) status=st_rip3_command_environment(host,&environment_number,&environment);
	if (status==0) status=st_rip3_environments_device(host->environments,&device);
	if (status!=0) return status;
	bool outline=style.font_mode==ST_RIP3_FONT_OUTLINE;
	if (outline && argument->literal_empty) return 0;
	if (outline) prepare_outline(host,&style,environment_number);
	const uint8_t *glyphs=state->bytes;size_t count=state->length;uint8_t *owned=NULL;
	if (converted) {
		status=st_rip3_command_expanded_glyphs(host->text,state->bytes,state->length,&glyphs,&count,&owned);
		if (status!=0) return status;
	}
	struct st_rip3_classic_font builtin;
	const struct st_rip3_classic_font *font=host->text->font;
	if (!outline && (font==NULL || font->number!=style.font.font)) {
		status=st_rip3_classic_font_builtin_device(&builtin,device.device_width,device.device_height,style.font.font);
		font=&builtin;
	}
	/* One bounded reply is built before the callback, including list modes;
	 * later preparation/format failure must never emit a partial metric. */
	char output[7*1022+1];size_t used=0;
	size_t results=argument->mode==0 ? 1 : count;
	for (size_t i=0;status==0 && i<results;i++) {
		const uint8_t *text=glyphs+(argument->mode==0 ? 0 : i);
		size_t size=argument->mode==0 ? count : 1;
		int value;
		if (outline) {
			struct st_rip3_outline_width width;
			status=st_rip3_outline_cache_measure(&width,host->text->outline_cache,text,size);value=status==0 ? width.cursor : 0;
		}
		else {
			struct st_rip3_classic_text_plan plan;const struct st_rip3_text_position zero={0,0};
			status=st_rip3_classic_text_plan(font,&style.font,text,size,&zero,&plan);value=status==0 ? word(plan.integer_advance) : 0;
		}
		if (status!=0) break;
		if (argument->domain==0) value=world_value(value,environment.world_width,device.device_width);
		int written=snprintf(output+used,sizeof(output)-used,"%s%d",i==0 ? "" : ":",value);
		if (written<0 || (size_t)written>=sizeof(output)-used) status=IO_ERROR_OUT_OF_RANGE;
		else used+=(size_t)written;
	}
	if (status==0) {output[used++]='\r';sink->reply(sink->context,(const uint8_t *)output,used);}
	free(owned);return status;
}

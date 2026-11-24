/* Button-style field widths, device units, palette colors and recovery. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_commands.h"
#include "st_rip3_button_styles.h"
#include "st_rip3_environments.h"
#include "st_rip3_number.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x) do {if (!(x)) {fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}} while (0)
static void diagnostic(void *context, enum st_rip3_command_diagnostic kind) {(void)kind;++*(unsigned *)context;}

static size_t
definition(uint8_t *bytes, unsigned radix, unsigned xy, const uint64_t values[16])
{
	size_t at=2;bytes[0]='1';bytes[1]='B';
	for (unsigned i=0;i<16;i++) {
		size_t width=i<2 ? xy : i==3 ? 4 : i==14 ? 1 : i==15 ? 5 : 2;
		if (st_rip3_number_encode((char *)bytes+at,128-at,values[i],width,radix)!=0) return 0;
		at+=width;
	}
	return at;
}

int
main(void)
{
	struct st_rip3_command_host host={0};unsigned errors=0;
	const struct st_rip3_command_sink sink={&errors,diagnostic};
	CHECK(st_rip3_environments_create(&host.environments,ST_RIP3_CANVAS_640_480)==0
	    && st_rip3_button_styles_create(&host.button_styles)==0);
	for (unsigned radix=36;radix<=64;radix+=28) for (unsigned xy=2;xy<=5;xy++) {
		struct st_rip3_environment env;
		CHECK(st_rip3_environments_get(host.environments,0,&env)==0);
		env.base_math=radix;env.coordinate_size=xy;env.world_width=1280;env.world_height=1920;env.rgb_bits=8;
		CHECK(st_rip3_environments_set(host.environments,0,&env)==0);
		uint64_t values[16]={64,32,2,769,16,255,0,15,8,7,35,255,15,7,35,0};
		uint8_t bytes[128];size_t size=definition(bytes,radix,xy,values);
		CHECK(size!=0);
		st_rip3_commands_process(&host,&sink,bytes,size);
		CHECK(errors==0);
		struct st_rip3_button_style value;
		CHECK(st_rip3_button_styles_get(host.button_styles,0,&value)==0);
		CHECK(value.width==32 && value.height==8 && value.bevel==8 && value.flags==769
		    && value.orientation==2 && value.group==35 && value.flags2==255);
		const uint8_t colors[]={255,0,15,8,7,15,7};
		CHECK(memcmp(value.colors,colors,sizeof(colors))==0);
		/* Changing WORLD must not rescale a previously defined style. */
		env.world_width=640;env.world_height=480;
		CHECK(st_rip3_environments_set(host.environments,0,&env)==0);
		CHECK(st_rip3_button_styles_get(host.button_styles,0,&value)==0
		    && value.width==32 && value.height==8 && value.bevel==8);
		values[3]=65537; /* Store low WORD; accept multiple/inapplicable flag bits. */
		size=definition(bytes,radix,xy,values);
		st_rip3_commands_process(&host,&sink,bytes,size);
		CHECK(errors==0 && st_rip3_button_styles_get(host.button_styles,0,&value)==0
		    && value.flags==1 && value.bevel==0);
	}
	struct st_rip3_environment env;
	CHECK(st_rip3_environments_get(host.environments,0,&env)==0);
	env.base_math=36;env.coordinate_size=2;
	CHECK(st_rip3_environments_set(host.environments,0,&env)==0);
	st_rip3_commands_process(&host,&sink,(const uint8_t *)"2B51",4);
	uint64_t values[16]={64,32,2,768,16,15,0,15,8,7,0,0,15,7,35,0};
	uint8_t bytes[128];size_t size=definition(bytes,36,2,values);
	st_rip3_commands_process(&host,&sink,bytes,size);
	CHECK(errors==1);
	struct st_rip3_button_style value;
	CHECK(st_rip3_button_styles_get(host.button_styles,5,&value)==0 && value.width==0 && value.bevel==3);
	CHECK(st_rip3_button_styles_protect(host.button_styles,5,false)==0);
	for (unsigned i=0;i<4;i++) {
		uint64_t invalid[16];memcpy(invalid,values,sizeof(invalid));
		invalid[i==0 ? 2 : i==1 ? 5 : i==2 ? 10 : 11]=i==0 ? 5 : i==2 ? 36 : 256;
		size=definition(bytes,36,2,invalid);st_rip3_commands_process(&host,&sink,bytes,size);
		CHECK(errors==i+2);
	}
	st_rip3_commands_process(&host,&sink,(const uint8_t *)"2BZ?",4);
	CHECK(errors==6);
	st_rip3_commands_process(&host,&sink,(const uint8_t *)"2B7.",4);
	unsigned current;
	CHECK(errors==6 && st_rip3_button_styles_current(host.button_styles,&current)==0 && current==7);
	size=definition(bytes,36,2,values);
	st_rip3_commands_process(&host,&sink,bytes,size-5); /* Only the reserve is optional. */
	CHECK(errors==6 && st_rip3_button_styles_get(host.button_styles,7,&value)==0
	    && value.width==64 && value.height==32 && value.bevel==16);
	env.world_width=0;
	CHECK(st_rip3_environments_set(host.environments,0,&env)==0);
	st_rip3_commands_process(&host,&sink,bytes,size);
	CHECK(errors==7 && st_rip3_button_styles_get(host.button_styles,7,&value)==0 && value.width==64);
	st_rip3_commands_process(&host,&sink,(const uint8_t *)"2B80",4);
	CHECK(errors==7 && st_rip3_button_styles_current(host.button_styles,&current)==0 && current==8);
	struct st_rip3_button_style_info source, target;
	st_rip3_commands_process(&host,&sink,(const uint8_t *)"2B75",4);
	CHECK(errors==7 && st_rip3_button_styles_current(host.button_styles,&current)==0 && current==7);
	CHECK(st_rip3_button_styles_info(host.button_styles,8,&source)==0 && source.protected
	    && st_rip3_button_styles_info(host.button_styles,7,&target)==0 && target.protected);
	st_rip3_commands_process(&host,&sink,(const uint8_t *)"2B7F",4);
	CHECK(errors==7 && st_rip3_button_styles_info(host.button_styles,7,&target)==0 && !target.protected);
	st_rip3_commands_process(&host,&sink,(const uint8_t *)"2B0.",4);
	st_rip3_commands_process(&host,&sink,(const uint8_t *)"2B84",4);
	CHECK(errors==8 && st_rip3_button_styles_current(host.button_styles,&current)==0 && current==8
	    && st_rip3_button_styles_info(host.button_styles,0,&source)==0 && !source.protected);
	st_rip3_button_styles_destroy(host.button_styles);st_rip3_environments_destroy(host.environments);return 0;
}

/* Independent X3/clamp/custom-cache/metric/failure expectations, BIG28/53. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_text.h"
#include "io_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
struct fixture { struct st_big_state s; size_t calls; int error,mutation; uint16_t args[4]; };
static void init(struct fixture *f)
{
	memset(f,0,sizeof(*f)); st_big_state_init(&f->s);
	f->s.bgi.text_size=3; f->s.bgi.error=-11;
	f->s.bgi.text_header[8]=21; f->s.bgi.text_header[10]=249;
	f->s.bgi.text_scaled[0]=71; f->s.bgi.text_scaled[1]=72;
}
static int device(void *context, const uint16_t args[4], uint16_t dimensions[2])
{
	struct fixture *f=context; ++f->calls; memcpy(f->args,args,sizeof(f->args));
	CHECK(f->s.bgi.text_size==3 && f->s.bgi.saved_ratio[0]==1);
	dimensions[0]=91; dimensions[1]=92;
	if (f->mutation==1) f->s.bgi.text_control=1;
	if (f->mutation==2) { f->s.bgi.text_header[8]=6; f->s.bgi.text_header[10]=254; }
	return f->error;
}
static void clamp_and_normal(void)
{
	for (size_t part=0;part<4;++part) for (unsigned word=0;word<65536;++word) {
		struct fixture f; init(&f); uint16_t r[4]={2,1,3,2}; r[part]=(uint16_t)word;
		struct st_big_state expected=f.s;
		for (size_t i=0;i<4;++i) expected.bgi.saved_ratio[i]=expected.bgi.effective_ratio[i]=r[i]>=1 && r[i]<=4?r[i]:1;
		expected.bgi.text_cached[0]=expected.bgi.saved_ratio[1]; expected.bgi.text_cached[1]=expected.bgi.saved_ratio[2];
		expected.bgi.text_selected_size=expected.bgi.text_size=0;
		expected.bgi.text_scaled[0]=(uint16_t)(28u*expected.bgi.saved_ratio[0]/expected.bgi.saved_ratio[1]);
		expected.bgi.text_scaled[1]=(uint16_t)(28u*expected.bgi.saved_ratio[2]/expected.bgi.saved_ratio[3]);
		CHECK(st_big_text_x3(&f.s,r,NULL)==0 && memcmp(&expected,&f.s,sizeof(expected))==0);
	}
}
static void public_validation_and_raw_values(void)
{
	const uint16_t accepted[]={2,1,3,2};
	for (size_t axis=0;axis<2;++axis) for (unsigned maximum=0;maximum<5;++maximum) {
		struct fixture f; init(&f); if (axis==0) f.s.bgi.width=maximum+1; else f.s.bgi.height=maximum+1;
		struct st_big_state before=f.s;
		bool accept=axis==0?maximum>=2:maximum>=3;
		CHECK(st_big_text_custom(&f.s,accepted,NULL)==0 && f.s.bgi.text_size==0 && f.s.bgi.error==-11);
		if (!accept) { before.bgi.text_size=0; CHECK(memcmp(&before,&f.s,sizeof(before))==0); }
		else CHECK(f.s.bgi.saved_ratio[0]==2 && f.s.bgi.text_scaled[0]==56 && f.s.bgi.text_scaled[1]==42);
	}
	struct fixture f; init(&f); const uint16_t zero[]={2,0,3,0};
	CHECK(st_big_text_custom(&f.s,zero,NULL)==0 && f.s.bgi.saved_ratio[1]==0 && f.s.bgi.saved_ratio[3]==0);
	CHECK(f.s.bgi.effective_ratio[1]==1 && f.s.bgi.effective_ratio[3]==1 && f.s.bgi.text_scaled[0]==56 && f.s.bgi.text_scaled[1]==84);
	init(&f); f.s.bgi.width=65536; f.s.bgi.height=65536; const uint16_t high[]={65535,65535,65535,65535};
	CHECK(st_big_text_custom(&f.s,high,NULL)==0 && f.s.bgi.saved_ratio[0]==32767 && f.s.bgi.saved_ratio[2]==65535);
	CHECK(f.s.bgi.text_scaled[0]==13 && f.s.bgi.text_scaled[1]==28);
	init(&f); f.s.bgi.state=0; CHECK(st_big_text_custom(&f.s,accepted,NULL)==IO_ERROR_PROTOCOL && f.s.bgi.error==-1 && f.s.bgi.text_size==3);
	f.s.bgi.width=1; f.s.bgi.error=-11; CHECK(st_big_text_custom(&f.s,accepted,NULL)==0 && f.s.bgi.error==-11 && f.s.bgi.text_size==0);
}
static void dispatch_and_metric_prefixes(void)
{
	const uint16_t r[]={2,1,3,2};
	for (unsigned bypass=0;bypass<2;++bypass) {
		struct fixture f; init(&f); f.s.bgi.text_control=(uint8_t)bypass;
		struct st_big_state before=f.s; const struct st_big_text_sink sink={&f,device};
		CHECK(st_big_text_custom(&f.s,r,&sink)==0 && f.calls==1 && memcmp(r,f.args,sizeof(f.args))==0);
		if (bypass) { before.bgi.text_size=0; CHECK(memcmp(&before,&f.s,sizeof(before))==0); }
		else CHECK(f.s.bgi.text_cached[0]==91 && f.s.bgi.text_cached[1]==92 && f.s.bgi.saved_ratio[0]==2 && f.s.bgi.saved_ratio[1]==1 && f.s.bgi.saved_ratio[2]==3 && f.s.bgi.saved_ratio[3]==2);
	}
	for (int mutation=1;mutation<=2;++mutation) {
		struct fixture f; init(&f); f.mutation=mutation; const struct st_big_text_sink sink={&f,device};
		CHECK(st_big_text_custom(&f.s,r,&sink)==0 && f.calls==1 && f.s.bgi.saved_ratio[0]==2);
		CHECK(f.s.bgi.text_scaled[0]==(mutation==1?56:16) && f.s.bgi.text_scaled[1]==(mutation==1?42:12));
	}
	for (unsigned kind=0;kind<2;++kind) {
		struct fixture f; init(&f); f.error=kind?IO_ERROR_IN_PROGRESS:IO_ERROR_IO;
		struct st_big_state before=f.s; const struct st_big_text_sink sink={&f,device};
		CHECK(st_big_text_custom(&f.s,r,&sink)==(kind?IO_ERROR_PROTOCOL:IO_ERROR_IO) && f.calls==1 && memcmp(&before,&f.s,sizeof(before))==0);
	}
	struct fixture f; init(&f); f.s.bgi.width=f.s.bgi.height=65536;
	const uint16_t bad_metric[]={2,1,65535,1};
	CHECK(st_big_text_custom(&f.s,bad_metric,NULL)==IO_ERROR_OUT_OF_RANGE && f.s.bgi.text_size==3 && f.s.bgi.text_scaled[0]==56 && f.s.bgi.text_scaled[1]==72);
	CHECK(f.s.bgi.saved_ratio[2]==65535 && f.s.bgi.effective_ratio[2]==65535 && f.s.bgi.text_cached[1]==65535);
	CHECK(st_big_text_custom(NULL,r,NULL)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_text_x3(&f.s,NULL,NULL)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_text_custom(&f.s,r,&(struct st_big_text_sink){0})==IO_ERROR_INVALID_ARGUMENT);
}
int main(void) { clamp_and_normal(); public_validation_and_raw_values(); dispatch_and_metric_prefixes(); return 0; }

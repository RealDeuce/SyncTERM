/* Public/custom DDO ratio installation, original BIG28/53. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_text.h"
#include "io_error.h"
#include <string.h>
int st_big_text_custom(struct st_big_state *s, const uint16_t ratios[4], const struct st_big_text_sink *sink)
{
	if (s==NULL || ratios==NULL || (sink!=NULL && sink->custom==NULL)) return IO_ERROR_INVALID_ARGUMENT;
	uint16_t r[4]; memcpy(r,ratios,sizeof(r));
	uint16_t max_x=(uint16_t)(s->bgi.width-1u), max_y=(uint16_t)(s->bgi.height-1u);
	if (r[0]>max_x || r[1]>max_x || r[2]>max_y || r[3]>max_y) {
		s->bgi.text_size=0; return 0;
	}
	if (s->bgi.state==0) { s->bgi.error=-1; return IO_ERROR_PROTOCOL; }
	/* The front's direct-device path skips all DDO caches/private ratios.
	 * Native owner changes occur only at serialized callback boundaries. */
	bool bypass=s->bgi.text_control!=0;
	uint16_t dimensions[2]={r[1],r[2]};
	if (sink!=NULL) {
		int status=sink->custom(sink->context,r,dimensions);
		if (status==IO_ERROR_IN_PROGRESS) status=IO_ERROR_PROTOCOL;
		if (status!=0) return status;
	}
	if (!bypass) {
		s->bgi.text_cached[0]=dimensions[0]; s->bgi.text_cached[1]=dimensions[1];
		/* Lower custom dispatch sets then clears AH's sign bit before
		 * saving its restored original arguments, even for raw API words. */
		s->bgi.saved_ratio[0]=(uint16_t)(r[0]&0x7fff);
		for (size_t i=1;i<4;++i) s->bgi.saved_ratio[i]=r[i];
		s->bgi.text_selected_size=0;
		s->bgi.effective_ratio[0]=s->bgi.saved_ratio[0];
		s->bgi.effective_ratio[2]=s->bgi.saved_ratio[2];
		s->bgi.effective_ratio[1]=s->bgi.saved_ratio[1]?s->bgi.saved_ratio[1]:1;
		s->bgi.effective_ratio[3]=s->bgi.saved_ratio[3]?s->bgi.saved_ratio[3]:1;
		uint8_t height=(uint8_t)(s->bgi.text_header[8]-s->bgi.text_header[10]);
		for (size_t axis=0;axis<2;++axis) {
			uint32_t scaled=(uint32_t)height*s->bgi.effective_ratio[2*axis]/s->bgi.effective_ratio[2*axis+1];
			if (scaled>UINT16_MAX) return IO_ERROR_OUT_OF_RANGE;
			s->bgi.text_scaled[axis]=(uint16_t)scaled;
		}
	}
	s->bgi.text_size=0;
	return 0;
}
int st_big_text_x3(struct st_big_state *s, const uint16_t ratios[4], const struct st_big_text_sink *sink)
{
	if (s==NULL || ratios==NULL) return IO_ERROR_INVALID_ARGUMENT;
	uint16_t r[4];
	for (size_t i=0;i<4;++i) r[i]=ratios[i]>=1 && ratios[i]<=4?ratios[i]:1;
	return st_big_text_custom(s,r,sink);
}

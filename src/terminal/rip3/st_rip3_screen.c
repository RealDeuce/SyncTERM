/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_screen.h"
#include "st_rip3_ports.h"
#include "st_rip3_palettes.h"
#include "st_rip3_backup.h"
#include "st_pixels.h"
#include "io_error.h"
#include <stdlib.h>
struct screen_snapshot {struct st_pixel_buffer *pixels;struct st_rip3_palette palette;};
static bool valid(const struct st_rip3_screen *s)
{
	return s!=NULL && s->ports!=NULL && s->palettes!=NULL && ((s->capture==NULL)==(s->restore==NULL));
}
static void destroy(void *object)
{
	struct screen_snapshot *s=object;if (s==NULL) return;
	st_pixel_buffer_destroy(s->pixels);free(s);
}
static int clone(const void *object,void **result)
{
	const struct st_rip3_screen *screen=object;
	if (!valid(screen) || result==NULL) return IO_ERROR_INVALID_ARGUMENT;
	struct screen_snapshot *s=calloc(1,sizeof(*s));if (s==NULL) return IO_ERROR_OUT_OF_MEMORY;
	int status=st_rip3_palettes_current_stored(screen->palettes,&s->palette);
	if (status==0) status=st_rip3_ports_screen_capture(screen->ports,&s->pixels);
	if (status==0 && screen->capture!=NULL) status=screen->capture(screen->context,s->pixels);
	if (status==0) for (size_t i=0;i<(size_t)s->pixels->width*s->pixels->height;i++) {
		uint32_t primary=s->pixels->primary[i],alternate=s->pixels->alternate==NULL ? primary : s->pixels->alternate[i];
		if ((!ST_PIXEL_COLOR_IS_RGB(primary) && primary>255) || (!ST_PIXEL_COLOR_IS_RGB(alternate) && alternate>255)) {status=IO_ERROR_OUT_OF_RANGE;break;}
	}
	if (status!=0) {destroy(s);return status;}
	*result=s;return 0;
}
static int restore(void *object,const void *saved)
{
	struct st_rip3_screen *screen=object;const struct screen_snapshot *s=saved;
	if (!valid(screen) || s==NULL) return IO_ERROR_INVALID_ARGUMENT;
	/* Palette is applied to the entry current now, not the saved selector.
	 * No table/port is revived; pixel-only COPY retains destination identity. */
	int status=st_rip3_palettes_restore_screen(screen->palettes,&s->palette);
	if (status==0) status=st_rip3_ports_screen_restore(screen->ports,s->pixels);
	if (status==0 && screen->restore!=NULL) status=screen->restore(screen->context,s->pixels,&s->palette);
	return status;
}
int st_rip3_screen_clear(struct st_rip3_screen *screen)
{
	if (!valid(screen)) return IO_ERROR_INVALID_ARGUMENT;
	struct st_rip3_port_info info;int status=st_rip3_ports_info(screen->ports,0,&info);
	return status==0 ? st_rip3_ports_screen_fill(screen->ports,&info.bounds,0) : status;
}
int st_rip3_screen_backup_create(struct st_rip3_backup **result)
{
	static const struct st_rip3_backup_ops ops={clone,restore,destroy};return st_rip3_backup_create(result,&ops);
}

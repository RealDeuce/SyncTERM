/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_button_styles.h"
#include "st_rip3_backup.h"
#include "io_error.h"

#include <stdlib.h>
#include <string.h>

struct button_entry {
	/* The current-reset defect writes nonboolean bytes here. Keep the owned
	 * protocol metadata, rather than performing the original invalid copy. */
	uint8_t in_use;
	bool protected;
	struct st_rip3_button_style value;
};
struct st_rip3_button_styles {unsigned current;struct button_entry entries[36];};

int
st_rip3_button_style_defaults(struct st_rip3_button_style *result)
{
	if (result==NULL) return IO_ERROR_INVALID_ARGUMENT;
	*result=(struct st_rip3_button_style){.orientation=2,.flags=768,.bevel=3,.colors={15,0,15,8,7,15,7}};
	return 0;
}

int
st_rip3_button_styles_create(struct st_rip3_button_styles **result)
{
	if (result==NULL) return IO_ERROR_INVALID_ARGUMENT;
	struct st_rip3_button_styles *styles=calloc(1,sizeof(*styles));
	if (styles==NULL) return IO_ERROR_OUT_OF_MEMORY;
	for (unsigned i=0;i<36;i++) (void)st_rip3_button_style_defaults(&styles->entries[i].value);
	styles->entries[0].in_use=1;*result=styles;return 0;
}

void st_rip3_button_styles_destroy(struct st_rip3_button_styles *styles) {free(styles);}

int
st_rip3_button_styles_current(const struct st_rip3_button_styles *styles, unsigned *result)
{
	if (styles==NULL || result==NULL) return IO_ERROR_INVALID_ARGUMENT;
	*result=styles->current;return 0;
}

int
st_rip3_button_styles_current_value(const struct st_rip3_button_styles *styles, struct st_rip3_button_style *result)
{
	if (styles==NULL || result==NULL) return IO_ERROR_INVALID_ARGUMENT;
	*result=styles->entries[styles->current].value;return 0;
}

int
st_rip3_button_styles_autoclip(struct st_rip3_button_styles *styles, uint16_t field_flags, uint16_t *result)
{
	if (styles==NULL || result==NULL) return IO_ERROR_INVALID_ARGUMENT;
	uint16_t flags=(uint16_t)(((unsigned)field_flags&~16u&0x7c37u)|1u);
	styles->entries[styles->current].value.flags=flags;*result=flags;return 0;
}

int
st_rip3_button_styles_info(const struct st_rip3_button_styles *styles, unsigned number,
    struct st_rip3_button_style_info *result)
{
	if (styles==NULL || result==NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (number>=36) return IO_ERROR_OUT_OF_RANGE;
	*result=(struct st_rip3_button_style_info){styles->entries[number].in_use!=0,styles->entries[number].protected};
	return 0;
}

int
st_rip3_button_styles_get(const struct st_rip3_button_styles *styles, unsigned number,
    struct st_rip3_button_style *result)
{
	if (styles==NULL || result==NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (number>=36) return IO_ERROR_OUT_OF_RANGE;
	if (!styles->entries[number].in_use) return IO_ERROR_NOT_FOUND;
	*result=styles->entries[number].value;return 0;
}

int
st_rip3_button_styles_set(struct st_rip3_button_styles *styles, unsigned number,
    const struct st_rip3_button_style *value)
{
	if (styles==NULL || value==NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (number>=36 || value->orientation>4 || value->group>=36 || !(value->flags&0x181)) return IO_ERROR_OUT_OF_RANGE;
	if (styles->entries[number].protected) return IO_ERROR_PERMISSION_DENIED;
	styles->entries[number].value=*value;
	if (!(value->flags&512)) styles->entries[number].value.bevel=0;
	styles->entries[number].in_use=1;
	return 0;
}

int
st_rip3_button_styles_select(struct st_rip3_button_styles *styles, unsigned number)
{
	if (styles==NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (number>=36) return IO_ERROR_OUT_OF_RANGE;
	struct button_entry *entry=&styles->entries[number];
	if (!entry->in_use) {
		(void)st_rip3_button_style_defaults(&entry->value);
		entry->in_use=1;entry->protected=false;
	}
	styles->current=number;return 0;
}

int
st_rip3_button_styles_protect(struct st_rip3_button_styles *styles, unsigned number, bool protect)
{
	if (styles==NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (number==0 || number>=36) return IO_ERROR_OUT_OF_RANGE;
	if (styles->entries[number].in_use) styles->entries[number].protected=protect;
	return 0;
}

int
st_rip3_button_styles_copy_entries(struct st_rip3_button_styles *styles, unsigned source,
    const unsigned *destinations, size_t count)
{
	if (styles==NULL || destinations==NULL || count==0) return IO_ERROR_INVALID_ARGUMENT;
	if (source>=36) return IO_ERROR_OUT_OF_RANGE;
	if (!styles->entries[source].in_use) return IO_ERROR_NOT_FOUND;
	for (size_t i=0;i<count;i++) {
		if (destinations[i]>=36) return IO_ERROR_OUT_OF_RANGE;
		if (destinations[i]==source) return IO_ERROR_INVALID_ARGUMENT;
		if (styles->entries[destinations[i]].protected) return IO_ERROR_PERMISSION_DENIED;
	}
	for (size_t i=0;i<count;i++) {
		struct button_entry *destination=&styles->entries[destinations[i]];
		destination->value=styles->entries[source].value;
		destination->in_use=1;destination->protected=false;
	}
	return 0;
}

int
st_rip3_button_styles_reset_entry(struct st_rip3_button_styles *styles, unsigned number)
{
	if (styles==NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (number>=36) return IO_ERROR_OUT_OF_RANGE;
	if (styles->entries[number].protected) return IO_ERROR_PERMISSION_DENIED;
	(void)st_rip3_button_style_defaults(&styles->entries[number].value);
	styles->entries[number].in_use=number==0;return 0;
}

int
st_rip3_button_styles_reset_table(struct st_rip3_button_styles *styles)
{
	if (styles==NULL) return IO_ERROR_INVALID_ARGUMENT;
	for (unsigned i=0;i<36;i++) if (!styles->entries[i].protected)
		(void)st_rip3_button_styles_reset_entry(styles,i);
	return 0;
}

int
st_rip3_button_styles_reset_current(struct st_rip3_button_styles *styles)
{
	if (styles==NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (styles->entries[styles->current].protected) return IO_ERROR_PERMISSION_DENIED;
	/* Published default-record bytes 2..27, copied over in-use[0..25]. The
	 * original writes zero into current first, then forces in-use[0] to one. */
	static const uint8_t in_use[26]={1,0,2,0,0,3,3,0,15,0,0,0,15,0,8,0,7,0,0,0,0,0,15,0,7,0};
	styles->current=0;
	for (unsigned i=0;i<sizeof(in_use);i++) styles->entries[i].in_use=in_use[i];
	return 0;
}

static int
clone(const void *object, void **result)
{
	if (object==NULL || result==NULL) return IO_ERROR_INVALID_ARGUMENT;
	struct st_rip3_button_styles *copy=malloc(sizeof(*copy));
	if (copy==NULL) return IO_ERROR_OUT_OF_MEMORY;
	*copy=*(const struct st_rip3_button_styles *)object;*result=copy;return 0;
}

static int
restore(void *object, const void *saved)
{
	if (object==NULL || saved==NULL) return IO_ERROR_INVALID_ARGUMENT;
	*(struct st_rip3_button_styles *)object=*(const struct st_rip3_button_styles *)saved;return 0;
}

int
st_rip3_button_styles_backup_create(struct st_rip3_backup **result)
{
	static const struct st_rip3_backup_ops ops={clone,restore,free};
	return st_rip3_backup_create(result,&ops);
}

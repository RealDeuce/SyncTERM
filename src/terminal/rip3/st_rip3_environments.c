/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>

#include "io_error.h"
#include "st_rip3_backup.h"
#include "st_rip3_environments.h"

struct environment_entry {
	struct st_rip3_environment_info info;
	struct st_rip3_environment      data;
};

struct st_rip3_environments {
	struct st_rip3_frame     device;
	unsigned                 current;
	uint32_t                 playback_rate;
	enum st_rip3_charset     shared_charset;
	bool                     scene_arrow_pending;
	struct environment_entry entries[36];
};

int
st_rip3_environment_defaults(struct st_rip3_environment *result)
{
	if (result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	*result = (struct st_rip3_environment) {.world_width = 640,
	    .world_height                                    = 350,
	    .base_math                                       = 36,
	    .coordinate_size                                 = 2};
	return 0;
}

static bool
valid(const struct st_rip3_environment *value)
{
	return value->graphics_style < 36 && value->button_style < 36 && value->port < 36
	       && value->text_window < 36 && value->palette < 36 && value->world_width <= 65535
	       && value->world_height <= 65535 && (value->base_math == 36 || value->base_math == 64)
	       && value->coordinate_size >= 2 && value->coordinate_size <= 5 && value->rgb_bits <= 8
	       && value->mouse_pointer <= 7 && value->charset >= ST_RIP3_CHARSET_UNSET
	       && value->charset <= ST_RIP3_CHARSET_KANJI;
}

int
st_rip3_environments_create(struct st_rip3_environments **result, enum st_rip3_canvas canvas)
{
	struct st_rip3_frame frame;
	int                  status = st_rip3_frame_init(&frame, canvas);
	return status != 0 ? status
	                   : st_rip3_environments_create_device(result, frame.device_width, frame.device_height);
}

int
st_rip3_environments_create_device(struct st_rip3_environments **result, uint32_t width, uint32_t height)
{
	struct st_rip3_environments *environments;
	struct st_rip3_frame         frame;
	int                          status;

	if (result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_rip3_frame_init_device(&frame, width, height);
	if (status != 0)
		return status;
	environments = calloc(1, sizeof(*environments));
	if (environments == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	environments->device                  = frame;
	environments->shared_charset          = ST_RIP3_CHARSET_USASCII;
	environments->entries[0].info.defined = true;
	(void)st_rip3_environment_defaults(&environments->entries[0].data);
	*result = environments;
	return 0;
}

void
st_rip3_environments_destroy(struct st_rip3_environments *environments)
{
	free(environments);
}

int
st_rip3_environments_current(const struct st_rip3_environments *environments, unsigned *result)
{
	if (environments == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	*result = environments->current;
	return 0;
}

int
st_rip3_environments_info(const struct st_rip3_environments *environments, unsigned number,
    struct st_rip3_environment_info *result)
{
	if (environments == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (number >= 36)
		return IO_ERROR_OUT_OF_RANGE;
	*result = environments->entries[number].info;
	return 0;
}

int
st_rip3_environments_get(const struct st_rip3_environments *environments, unsigned number,
    struct st_rip3_environment *result)
{
	if (environments == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (number >= 36)
		return IO_ERROR_OUT_OF_RANGE;
	if (!environments->entries[number].info.defined)
		return IO_ERROR_NOT_FOUND;
	*result = environments->entries[number].data;
	return 0;
}

int
st_rip3_environments_active(const struct st_rip3_environments *environments, unsigned *number,
    struct st_rip3_environment *result)
{
	if (environments == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (number != NULL)
		*number = environments->current;
	*result = environments->entries[environments->current].data;
	return 0;
}

static int
set_entry(struct st_rip3_environments *environments, unsigned number, const struct st_rip3_environment *value,
    bool define)
{
	if (environments == NULL || value == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (number >= 36)
		return IO_ERROR_OUT_OF_RANGE;
	struct environment_entry *entry = &environments->entries[number];
	if (!define && !entry->info.defined && number != environments->current)
		return IO_ERROR_NOT_FOUND;
	if (!valid(value))
		return IO_ERROR_OUT_OF_RANGE;
	if (entry->info.protected)
		return IO_ERROR_PERMISSION_DENIED;
	entry->data = *value;
	entry->info.defined |= define;
	return 0;
}

int
st_rip3_environments_define(struct st_rip3_environments *environments, unsigned number,
    const struct st_rip3_environment *value)
{
	return set_entry(environments, number, value, true);
}

int
st_rip3_environments_set(struct st_rip3_environments *environments, unsigned number,
    const struct st_rip3_environment *value)
{
	return set_entry(environments, number, value, false);
}

int
st_rip3_environments_color(struct st_rip3_environments *environments, unsigned number, unsigned bits)
{
	if (environments == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (number >= 36 || bits > 8)
		return IO_ERROR_OUT_OF_RANGE;
	if (!environments->entries[number].info.defined && number != environments->current)
		return IO_ERROR_NOT_FOUND;
	/* COLORMODE's callback bypasses protection; wire M uses the ordinary
	 * protected record setter. No other environment field is affected. */
	environments->entries[number].data.rgb_bits = bits;
	return 0;
}

int
st_rip3_environments_compat(struct st_rip3_environments *environments, unsigned number, bool current_form)
{
	if (environments == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (current_form)
		number = environments->current;
	if (number >= 36)
		return IO_ERROR_OUT_OF_RANGE;

	struct environment_entry *entry = &environments->entries[number];
	if (!current_form && !entry->info.defined)
		return IO_ERROR_INVALID_FORMAT;
	entry->data.world_width     = 640;
	entry->data.world_height    = 350;
	entry->data.base_math       = 36;
	entry->data.coordinate_size = 2;
	entry->data.rgb_bits        = 0;
	return 0;
}

enum st_rip3_charset
st_rip3_environments_shared_charset(const struct st_rip3_environments *environments)
{
	return environments->shared_charset;
}

void
st_rip3_environments_set_shared_charset(struct st_rip3_environments *environments, enum st_rip3_charset charset)
{
	environments->shared_charset = charset;
}

void
st_rip3_environments_set_charset(struct st_rip3_environments *environments, unsigned number,
    enum st_rip3_charset charset)
{
	struct environment_entry *entry = &environments->entries[number];
	if (charset != ST_RIP3_CHARSET_UNSET || entry->info.protected)
		entry->data.charset = charset;
}

void
st_rip3_environments_reset_charset(struct st_rip3_environments *environments, unsigned number, bool table)
{
	unsigned first = table ? 0 : number;
	unsigned end   = table ? 36 : number + 1;
	for (unsigned i = first; i < end; i++) {
		struct environment_entry *entry = &environments->entries[i];
		if (entry->info.defined && !entry->info.protected)
			entry->data.charset = ST_RIP3_CHARSET_UNSET;
	}
}

int
st_rip3_environments_select(struct st_rip3_environments *environments, unsigned number)
{
	if (environments == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (number >= 36)
		return IO_ERROR_OUT_OF_RANGE;
	if (!environments->entries[number].info.defined)
		return IO_ERROR_NOT_FOUND;
	environments->current = number;
	return 0;
}

int
st_rip3_environments_activate(struct st_rip3_environments *environments, unsigned number)
{
	if (environments == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (number >= 36)
		return IO_ERROR_OUT_OF_RANGE;
	struct st_rip3_environment *old         = &environments->entries[environments->current].data;
	unsigned                    old_pointer = old->mouse_pointer;
	struct environment_entry   *target      = &environments->entries[number];
	if (!target->info.defined) {
		(void)st_rip3_environment_defaults(&target->data);
		target->info = (struct st_rip3_environment_info) {.defined = true};
	}
	const struct st_rip3_environment value = target->data;
	/* The documented selector applies these setters before changing current,
	 * so they overwrite the departing record even when it is protected. */
	old->base_math       = value.base_math;
	old->rgb_bits        = value.rgb_bits;
	old->coordinate_size = value.coordinate_size;
	if (old_pointer != value.mouse_pointer)
		environments->scene_arrow_pending = false;
	environments->current = number;
	return 0;
}

int
st_rip3_environments_copy_entries(struct st_rip3_environments *environments, unsigned source,
    const unsigned *destinations, size_t count)
{
	if (environments == NULL || destinations == NULL || count == 0)
		return IO_ERROR_INVALID_ARGUMENT;
	if (source >= 36)
		return IO_ERROR_OUT_OF_RANGE;
	if (!environments->entries[source].info.defined)
		return IO_ERROR_NOT_FOUND;
	for (size_t i = 0; i < count; i++) {
		if (destinations[i] >= 36)
			return IO_ERROR_OUT_OF_RANGE;
		if (destinations[i] == source)
			return IO_ERROR_INVALID_ARGUMENT;
		if (environments->entries[destinations[i]].info.protected)
			return IO_ERROR_PERMISSION_DENIED;
	}
	for (size_t i = 0; i < count; i++) {
		environments->entries[destinations[i]] = environments->entries[source];
		if (destinations[i] == 0)
			environments->entries[0].info.protected = false;
	}
	return 0;
}

int
st_rip3_environments_mouse(struct st_rip3_environments *environments, unsigned pointer)
{
	if (environments == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (pointer > 7)
		return IO_ERROR_OUT_OF_RANGE;
	environments->entries[environments->current].data.mouse_pointer = pointer;
	environments->scene_arrow_pending                               = false;
	return 0;
}

void
st_rip3_environments_scene_lock(struct st_rip3_environments *environments)
{
	environments->entries[environments->current].data.mouse_pointer = 6;
	environments->scene_arrow_pending                               = true;
}

void
st_rip3_environments_scene_release(struct st_rip3_environments *environments, bool mouse_locked)
{
	if (environments == NULL)
		return;
	if (mouse_locked && environments->scene_arrow_pending)
		environments->entries[environments->current].data.mouse_pointer = 0;
	environments->scene_arrow_pending = false;
}

uint32_t
st_rip3_environments_playback_rate(const struct st_rip3_environments *environments)
{
	return environments->playback_rate;
}

int
st_rip3_environments_set_playback_rate(struct st_rip3_environments *environments, uint32_t rate)
{
	if (environments == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (rate > INT32_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	environments->playback_rate = rate;
	return 0;
}

static void
header_defaults(struct st_rip3_environment *value)
{
	value->base_math       = 36;
	value->coordinate_size = 2;
	value->rgb_bits        = 0;
	value->world_width     = 640;
	value->world_height    = 350;
}

static void
header_delete_others(struct st_rip3_environments *environments, unsigned keep)
{
	for (unsigned i = 1; i < 36; ++i)
		if (i != keep && !environments->entries[i].info.protected)
			environments->entries[i] = (struct environment_entry) {0};
}

void
st_rip3_environments_header(struct st_rip3_environments *environments, unsigned flags, unsigned slot,
    unsigned coordinate_width, unsigned rgb_bits)
{
	if (flags & 1) {
		if (environments->entries[environments->current].data.mouse_pointer
		    != environments->entries[0].data.mouse_pointer)
			environments->scene_arrow_pending = false;
		header_delete_others(environments, 0);
		environments->current = 0;
	}
	if (flags & 2)
		(void)st_rip3_environments_activate(environments, slot);
	if (flags & 4)
		header_delete_others(environments, environments->current);
	if (flags & 64) {
		for (unsigned i = 0; i < 36; ++i) {
			struct environment_entry *entry = &environments->entries[i];
			if (entry->info.defined && !entry->info.protected)
				header_defaults(&entry->data);
		}
	}
	struct st_rip3_environment *current = &environments->entries[environments->current].data;
	if (flags & 128)
		header_defaults(current);
	if (flags & 256)
		current->base_math = 64;
	else if (flags & 512)
		current->base_math = 36;
	if ((flags & 1024) && coordinate_width >= 2 && coordinate_width <= 5)
		current->coordinate_size = coordinate_width;
	if (flags & 2048)
		current->rgb_bits = 0;
	if ((flags & 4096) && rgb_bits <= 8)
		current->rgb_bits = rgb_bits;
	if (flags & 8192) {
		current->world_width  = current->base_math == 64 ? 4096 : 1280;
		current->world_height = current->base_math == 64 ? 3072 : 960;
	}
	if (flags & 16384) {
		current->world_width  = 640;
		current->world_height = 350;
	}
	if (flags & 32768)
		current->mouse_pointer = 0;
}

int
st_rip3_environments_reset_entry(struct st_rip3_environments *environments, unsigned number, bool current_form)
{
	if (environments == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (number >= 36)
		return IO_ERROR_OUT_OF_RANGE;
	struct environment_entry *entry = &environments->entries[number];
	if (entry->info.protected)
		return IO_ERROR_PERMISSION_DENIED;
	(void)st_rip3_environment_defaults(&entry->data);
	entry->info.defined = number == 0 || current_form;
	return 0;
}

int
st_rip3_environments_soft_reset(struct st_rip3_environments *environments)
{
	if (environments == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	struct st_rip3_environment *old = &environments->entries[environments->current].data;
	old->base_math                  = 36;
	old->coordinate_size            = 2;
	old->rgb_bits                   = 0;
	for (unsigned i = 0; i < 36; i++)
		(void)st_rip3_environments_reset_entry(environments, i, false);
	environments->scene_arrow_pending = false;
	environments->current             = 0;
	return 0;
}

int
st_rip3_environments_protect(struct st_rip3_environments *environments, unsigned number, bool protect)
{
	if (environments == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (number == 0 || number >= 36)
		return IO_ERROR_OUT_OF_RANGE;
	if (environments->entries[number].info.defined)
		environments->entries[number].info.protected = protect;
	return 0;
}

int
st_rip3_environments_frame(const struct st_rip3_environments *environments, unsigned number,
    struct st_rip3_frame *result)
{
	struct st_rip3_environment value;
	struct st_rip3_frame       frame;
	int                        status;

	if (environments == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = number == environments->current ? st_rip3_environments_active(environments, NULL, &value)
	                                         : st_rip3_environments_get(environments, number, &value);
	if (status != 0)
		return status;
	frame  = environments->device;
	status = st_rip3_frame_world(&frame, value.world_width, value.world_height);
	if (status == 0)
		*result = frame;
	return status;
}

int
st_rip3_environments_device(const struct st_rip3_environments *environments, struct st_rip3_frame *result)
{
	if (environments == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	*result = environments->device;
	return 0;
}

static int
environment_clone(const void *object, void **result)
{
	struct st_rip3_environments *snapshot;

	if (object == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	snapshot = malloc(sizeof(*snapshot));
	if (snapshot == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	*snapshot = *(const struct st_rip3_environments *)object;
	*result   = snapshot;
	return 0;
}

static int
environment_restore(void *object, const void *saved)
{
	struct st_rip3_environments       *environments = object;
	const struct st_rip3_environments *snapshot     = saved;

	if (object == NULL || saved == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (environments->device.device_width != snapshot->device.device_width
	    || environments->device.device_height != snapshot->device.device_height)
		return IO_ERROR_CONFLICT;
	/* Shared charset and pending scene pointer return are live state. */
	enum st_rip3_charset shared_charset      = environments->shared_charset;
	bool                 scene_arrow_pending = environments->scene_arrow_pending;
	*environments                            = *snapshot;
	environments->shared_charset             = shared_charset;
	environments->scene_arrow_pending        = scene_arrow_pending;
	return 0;
}

static void
environment_destroy(void *saved)
{
	st_rip3_environments_destroy(saved);
}

int
st_rip3_environments_backup_create(struct st_rip3_backup **result)
{
	static const struct st_rip3_backup_ops ops = {
	    environment_clone,
	    environment_restore,
	    environment_destroy,
	};

	return st_rip3_backup_create(result, &ops);
}

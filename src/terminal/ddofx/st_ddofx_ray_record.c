/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "filesystem.h"
#include "st_ddofx_ray.h"
#include "st_ddofx_resource.h"
#include "st_ddofx_script.h"
#include "st_file_lock.h"
#include "string_util.h"

enum {
	POSE_SIZE = 6
};

struct rc_recording {
	void    *context;
	char    *(*theme_path)(void *, const char *);
	char     name[13];
	char    *directory;
	FILE    *file;
	uint64_t left;
	uint8_t *member;
	size_t   member_size;
	uint8_t  previous[POSE_SIZE];
	bool     recording, sampled, extracted;
};

/* A door can replace a loose asset during playback. Remove only the bytes
 * extracted by this playback, resolving its cache name again under the lock. */
static void
remove_member(const struct rc_recording *r)
{
	st_file_lock_t *lock = NULL;
	if (st_file_lock_acquire(&lock, r->directory, true))
		return;
	char *path = r->theme_path(r->context, r->name);
	FILE *file;
	if (path && !fs_open(&file, path, "rb")) {
		bool    same = fs_stream_size(file) == (int64_t)r->member_size;
		uint8_t bytes[4096];
		for (size_t i = 0; same && i < r->member_size;) {
			size_t n = r->member_size - i;
			if (n > sizeof(bytes))
				n = sizeof(bytes);
			same = fread(bytes, 1, n, file) == n && !memcmp(bytes, r->member + i, n);
			i += n;
		}
		fclose(file);
		if (same)
			(void)fs_remove_file(path);
	}
	free(path);
	st_file_lock_release(lock);
}

void
st_ddofx_ray_stop_recording(struct rc_world *w)
{
	if (!w || !w->motion)
		return;
	struct rc_recording *r = w->motion;
	if (r->file)
		fclose(r->file);
	if (r->extracted)
		remove_member(r);
	free(r->directory);
	free(r->member);
	free(r);
	w->motion = NULL;
}

static bool
write_pose(struct rc_recording *r, const uint8_t pose[POSE_SIZE])
{
	/* One locked write per changed pose keeps the file available to query,
	 * playback and package installation between frames. */
	st_file_lock_t *lock = NULL;
	if (st_file_lock_acquire(&lock, r->directory, true))
		return false;
	char *path = r->theme_path(r->context, r->name);
	FILE *file;
	bool  ok = false;
	if (path && !fs_open(&file, path, "ab")) {
		int64_t size = fs_stream_size(file);
		if (size >= 0 && size % POSE_SIZE == 0) {
			ok = fwrite(pose, 1, POSE_SIZE, file) == POSE_SIZE && !fflush(file);
			if (!ok)
				(void)fs_stream_truncate(file, (uint64_t)size);
		}
		if (fclose(file))
			ok = false;
	}
	free(path);
	st_file_lock_release(lock);
	return ok;
}

static void
record_pose(struct rc_world *w)
{
	struct rc_recording *r       = w->motion;
	unsigned             heading = (unsigned)(142 * w->angle + 50) / 100;
	unsigned words[3] = {(unsigned)((w->x + 4) * 32), (unsigned)((w->y + 19) * 32), (768 - heading) % 512};
	uint8_t  pose[POSE_SIZE];
	for (int i = 0; i < 3; ++i) {
		pose[2 * i]     = (uint8_t)words[i];
		pose[2 * i + 1] = (uint8_t)(words[i] >> 8);
	}
	if (r->sampled && !memcmp(pose, r->previous, POSE_SIZE))
		return;
	if (!write_pose(r, pose)) {
		st_ddofx_ray_stop_recording(w);
		return;
	}
	memcpy(r->previous, pose, POSE_SIZE);
	r->sampled = true;
}

void
st_ddofx_ray_motion_frame(struct rc_world *w)
{
	struct rc_recording *r = w->motion;
	if (!r)
		return;
	if (r->recording) {
		record_pose(w);
		return;
	}
	uint8_t pose[POSE_SIZE];
	if (r->left < POSE_SIZE) {
		st_ddofx_ray_stop_recording(w);
		return;
	}
	if (!r->file)
		memcpy(pose, r->member + r->member_size - r->left, POSE_SIZE);
	else if (fread(pose, 1, POSE_SIZE, r->file) != POSE_SIZE) {
		st_ddofx_ray_stop_recording(w);
		return;
	}
	r->left -= POSE_SIZE;
	double x = (pose[0] + 256 * pose[1]) / 32.0 - 4;
	double y = (pose[2] + 256 * pose[3]) / 32.0 - 19;
	int    h = pose[4] + 256 * pose[5];
	if (x >= 2 && x < RC_WIDTH && y >= 2 && y < RC_HEIGHT && h < 512) {
		int angle = (100 * ((768 - h) % 512) + 71) / 142;
		w->angle  = angle ? angle : 360;
		w->x      = x;
		w->y      = y;
		w->dirty  = true;
	}
	if (r->left < POSE_SIZE)
		st_ddofx_ray_stop_recording(w);
}

static bool
extract(struct rc_recording *r, const struct st_ddofx *ddo, const struct st_ddofx_sink *sink)
{
	if (!ddo->resource_prefix[0] || !sink->open_asset)
		return false;
	char container[sizeof(ddo->resource_prefix) + 1];
	snprintf(container, sizeof(container), "%sM", ddo->resource_prefix);
	if (!ascii_case_compare(container, r->name))
		return false;
	FILE *file = sink->open_asset(sink->context, "BBSTHEME", NULL, container);
	if (!file)
		return false;
	uint32_t length;
	if (st_ddofx_resource_find(file, r->name, -1, &length, NULL) >= 0 && length <= 60000) {
		r->member = malloc(length ? length : 1);
		if (r->member && fread(r->member, 1, length, file) == length) {
			r->member_size = length;
			r->left        = length;
		}
		else {
			free(r->member);
			r->member = NULL;
		}
	}
	fclose(file);
	return r->member != NULL;
}

bool
st_ddofx_ray_record_command(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, const char *verb, char *args)
{
	bool record = !ascii_case_compare(verb, "5DREC");
	bool member = !ascii_case_compare(verb, "5DPLYR");
	if (!record && !member && ascii_case_compare(verb, "5DPLY"))
		return false;
	int32_t append = 0;
	if ((record && !st_ddofx_field_number(ddo, &args, &append)) || !args)
		return true;
	struct rc_world *w = st_ddofx_ray_world(ddo->graphics);
	if (!w)
		return true;
	const char *name = st_ddofx_asset_name(ddo, args);
	if (record && !ascii_case_compare(name, "STOP")) {
		if (w->motion && w->motion->recording)
			st_ddofx_ray_stop_recording(w);
		return true;
	}
	if ((unsigned)append > 1 || !st_ddofx_dos_filename(name))
		return true;
	struct rc_recording *r = calloc(1, sizeof(*r));
	if (!r)
		return true;
	strcpy(r->name, name);
	r->recording  = record;
	r->context    = sink->context;
	r->theme_path = sink->theme_path;
	bool ok       = false;
	if (record || member) {
		if (r->theme_path)
			r->directory = r->theme_path(r->context, NULL);
		if (r->directory && (record || extract(r, ddo, sink))) {
			st_file_lock_t *lock = NULL;
			if (!st_file_lock_acquire(&lock, r->directory, true)) {
				char *path = r->theme_path(r->context, name);
				if (path) {
					if (append) {
						FILE *file;
						if (!fs_open(&file, path, "ab"))
							ok = fclose(file) == 0;
					}
					else
						ok = fs_replace_file(path, r->member, r->member_size) == 0;
					r->extracted = ok && member;
				}
				free(path);
				st_file_lock_release(lock);
			}
		}
	}
	else if (sink->open_asset) {
		r->file = sink->open_asset(sink->context, "BBSTHEME", NULL, name);
		if (r->file) {
			int64_t length = fs_stream_size(r->file);
			if (length >= 0) {
				r->left = (uint64_t)length;
				ok      = true;
			}
		}
	}
	if (ok) {
		struct rc_recording *old = w->motion;
		if (old && old->extracted && !ascii_case_compare(old->name, r->name)) {
			/* The new command now owns this cache name. A loose replay
			 * inherits cleanup of the original extracted copy. */
			old->extracted = false;
			if (r->file) {
				r->extracted   = true;
				r->directory   = old->directory;
				r->member      = old->member;
				r->member_size = old->member_size;
				old->directory = NULL;
				old->member    = NULL;
			}
		}
		st_ddofx_ray_stop_recording(w);
		w->motion = r;
	}
	else {
		if (r->file)
			fclose(r->file);
		free(r->member);
		free(r->directory);
		free(r);
	}
	return true;
}

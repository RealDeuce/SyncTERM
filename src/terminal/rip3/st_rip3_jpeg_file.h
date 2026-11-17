/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_JPEG_FILE_H_
#define ST_RIP3_JPEG_FILE_H_

struct st_pixel_buffer;
struct st_rip3_resource_paths;

/* Read a JPEG through the trusted MEDIA search path and baseline decoder.
 * name is an exact bare UTF-8 filename, matched caselessly; no extension is
 * inferred. Success owns tagged RGB pixels and closes the file before
 * publishing them. Errors preserve *result and release all temporary storage.
 * Missing JPEG support returns NOT_SUPPORTED without accessing the disk.
 * No source deletion, rewriting, delivery or image-style policy is implied.
 * Storage/decoder statuses are consumed by the owning void parser boundary. */
int st_rip3_jpeg_load(struct st_pixel_buffer **result,
	const struct st_rip3_resource_paths *paths, const char *name);

#endif

/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_BITMAP_FILE_H_
#define ST_RIP3_BITMAP_FILE_H_

#include "st_rip3_bitmap.h"
struct st_rip3_resource_paths;

/* Read an owned decoded BMP through the media lookup namespace. Append .BMP
 * only when no extension is supplied; explicit names/extensions stay literal.
 * Read at most the file header's declared BMP length, validating it against
 * the actual file before file-buffer allocation. Contents and modification
 * times are retained; trailing bytes are ignored. No automatic legacy
 * conversion, clipboard, delivery, prompt or parser/session error policy is
 * implied. All failures preserve result;
 * callers consume storage/asset statuses inside their owning parser. */
int st_rip3_bitmap_load(struct st_rip3_bitmap **result,
	const struct st_rip3_resource_paths *paths, const char *name);

/* The ordinary bitmap lookup spelling of a legacy suffix, or NULL. */
const char *st_rip3_bitmap_legacy_extension(const char *extension);

/* Convert a completed legacy download in the active BBS cache to .BMP. */
int st_rip3_bitmap_convert_download(const struct st_rip3_resource_paths *paths,
	const char *name);

#endif

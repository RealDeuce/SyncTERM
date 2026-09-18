/* Read-only mapped file byte views. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef SYNCTERM_FILE_MAP_H_
#define SYNCTERM_FILE_MAP_H_

#include "io_error.h"

#include <stddef.h>
#include <stdio.h>

typedef struct file_map file_map_t;

/*
 * Paths are UTF-8. Result is unchanged on failure. A successful empty-file
 * mapping has size zero and a non-NULL data pointer. The mapped byte range is
 * immutable and remains valid until file_map_close().
 *
 * The mapped file must not be concurrently modified, replaced, or truncated.
 */
int file_map_open(file_map_t **result, const char *path);
/*
 * Maps the entire regular file underlying stream without changing its
 * position. The stream is borrowed and may be closed after this returns.
 * Callers must flush pending writes before mapping.
 */
int file_map_open_stream(file_map_t **result, FILE *stream);
/* NULL maps return NULL data and size zero. */
const void *file_map_data(const file_map_t *map);
size_t file_map_size(const file_map_t *map);

/* Always consumes map and attempts every cleanup operation. NULL succeeds. */
int file_map_close(file_map_t *map);

#endif

/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_DDOFX_RESOURCE_H_
#define ST_DDOFX_RESOURCE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* An ID of -1 matches the first named entry. On success the stream points
 * at its payload, and the return value is the entry's component ID. */
int st_ddofx_resource_find(FILE *, const char *member, int id, uint32_t *length, uint16_t *type);

bool st_ddofx_dos_filename(const char *name);
int st_ddofx_copy_bytes(FILE *output, FILE *input, uint64_t length);

int st_ddofx_edit_resource(const char *path, const char *member, const char *addition, uint16_t id);

#endif

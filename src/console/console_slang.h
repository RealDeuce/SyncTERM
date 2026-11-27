/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_CONSOLE_SLANG_H_
#define ST_CONSOLE_SLANG_H_
#include <stddef.h>
#include <stdint.h>
struct st_slang;
int  st_slang_create(struct st_slang **result, const char *path, const void *(*symbol)(const char *));
void st_slang_free(struct st_slang *chain);
int  st_slang_frame(struct st_slang *chain, size_t count, uint32_t source, uint32_t width, uint32_t height,
    uint32_t target, uint32_t output_width, uint32_t output_height, uint32_t elapsed);
#endif

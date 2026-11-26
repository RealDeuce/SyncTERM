/* Allocation failures only in the window interpreter preparation service. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "io_error.h"
#include "st_cterm.h"
#include "st_rip3_native_window.h"
#include "string_compat.h"

bool         st_cterm_window_test_tracking;
size_t       st_cterm_window_test_attempts, st_cterm_window_test_live;
size_t       st_cterm_window_test_fail_at = SIZE_MAX;
static void *allocations[16];

static void *
window_malloc(size_t size)
{
	if (!st_cterm_window_test_tracking)
		return malloc(size);
	if (st_cterm_window_test_attempts++ == st_cterm_window_test_fail_at)
		return NULL;
	void *value = malloc(size);
	if (value != NULL)
		for (size_t i = 0; i < 16; i++)
			if (allocations[i] == NULL) {
				allocations[i] = value;
				st_cterm_window_test_live++;
				break;
			}
	return value;
}

static void
window_free(void *value)
{
	if (value != NULL)
		for (size_t i = 0; i < 16; i++)
			if (allocations[i] == value) {
				allocations[i] = NULL;
				st_cterm_window_test_live--;
				break;
			}
	free(value);
}

static void *
window_calloc(size_t count, size_t size)
{
	if (count != 0 && size > SIZE_MAX / count)
		return NULL;
	void *value = window_malloc(count * size);
	if (value != NULL)
		memset(value, 0, count * size);
	return value;
}

static char *
window_strdup(const char *value)
{
	size_t length = strlen(value) + 1;
	char  *copy   = window_malloc(length);
	if (copy != NULL)
		memcpy(copy, value, length);
	return copy;
}

size_t st_cterm_definition_test_fonts;

static int
window_font_capture(struct st_console_font_snapshot **result, st_console_t *console, uint16_t id)
{
	if (st_cterm_window_test_tracking && st_cterm_window_test_attempts++ == st_cterm_window_test_fail_at)
		return IO_ERROR_OUT_OF_MEMORY;
	int status = st_console_font_snapshot_create(result, console, id);
	if (status == 0)
		st_cterm_definition_test_fonts++;
	return status;
}

static int
window_font_register(st_console_t *console, const struct st_console_font_snapshot *font, uint16_t *id)
{
	if (st_cterm_window_test_tracking && st_cterm_window_test_attempts++ == st_cterm_window_test_fail_at)
		return IO_ERROR_OUT_OF_MEMORY;
	return st_console_font_snapshot_register(console, font, id);
}

static int
window_font_copy(st_console_t *destination, st_console_t *source, uint16_t identifier, uint16_t *result)
{
	if (st_cterm_window_test_tracking && st_cterm_window_test_attempts++ == st_cterm_window_test_fail_at)
		return IO_ERROR_OUT_OF_MEMORY;
	return st_console_copy_font(destination, source, identifier, result);
}

static void
window_font_destroy(struct st_console_font_snapshot *font)
{
	if (font != NULL)
		st_cterm_definition_test_fonts--;
	st_console_font_snapshot_destroy(font);
}

#define malloc window_malloc
#define free   window_free
#include "../st_cterm_text_window.c"

#define calloc                            window_calloc
#define strdup                            window_strdup
#define st_console_copy_font              window_font_copy
#define st_console_font_snapshot_create   window_font_capture
#define st_console_font_snapshot_register window_font_register
#define st_console_font_snapshot_destroy  window_font_destroy
#include "../st_cterm_window_definition.c"

size_t
st_cterm_definition_test_resources(const struct st_cterm_window_definition *definition)
{
	return definition->count;
}

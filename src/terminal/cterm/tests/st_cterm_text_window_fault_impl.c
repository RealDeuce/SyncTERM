/* Allocation failures only in the window interpreter preparation service. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

bool st_cterm_window_test_tracking;
size_t st_cterm_window_test_attempts,st_cterm_window_test_live;
size_t st_cterm_window_test_fail_at=SIZE_MAX;
static void *allocations[16];

static void *
window_malloc(size_t size)
{
	if (!st_cterm_window_test_tracking) return malloc(size);
	if (st_cterm_window_test_attempts++==st_cterm_window_test_fail_at) return NULL;
	void *value=malloc(size);
	if (value!=NULL) for (size_t i=0;i<16;i++) if (allocations[i]==NULL) {
		allocations[i]=value;st_cterm_window_test_live++;break;
	}
	return value;
}

static void
window_free(void *value)
{
	if (value!=NULL) for (size_t i=0;i<16;i++) if (allocations[i]==value) {
		allocations[i]=NULL;st_cterm_window_test_live--;break;
	}
	free(value);
}

#define malloc window_malloc
#define free window_free
#include "../st_cterm_text_window.c"

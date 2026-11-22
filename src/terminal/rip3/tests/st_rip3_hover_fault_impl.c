/* Inject allocation failures into the owner without changing production code. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_mouse.h"
#include <stdlib.h>

size_t st_rip3_hover_test_attempts,st_rip3_hover_test_live;
size_t st_rip3_hover_test_fail_at=SIZE_MAX;
static void *test_malloc(size_t length)
{
	if (st_rip3_hover_test_attempts++==st_rip3_hover_test_fail_at) return NULL;
	void *result=malloc(length);if (result!=NULL) st_rip3_hover_test_live++;return result;
}
static void *test_calloc(size_t count,size_t length)
{
	if (st_rip3_hover_test_attempts++==st_rip3_hover_test_fail_at) return NULL;
	void *result=calloc(count,length);if (result!=NULL) st_rip3_hover_test_live++;return result;
}
static void test_free(void *pointer)
{
	if (pointer!=NULL) st_rip3_hover_test_live--;free(pointer);
}
#define malloc test_malloc
#define calloc test_calloc
#define free test_free
#include "../st_rip3_mouse.c"

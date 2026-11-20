/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdint.h>
#include <stdlib.h>
#include "st_rip3_condition_internal.h"
#include "io_error.h"

size_t st_rip3_angle_test_fail_at=SIZE_MAX,st_rip3_angle_test_allocations;
static void *test_malloc(size_t size) {return st_rip3_angle_test_allocations++==st_rip3_angle_test_fail_at ? NULL : malloc(size);}
static void *test_calloc(size_t count,size_t size) {return st_rip3_angle_test_allocations++==st_rip3_angle_test_fail_at ? NULL : calloc(count,size);}
static void *test_realloc(void *p,size_t size) {return st_rip3_angle_test_allocations++==st_rip3_angle_test_fail_at ? NULL : realloc(p,size);}
static int test_parse(struct st_rip3_condition_program **p,const uint8_t *b,size_t n,size_t *c)
{return st_rip3_angle_test_allocations++==st_rip3_angle_test_fail_at ? IO_ERROR_OUT_OF_MEMORY : st_rip3_condition_program_parse(p,b,n,c);}
static int test_evaluation(struct st_rip3_condition_evaluation **p,const struct st_rip3_condition_program *s)
{return st_rip3_angle_test_allocations++==st_rip3_angle_test_fail_at ? IO_ERROR_OUT_OF_MEMORY : st_rip3_condition_evaluation_create(p,s);}
#define malloc test_malloc
#define calloc test_calloc
#define realloc test_realloc
#define st_rip3_condition_program_parse test_parse
#define st_rip3_condition_evaluation_create test_evaluation
#include "../st_rip3_angle.c"

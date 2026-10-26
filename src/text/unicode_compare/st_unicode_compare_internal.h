/* Internal Unicode comparison test support. */

/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */

#ifndef ST_UNICODE_COMPARE_INTERNAL_H_
#define ST_UNICODE_COMPARE_INTERNAL_H_

#include "st_unicode_compare.h"

int st_unicode_sort_key_compare_uca(const st_unicode_sort_key_t *left,
	const st_unicode_sort_key_t *right);

#endif

/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_WREN_SCREEN_TEST_CONTROL_H
#define ST_WREN_SCREEN_TEST_CONTROL_H
#include <stdbool.h>
#include <stddef.h>
struct st_wren_screen_fault {
	const char *name;
	int status;
	bool after;
	size_t calls;
};
extern struct st_wren_screen_fault st_wren_test_screen_fault;
extern bool st_wren_test_no_pixels;
#endif

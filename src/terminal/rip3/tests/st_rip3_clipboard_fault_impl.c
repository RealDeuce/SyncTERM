/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "io_error.h"
#include "st_rip3_clipboard.h"
#include "st_rip3_ports.h"
#include "st_rip3_surface.h"

bool                                st_rip3_clipboard_test_fail_allocate;
bool                                st_rip3_clipboard_test_fail_copy;
bool                                st_rip3_clipboard_test_observed_selection;
struct st_rip3_clipboard_selection *st_rip3_clipboard_test_selection;

static int
test_create(struct st_rip3_surface **result, uint32_t width, uint32_t height)
{
	if (st_rip3_clipboard_test_selection != NULL)
		st_rip3_clipboard_test_observed_selection = st_rip3_clipboard_test_selection->set;
	if (st_rip3_clipboard_test_fail_allocate) {
		st_rip3_clipboard_test_fail_allocate = false;
		return IO_ERROR_OUT_OF_MEMORY;
	}
	return st_rip3_surface_create(result, width, height);
}

static int
test_copy(struct st_rip3_ports *ports, unsigned source, const struct st_rip3_rect *from, unsigned destination,
    const struct st_rip3_rect *to, enum st_rip3_transfer_mode mode)
{
	if (st_rip3_clipboard_test_fail_copy) {
		st_rip3_clipboard_test_fail_copy = false;
		return IO_ERROR_IO;
	}
	return st_rip3_ports_copy(ports, source, from, destination, to, mode);
}

#define st_rip3_surface_create test_create
#define st_rip3_ports_copy     test_copy
#include "../st_rip3_clipboard.c"

/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_wren_screen_test_calls.h"
static int
test_screen_mode(struct st_wren_host *host, enum st_screen_mode mode)
{
	struct st_wren_screen_fault *fault = &st_wren_test_screen_fault;
	if (fault->name != NULL && strcmp(fault->name, "st_wren_host_set_screen_mode") == 0) {
		++fault->calls;
		return fault->status;
	}
	return st_wren_host_set_screen_mode(host, mode);
}

static int
test_hyperlink_copy(st_hyperlink_registry_t *registry, uint16_t identifier, struct st_hyperlink *result)
{
	struct st_wren_screen_fault *fault = &st_wren_test_screen_fault;
	if (fault->name != NULL && strcmp(fault->name, "st_hyperlink_registry_copy") == 0) {
		++fault->calls;
		return fault->status;
	}
	return st_hyperlink_registry_copy(registry, identifier, result);
}
static int
test_hyperlink_contains(st_hyperlink_registry_t *registry, uint16_t identifier, bool *result)
{
	struct st_wren_screen_fault *fault = &st_wren_test_screen_fault;
	if (fault->name != NULL && strcmp(fault->name, "st_hyperlink_registry_contains") == 0) {
		++fault->calls;
		return fault->status;
	}
	return st_hyperlink_registry_contains(registry, identifier, result);
}
#define st_hyperlink_registry_copy test_hyperlink_copy
#define st_hyperlink_registry_contains test_hyperlink_contains
#define st_wren_host_set_screen_mode test_screen_mode
#include "../wren_bind_menu.c"

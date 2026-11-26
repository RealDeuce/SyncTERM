/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "io_error.h"
#include "st_pixels.h"
#include "st_rip3_backup.h"
#include "st_rip3_clipboard.h"
#include "st_rip3_ports.h"
#include "st_rip3_resident_query.h"

#define CHECK(expression)                                                                        \
	do {                                                                                     \
		if (!(expression)) {                                                             \
			fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
			return 1;                                                                \
		}                                                                                \
	} while (0)

static int
check_saved(struct st_rip3_ports *ports, bool survivor)
{
	struct st_rip3_port_info info;
	unsigned                 current;

	CHECK(st_rip3_ports_current(ports, &current) == 0 && current == 2);
	CHECK(st_rip3_ports_info(ports, 1, &info) == 0);
	CHECK(info.defined && !info.offscreen && info.protected && info.viewport_active);
	CHECK(info.bounds.x0 == 10 && info.bounds.y0 == 20 && info.bounds.x1 == 15 && info.bounds.y1 == 24);
	CHECK(info.viewport.x0 == 1 && info.viewport.y0 == 1 && info.viewport.x1 == 4 && info.viewport.y1 == 3);
	CHECK(st_rip3_ports_info(ports, 2, &info) == 0);
	CHECK(info.defined && info.offscreen && info.protected && !info.viewport_active);
	CHECK(info.bounds.x0 == 0 && info.bounds.y0 == 0 && info.bounds.x1 == 3 && info.bounds.y1 == 2);
	CHECK(info.viewport.x0 == 1 && info.viewport.y0 == 0 && info.viewport.x1 == 3 && info.viewport.y1 == 1);
	/* Port 3 is absent in the snapshot. Its protected live definition is
	 * retained by the original cleanup/undefined-slot rule. */
	CHECK(st_rip3_ports_info(ports, 3, &info) == 0 && info.defined == survivor && info.protected == survivor);
	if (survivor) {
		uint8_t pixel;
		CHECK(info.offscreen && !info.viewport_active && info.viewport.x0 == 1 && info.viewport.y1 == 1);
		CHECK(st_rip3_ports_read(ports, 3, 1, 1, &pixel) == 0 && pixel == 11);
	}
	for (int32_t y = 0; y < 3; y++) {
		for (int32_t x = 0; x < 4; x++) {
			uint8_t value;

			CHECK(st_rip3_ports_read(ports, 2, x, y, &value) == 0);
			CHECK(value == (x == 1 && y == 1 ? 7 : 4));
		}
	}
	return 0;
}

static int
restoration(void)
{
	struct st_rip3_ports     *ports  = NULL;
	struct st_rip3_backup    *backup = NULL;
	struct st_rip3_palette    palette;
	struct st_pixel_buffer   *capture        = NULL;
	const struct st_rip3_rect screen_port    = {10, 20, 15, 24};
	const struct st_rip3_rect screen_view    = {1, 1, 4, 3};
	const struct st_rip3_rect offscreen      = {99, 88, 3, 2};
	const struct st_rip3_rect offscreen_view = {1, 0, 3, 1};
	const struct st_rip3_rect offscreen_full = {0, 0, 3, 2};
	const struct st_rip3_rect replacement    = {30, 40, 32, 42};
	const struct st_rip3_rect small          = {0, 0, 0, 0};

	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_ports_backup_create(&backup) == 0);
	CHECK(st_rip3_ports_erase(ports, 2) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &screen_port, false) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	CHECK(st_rip3_ports_viewport(ports, &screen_view) == 0);
	CHECK(st_rip3_ports_erase(ports, 3) == 0);
	CHECK(st_rip3_ports_protect(ports, 1, true) == 0);
	CHECK(st_rip3_ports_define(ports, 2, &offscreen, true) == 0);
	CHECK(st_rip3_ports_switch(ports, 2) == 0);
	CHECK(st_rip3_ports_erase(ports, 4) == 0);
	CHECK(st_rip3_ports_plot(ports, 1, 1, 7, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_viewport(ports, &offscreen_view) == 0);
	CHECK(st_rip3_ports_activate(ports, false) == 0);
	CHECK(st_rip3_ports_protect(ports, 2, true) == 0);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_BASE, 0, ports) == 0);

	CHECK(st_rip3_ports_protect(ports, 1, false) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &replacement, false) == 0);
	CHECK(st_rip3_ports_protect(ports, 1, true) == 0);
	CHECK(st_rip3_ports_protect(ports, 2, false) == 0);
	CHECK(st_rip3_ports_define(ports, 2, &small, true) == 0);
	CHECK(st_rip3_ports_switch(ports, 2) == 0);
	CHECK(st_rip3_ports_erase(ports, 9) == 0);
	CHECK(st_rip3_ports_protect(ports, 2, true) == 0);
	CHECK(st_rip3_ports_define(ports, 3, &offscreen, true) == 0);
	CHECK(st_rip3_ports_switch(ports, 3) == 0 && st_rip3_ports_erase(ports, 11) == 0);
	CHECK(st_rip3_ports_viewport(ports, &offscreen_view) == 0 && st_rip3_ports_activate(ports, false) == 0);
	CHECK(st_rip3_ports_protect(ports, 3, true) == 0);
	CHECK(st_rip3_ports_define(ports, 4, &offscreen, true) == 0);
	uint64_t survivor_generation = st_rip3_ports_generation(ports, 3);
	CHECK(st_rip3_ports_switch(ports, 0) == 0);
	CHECK(st_rip3_ports_erase(ports, 5) == 0);
	CHECK(st_rip3_ports_plot(ports, 10, 10, 8, ST_RIP3_COPY) == 0);
	/* Restore bypasses protected live definitions, but no screen bitmap was
	 * saved by the port table: the newer magenta background must survive. */
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_BASE, 0, ports) == 0);
	CHECK(check_saved(ports, true) == 0);
	struct st_rip3_port_info survivor;
	CHECK(st_rip3_ports_info(ports, 3, &survivor) == 0 && survivor.defined && survivor.protected);
	CHECK(st_rip3_ports_generation(ports, 3) == survivor_generation);
	CHECK(st_rip3_ports_info(ports, 4, &survivor) == 0 && !survivor.defined);
	CHECK(st_rip3_ports_protect(ports, 2, false) == 0);
	CHECK(st_rip3_ports_viewport(ports, &offscreen_full) == 0);
	CHECK(st_rip3_ports_plot(ports, 0, 0, 10, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_protect(ports, 2, true) == 0);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_BASE, 0, ports) == 0);
	CHECK(check_saved(ports, true) == 0);
	CHECK(st_rip3_palette_defaults(&palette) == 0);
	CHECK(st_rip3_ports_snapshot(ports, &palette, &capture) == 0);
	for (uint32_t y = 0; y < 480; y++) {
		for (uint32_t x = 0; x < 640; x++) {
			uint32_t expected = x == 10 && y == 10 ? ST_PIXEL_COLOR_RGB_VALUE(85, 85, 85)
			                                       : ST_PIXEL_COLOR_RGB_VALUE(170, 0, 170);

			CHECK(capture->primary[(size_t)y * 640 + x] == expected);
		}
	}
	st_pixel_buffer_destroy(capture);
	/* No saved screen pointer or pixels may depend on the original owner. */
	st_rip3_ports_destroy(ports);
	ports = NULL;
	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_BASE, 0, ports) == 0);
	CHECK(check_saved(ports, false) == 0);
	CHECK(st_rip3_ports_snapshot(ports, &palette, &capture) == 0);
	for (size_t i = 0; i < 640U * 480U; i++)
		CHECK(capture->primary[i] == ST_PIXEL_COLOR_RGB_TAG);
	st_pixel_buffer_destroy(capture);
	st_rip3_ports_destroy(ports);
	st_rip3_backup_destroy(backup);
	return 0;
}

static int
canvas_mismatch(void)
{
	struct st_rip3_ports      *original = NULL, *other = NULL;
	struct st_rip3_backup     *backup = NULL;
	struct st_rip3_backup_info info;
	struct st_rip3_port_info   port_info;
	const struct st_rip3_rect  bounds = {0, 0, 2, 1};
	unsigned                   current;
	uint8_t                    value;

	CHECK(st_rip3_ports_create(&original, ST_RIP3_CANVAS_800_600) == 0);
	CHECK(st_rip3_ports_create(&other, ST_RIP3_CANVAS_1024_768) == 0);
	CHECK(st_rip3_ports_define(other, 35, &bounds, true) == 0);
	CHECK(st_rip3_ports_switch(other, 35) == 0);
	CHECK(st_rip3_ports_erase(other, 9) == 0);
	CHECK(st_rip3_ports_protect(other, 35, true) == 0);
	CHECK(st_rip3_ports_backup_create(&backup) == 0);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_SLOT, 9, original) == 0);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_SLOT, 9, other) == IO_ERROR_CONFLICT);
	CHECK(st_rip3_backup_info(backup, &info) == 0 && info.slot_used[9]);
	CHECK(st_rip3_ports_current(other, &current) == 0 && current == 35);
	CHECK(st_rip3_ports_info(other, 35, &port_info) == 0 && port_info.protected);
	CHECK(st_rip3_ports_read(other, 35, 0, 0, &value) == 0 && value == 9);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_SLOT, 9, original) == 0);
	CHECK(st_rip3_backup_info(backup, &info) == 0 && !info.slot_used[9]);
	st_rip3_backup_destroy(backup);
	st_rip3_ports_destroy(original);
	st_rip3_ports_destroy(other);
	return 0;
}

static int
query_value(struct st_rip3_ports *ports, bool floating, unsigned number, const char *expected)
{
	const struct st_rip3_resident_query *q = st_rip3_ports_query(ports, floating, number);
	CHECK(q != NULL && q->length == strlen(expected)
	      && (q->length == 0 || memcmp(q->bytes, expected, q->length) == 0));
	return 0;
}

static int
query_set(struct st_rip3_ports *ports, bool floating, unsigned number, const char *source)
{
	return st_rip3_ports_define_query(ports, floating, number, (const uint8_t *)source, strlen(source), false);
}

static int
resident_ownership(void)
{
	struct st_rip3_ports  *ports  = NULL;
	struct st_rip3_backup *backup = NULL;
	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_640_480) == 0
	      && st_rip3_ports_backup_create(&backup) == 0);
	const struct st_rip3_rect bounds = {0, 0, 100, 100};
	CHECK(st_rip3_ports_define(ports, 1, &bounds, false) == 0);
	CHECK(query_set(ports, true, 63, "F") == 0 && query_set(ports, false, 0, "A") == 0
	      && query_set(ports, false, 1, "B") == 0);
	CHECK(query_set(ports, false, 35, "undefined") == IO_ERROR_NOT_FOUND);
	CHECK(st_rip3_ports_protect(ports, 1, true) == 0
	      && query_set(ports, false, 1, "$OFF$") == IO_ERROR_PERMISSION_DENIED);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_BASE, 0, ports) == 0);
	extern size_t st_rip3_resident_test_attempts, st_rip3_resident_test_fail_at;
	for (size_t fail = 0; fail < 3; fail++) {
		CHECK(query_set(ports, true, 0, "newF") == 0 && query_set(ports, false, 0, "newA") == 0);
		st_rip3_resident_test_fail_at = st_rip3_resident_test_attempts + fail;
		CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_BASE, 0, ports) == IO_ERROR_OUT_OF_MEMORY);
		st_rip3_resident_test_fail_at = SIZE_MAX;
		CHECK(query_value(ports, true, 0, "newF") == 0 && query_value(ports, false, 0, "newA") == 0);
		CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_BASE, 0, ports) == 0);
		CHECK(query_value(ports, true, 0, "F") == 0 && query_value(ports, false, 0, "A") == 0
		      && query_value(ports, false, 1, "B") == 0);
	}
	for (size_t fail = 0; fail < 3; fail++) {
		CHECK(query_set(ports, true, 0, "liveF") == 0 && query_set(ports, false, 0, "liveA") == 0);
		uint64_t generation           = st_rip3_ports_generation(ports, 0);
		st_rip3_resident_test_fail_at = st_rip3_resident_test_attempts + fail;
		CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_BASE, 0, ports) == IO_ERROR_OUT_OF_MEMORY);
		st_rip3_resident_test_fail_at = SIZE_MAX;
		CHECK(query_value(ports, true, 0, "liveF") == 0 && query_value(ports, false, 0, "liveA") == 0
		      && st_rip3_ports_generation(ports, 0) == generation);
	}
	CHECK(st_rip3_ports_reset(ports, false) == 0 && query_value(ports, true, 0, "") == 0
	      && query_value(ports, false, 0, "") == 0 && query_value(ports, false, 1, "B") == 0);
	CHECK(st_rip3_ports_protect(ports, 1, false) == 0 && st_rip3_ports_delete(ports, 1) == 0);
	CHECK(st_rip3_ports_query(ports, false, 1) == NULL);
	CHECK(st_rip3_ports_define(ports, 1, &bounds, true) == 0
	      && query_set(ports, false, 1, "offscreen") == IO_ERROR_NOT_SUPPORTED);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_BASE, 0, ports) == 0
	      && query_value(ports, false, 1, "B") == 0);
	st_rip3_backup_destroy(backup);
	st_rip3_ports_destroy(ports);
	return 0;
}

static int
clipboard_ownership(void)
{
	struct st_rip3_ports              *ports = NULL, *other = NULL;
	struct st_rip3_backup             *backup    = NULL;
	struct st_rip3_clipboard_selection selection = {.set = true, .port = 5}, target = {0};
	const struct st_rip3_rect          bounds = {0, 0, 2, 2};
	CHECK(st_rip3_ports_create_device(&ports, 720, 400) == 0);
	st_rip3_ports_bind_clipboard(ports, &selection);
	CHECK(st_rip3_ports_backup_create(&backup) == 0);
	CHECK(st_rip3_ports_define(ports, 5, &bounds, true) == 0);
	CHECK(query_set(ports, true, 0, "saved") == 0);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_BASE, 0, ports) == 0);
	selection = (struct st_rip3_clipboard_selection) {.set = true, .port = 0};
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_SLOT, 0, ports) == 0);
	selection = (struct st_rip3_clipboard_selection) {0};
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_SLOT, 1, ports) == 0);
	selection = (struct st_rip3_clipboard_selection) {.set = true, .port = 7};
	CHECK(query_set(ports, true, 0, "live") == 0);
	extern size_t st_rip3_resident_test_attempts, st_rip3_resident_test_fail_at;
	st_rip3_resident_test_fail_at = st_rip3_resident_test_attempts;
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_BASE, 0, ports) == IO_ERROR_OUT_OF_MEMORY);
	st_rip3_resident_test_fail_at = SIZE_MAX;
	CHECK(selection.set && selection.port == 7);
	st_rip3_resident_test_fail_at = st_rip3_resident_test_attempts;
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_BASE, 0, ports) == IO_ERROR_OUT_OF_MEMORY);
	st_rip3_resident_test_fail_at = SIZE_MAX;
	CHECK(selection.set && selection.port == 7 && query_value(ports, true, 0, "live") == 0);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_BASE, 0, ports) == 0 && selection.set
	      && selection.port == 5);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_SLOT, 0, ports) == 0 && selection.set
	      && selection.port == 0);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_SLOT, 1, ports) == 0 && !selection.set);
	CHECK(st_rip3_ports_create_device(&other, 720, 400) == 0);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_BASE, 0, other) == IO_ERROR_NOT_SUPPORTED);
	st_rip3_ports_bind_clipboard(other, &target);
	st_rip3_ports_destroy(ports);
	ports     = NULL;
	selection = (struct st_rip3_clipboard_selection) {0};
	CHECK(
	    st_rip3_backup_restore(backup, ST_RIP3_BACKUP_BASE, 0, other) == 0 && target.set && target.port == 5);
	CHECK(!selection.set); /* Snapshot never owns a pointer into its old session. */
	st_rip3_ports_destroy(other);
	CHECK(st_rip3_ports_create_device(&other, 800, 600) == 0);
	st_rip3_ports_bind_clipboard(other, &target);
	target.port = 35;
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_BASE, 0, other) == IO_ERROR_CONFLICT && target.set
	      && target.port == 35);
	st_rip3_ports_destroy(other);
	st_rip3_backup_destroy(backup);
	return 0;
}

int
main(void)
{
	CHECK(resident_ownership() == 0 && restoration() == 0);
	CHECK(canvas_mismatch() == 0 && clipboard_ownership() == 0);
	extern size_t st_rip3_resident_test_live;
	CHECK(st_rip3_resident_test_live == 0);
	return 0;
}

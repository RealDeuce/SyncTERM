/* Independent owned screen snapshots and shared backup-area behavior. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_screen.h"
#include "st_rip3_ports.h"
#include "st_rip3_backup.h"
#include "st_rip3_host_variables.h"
#include "st_rip3_table_host.h"
#include "st_pixels.h"
#include "io_error.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
		return 1; \
	} \
} while (0)

struct observations {size_t replies, diagnostics; enum st_rip3_host_diagnostic last;};

static void
reply(void *context, const uint8_t *bytes, size_t length)
{
	struct observations *observed = context;
	(void)bytes; (void)length;
	observed->replies++;
}

static void
diagnostic(void *context, enum st_rip3_host_diagnostic kind)
{
	struct observations *observed = context;
	observed->diagnostics++; observed->last = kind;
}

static int
process(const struct st_rip3_host_variables *host, const char *command, int error)
{
	struct observations observed = {0};
	const struct st_rip3_host_sink sink = {&observed,reply,diagnostic};

	st_rip3_host_variables_process(host, &sink, (const uint8_t *)command, strlen(command));
	CHECK(observed.replies == 0 && observed.diagnostics == (error < 0 ? 0u : 1u));
	if (error >= 0) CHECK((int)observed.last == error);
	return 0;
}

static int
capture(const struct st_rip3_screen *screen, uint32_t width, uint32_t height)
{
	struct st_pixel_buffer *pixels = NULL;

	CHECK(st_rip3_ports_snapshot(screen->ports, screen->palette, &pixels) == 0);
	CHECK(pixels->width == width && pixels->height == height);
	for (uint32_t y = 0; y < height; y++) {
		for (uint32_t x = 0; x < width; x++) {
			uint32_t expected = 0x102030;
			if (x >= 10 && x <= 14 && y >= 20 && y <= 23) expected = 0xabcdef;
			if (x >= 21 && x <= 23 && y >= 32 && y <= 34) expected = 0x456789;
			CHECK(pixels->primary[(size_t)y * width + x] == (ST_PIXEL_COLOR_RGB_TAG | expected));
		}
	}
	st_pixel_buffer_destroy(pixels);
	return 0;
}

static int
restoration(enum st_rip3_canvas canvas)
{
	struct st_rip3_ports *ports = NULL, *other = NULL;
	struct st_rip3_backup *backup = NULL;
	struct st_rip3_palette palette, expected_palette, other_palette;
	bool status_bar = true, other_status = false;
	struct st_rip3_screen screen = {NULL,&palette,&status_bar};
	struct st_rip3_screen wrong = {NULL,&other_palette,&other_status};
	struct st_rip3_frame frame;
	struct st_rip3_backup_info saves;
	struct st_rip3_port_info info;
	struct st_rip3_table_host tables = {0};
	struct st_rip3_host_variables host = {.tables=&tables};
	const struct st_rip3_rect rectangle = {10,20,14,23}, onscreen = {20,30,25,35};
	const struct st_rip3_rect view = {1,2,3,4}, offscreen = {0,0,2,1};
	const struct st_rip3_rect offview = {1,0,2,1}, replacement = {45,55,46,56};
	const struct st_rip3_rect tiny = {0,0,0,0};
	unsigned current;
	uint8_t pixel;

	CHECK(st_rip3_frame_init(&frame, canvas) == 0);
	CHECK(st_rip3_ports_create(&ports, canvas) == 0);
	CHECK(st_rip3_palette_defaults(&palette) == 0);
	palette.rgb[1] = 0x102030; palette.rgb[56] = 0xabcdef;
	palette.rgb[250] = 0x456789; palette.rgb[200] = 0xfedcba;
	expected_palette = palette;
	screen.ports = ports;
	CHECK(st_rip3_ports_erase(ports, 1) == 0);
	CHECK(st_rip3_ports_fill(ports, &rectangle, 56, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &onscreen, false) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0 && st_rip3_ports_viewport(ports, &view) == 0);
	CHECK(st_rip3_ports_erase(ports, 250) == 0);
	CHECK(st_rip3_ports_activate(ports, false) == 0 && st_rip3_ports_protect(ports, 1, true) == 0);
	CHECK(st_rip3_ports_define(ports, 2, &offscreen, true) == 0);
	CHECK(st_rip3_ports_switch(ports, 2) == 0 && st_rip3_ports_erase(ports, 8) == 0);
	CHECK(st_rip3_ports_viewport(ports, &offview) == 0);
	CHECK(st_rip3_ports_activate(ports, false) == 0 && st_rip3_ports_protect(ports, 2, true) == 0);
	CHECK(st_rip3_screen_backup_create(&backup) == 0);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_BASE, 0, &screen) == 0);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_SLOT, 0, &screen) == 0);
	CHECK(st_rip3_backup_protect(backup, 0, true) == 0);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_SLOT, 1, &screen) == 0);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_STACK, 0, &screen) == 0);
	/* Later live port configuration and offscreen content must survive a
	 * screen-only restore; all saved on-screen indexes remain independent. */
	CHECK(st_rip3_ports_switch(ports, 0) == 0 && st_rip3_ports_erase(ports, 7) == 0);
	CHECK(st_rip3_ports_viewport(ports, &tiny) == 0); /* Disable port zero. */
	CHECK(st_rip3_ports_protect(ports, 1, false) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &replacement, false) == 0);
	CHECK(st_rip3_ports_protect(ports, 1, true) == 0);
	CHECK(st_rip3_ports_protect(ports, 2, false) == 0);
	CHECK(st_rip3_ports_switch(ports, 2) == 0 && st_rip3_ports_activate(ports, true) == 0);
	CHECK(st_rip3_ports_erase(ports, 9) == 0);
	CHECK(st_rip3_ports_activate(ports, false) == 0 && st_rip3_ports_protect(ports, 2, true) == 0);
	memset(&palette, 0, sizeof(palette)); status_bar = false;
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_BASE, 0, &screen) == 0);
	CHECK(status_bar);
	for (size_t i = 0; i < 256; i++) CHECK(palette.rgb[i] == expected_palette.rgb[i]);
	CHECK(st_rip3_ports_current(ports, &current) == 0 && current == 2);
	CHECK(st_rip3_ports_info(ports, 0, &info) == 0 && !info.viewport_active);
	CHECK(st_rip3_ports_info(ports, 1, &info) == 0 && info.protected
	    && info.bounds.x0 == 45 && info.bounds.y0 == 55 && info.bounds.x1 == 46 && info.bounds.y1 == 56);
	CHECK(st_rip3_ports_info(ports, 2, &info) == 0 && info.protected && !info.viewport_active);
	CHECK(st_rip3_ports_read(ports, 2, 1, 0, &pixel) == 0 && pixel == 9);
	CHECK(st_rip3_ports_read(ports, 2, 0, 0, &pixel) == 0 && pixel == 8);
	CHECK(capture(&screen, frame.device_width, frame.device_height) == 0);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_SLOT, 0, &screen) == 0);
	CHECK(st_rip3_backup_info(backup, &saves) == 0 && saves.slot_used[0] && saves.slot_protected[0]);
	CHECK(st_rip3_backup_protect(backup, 0, false) == 0);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_SLOT, 0, &screen) == 0);
	CHECK(st_rip3_backup_info(backup, &saves) == 0 && !saves.slot_used[0] && saves.stack == 1);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_STACK, 0, &screen) == 0);
	CHECK(st_rip3_backup_info(backup, &saves) == 0 && saves.stack == 0);
	CHECK(st_rip3_ports_create(&other, canvas == ST_RIP3_CANVAS_640_480
	    ? ST_RIP3_CANVAS_800_600 : ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_palette_defaults(&other_palette) == 0);
	wrong.ports = other;
	other_palette.rgb[200] = 0x112233;
	CHECK(st_rip3_ports_erase(other, 5) == 0);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_SLOT, 1, &wrong) == IO_ERROR_CONFLICT);
	CHECK(!other_status && other_palette.rgb[200] == 0x112233);
	CHECK(st_rip3_ports_read(other, 0, 0, 0, &pixel) == 0 && pixel == 5);
	CHECK(st_rip3_backup_info(backup, &saves) == 0 && saves.slot_used[1]);
	/* Invalid save data cannot replace a previous valid base snapshot. A
	 * restore may repair the live display palette even if it is now invalid. */
	palette.rgb[200] = 0xff000000;
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_BASE, 0, &screen) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_BASE, 0, &screen) == 0);
	CHECK(palette.rgb[200] == 0xfedcba);
	struct st_rip3_screen incomplete[] = {{NULL,&palette,&status_bar},
	    {ports,NULL,&status_bar},{ports,&palette,NULL}};
	for (size_t i = 0; i < sizeof(incomplete) / sizeof(incomplete[0]); i++) {
		CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_BASE, 0, &incomplete[i]) == IO_ERROR_INVALID_ARGUMENT);
		CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_SLOT, 1, &incomplete[i]) == IO_ERROR_INVALID_ARGUMENT);
		CHECK(st_rip3_backup_info(backup, &saves) == 0 && saves.base && saves.slot_used[1]);
	}
	status_bar = false;
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_SLOT, 3, &screen) == 0);
	status_bar = true;
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_SLOT, 3, &screen) == 0 && !status_bar);
	st_rip3_ports_destroy(ports);
	CHECK(st_rip3_ports_create(&ports, canvas) == 0);
	screen.ports = ports;
	memset(&palette, 0, sizeof(palette)); status_bar = false;
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_BASE, 0, &screen) == 0);
	CHECK(status_bar && capture(&screen, frame.device_width, frame.device_height) == 0);
	/* The public parser can bind the explicit snapshot core to generic COPY.
	 * Full SAVE/RESTORE aliases stay unsupported pending context clarification. */
	tables.tables[ST_RIP3_TABLE_SCREEN] = (struct st_rip3_table_binding){&screen,backup};
	CHECK(process(&host, "$COPY(SCREEN,BASE,S2,PUSH)$", -1) == 0);
	CHECK(process(&host, "$SAVE$", ST_RIP3_HOST_UNSUPPORTED_REFERENCE) == 0);
	CHECK(process(&host, "$RESTORE(POP)$", ST_RIP3_HOST_UNSUPPORTED_REFERENCE) == 0);
	CHECK(process(&host, "$SAVEALL(PUSH)$", ST_RIP3_HOST_UNSUPPORTED_REFERENCE) == 0);
	CHECK(process(&host, "$RESTOREALL(POP)$", ST_RIP3_HOST_UNSUPPORTED_REFERENCE) == 0);
	for (unsigned i = 0; i < 10; i++) {
		char command[20];
		snprintf(command, sizeof(command), "$SAVE%u$", i);
		CHECK(process(&host, command, ST_RIP3_HOST_UNSUPPORTED_REFERENCE) == 0);
		snprintf(command, sizeof(command), "$RESTORE%u$", i);
		CHECK(process(&host, command, ST_RIP3_HOST_UNSUPPORTED_REFERENCE) == 0);
	}
	CHECK(st_rip3_backup_info(backup, &saves) == 0 && saves.stack == 1 && saves.slot_used[2]);
	CHECK(st_rip3_ports_erase(ports, 0) == 0);
	status_bar = false; palette.rgb[1] = 0;
	CHECK(process(&host, "$COPY(SCREEN,POP,TBL)$", -1) == 0);
	CHECK(status_bar && capture(&screen, frame.device_width, frame.device_height) == 0);
	CHECK(st_rip3_backup_info(backup, &saves) == 0 && saves.stack == 0);
	tables.tables[ST_RIP3_TABLE_SCREEN].live = &wrong;
	CHECK(process(&host, "$COPY(SCREEN,S1,TBL)$", ST_RIP3_HOST_OPERATION_FAILED) == 0);
	CHECK(st_rip3_backup_info(backup, &saves) == 0 && saves.slot_used[1]);
	tables.tables[ST_RIP3_TABLE_SCREEN].live = &screen;
	CHECK(process(&host, "$COPY(SCREEN,S1,TBL)$", -1) == 0);
	CHECK(st_rip3_backup_info(backup, &saves) == 0 && !saves.slot_used[1]);
	/* Raw index-plane snapshots are independent in both directions; restoring
	 * one neither adopts its allocation nor modifies the borrowed snapshot. */
	struct st_rip3_surface *raw = NULL, *small = NULL;
	CHECK(st_rip3_ports_screen_clone(NULL, &raw) == IO_ERROR_INVALID_ARGUMENT && raw == NULL);
	CHECK(st_rip3_ports_screen_clone(ports, NULL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_ports_screen_restore(ports, NULL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_surface_create(&small, 1, 1) == 0);
	CHECK(st_rip3_ports_screen_restore(ports, small) == IO_ERROR_CONFLICT);
	st_rip3_surface_destroy(small);
	CHECK(capture(&screen, frame.device_width, frame.device_height) == 0);
	CHECK(st_rip3_ports_screen_clone(ports, &raw) == 0);
	CHECK(st_rip3_surface_plot(raw, NULL, 0, 0, 77, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_read(ports, 0, 0, 0, &pixel) == 0 && pixel == 1);
	CHECK(st_rip3_ports_screen_restore(ports, raw) == 0);
	CHECK(st_rip3_surface_plot(raw, NULL, 0, 0, 88, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_read(ports, 0, 0, 0, &pixel) == 0 && pixel == 77);
	CHECK(st_rip3_ports_plot(ports, 0, 0, 66, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_surface_get(raw, 0, 0, &pixel) == 0 && pixel == 88);
	st_rip3_surface_destroy(raw);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_BASE, 0, &screen) == 0);
	CHECK(capture(&screen, frame.device_width, frame.device_height) == 0);
	st_rip3_backup_destroy(backup);
	st_rip3_ports_destroy(other);
	st_rip3_ports_destroy(ports);
	return 0;
}

int
main(void)
{
	CHECK(restoration(ST_RIP3_CANVAS_640_480) == 0);
	CHECK(restoration(ST_RIP3_CANVAS_800_600) == 0);
	CHECK(restoration(ST_RIP3_CANVAS_1024_768) == 0);
	return 0;
}

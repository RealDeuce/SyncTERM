/* Independent screen/palette binding, lifetime and failure vectors. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdio.h>
#include <string.h>

#include "io_error.h"
#include "st_pixels.h"
#include "st_rip3_backup.h"
#include "st_rip3_bitmap.h"
#include "st_rip3_host_internal.h"
#include "st_rip3_palettes.h"
#include "st_rip3_ports.h"
#include "st_rip3_screen.h"
#include "st_rip3_table_host.h"

#define CHECK(x)                                                        \
	do {                                                            \
		if (!(x)) {                                             \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x); \
			return 1;                                       \
		}                                                       \
	} while (0)

static int
execute(const struct st_rip3_table_host *host, const char *source, char *result, size_t capacity)
{
	struct st_rip3_builtin_reference reference;
	int status = st_rip3_builtin_reference_parse(&reference, (const uint8_t *)source, strlen(source));
	return status == 0 ? st_rip3_table_host_execute(host, &reference, result, capacity) : status;
}

struct backend {
	unsigned captures, restores, errors;
	bool     fail_capture, fail_restore, invalid_capture, valid;
};

static int
capture(void *context, struct st_pixel_buffer *pixels)
{
	struct backend *b = context;
	b->captures++;
	if (b->fail_capture)
		return IO_ERROR_OUT_OF_MEMORY;
	int status = st_pixel_buffer_ensure_alternate(pixels);
	if (status != 0)
		return status;
	pixels->primary[0]   = ST_PIXEL_COLOR_RGB_TAG | 0x123456;
	pixels->alternate[0] = b->invalid_capture ? 256u : ST_PIXEL_COLOR_RGB_TAG | 0x654321;
	return 0;
}

static int
restore(void *context, const struct st_pixel_buffer *pixels, const struct st_rip3_palette *palette)
{
	struct backend *b = context;
	b->restores++;
	b->valid &= pixels->primary[0] == (ST_PIXEL_COLOR_RGB_TAG | 0x123456) && pixels->alternate != NULL
	            && pixels->alternate[0] == (ST_PIXEL_COLOR_RGB_TAG | 0x654321) && palette->rgb[15] == 0xaa0000;
	return b->fail_restore ? IO_ERROR_IO : 0;
}

static void
failed(void *context, int status)
{
	(void)status;
	((struct backend *)context)->errors++;
}

static int
palette(struct st_rip3_palettes *p, unsigned n, uint32_t rgb)
{
	CHECK(st_rip3_palettes_switch(p, n) == 0 && st_rip3_palettes_rgb(p, 15, 8, &rgb, 1) == 0);
	return 0;
}

static int
pixels(struct st_rip3_ports *ports, uint32_t width, uint32_t height, uint32_t inside, uint32_t outside)
{
	struct st_pixel_buffer *p = NULL;
	CHECK(st_rip3_ports_screen_capture(ports, &p) == 0);
	CHECK(p->width == width && p->height == height);
	for (uint32_t y = 0; y < height; y++)
		for (uint32_t x = 0; x < width; x++)
			CHECK(p->primary[(size_t)y * width + x] == (x < 640 && y < 480 ? inside : outside));
	st_pixel_buffer_destroy(p);
	return 0;
}

static int
binding(uint32_t width, uint32_t height)
{
	struct st_rip3_ports    *ports = NULL, *other = NULL;
	struct st_rip3_palettes *palettes = NULL, *other_palettes = NULL;
	struct st_rip3_backup   *backup = NULL;
	CHECK(st_rip3_ports_create_device(&ports, width, height) == 0 && st_rip3_palettes_create(&palettes) == 0);
	CHECK(st_rip3_screen_backup_create(&backup) == 0);
	struct st_rip3_screen screen = {.ports = ports, .palettes = palettes};
	CHECK(palette(palettes, 5, 0xaa0000) == 0 && st_rip3_ports_erase(ports, 15) == 0);
	struct st_rip3_rect area = {20, 30, 100, 110}, view = {2, 3, 6, 8}, off = {0, 0, 2, 1};
	CHECK(st_rip3_ports_define(ports, 1, &area, false) == 0 && st_rip3_ports_switch(ports, 1) == 0);
	CHECK(st_rip3_ports_viewport(ports, &view) == 0 && st_rip3_ports_activate(ports, false) == 0
	      && st_rip3_ports_protect(ports, 1, true) == 0);
	CHECK(st_rip3_ports_define(ports, 2, &off, true) == 0 && st_rip3_ports_switch(ports, 2) == 0
	      && st_rip3_ports_erase(ports, 9) == 0);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_BASE, 0, &screen) == 0);
	CHECK(palette(palettes, 5, 0x0000aa) == 0 && palette(palettes, 17, 0x00aa00) == 0
	      && st_rip3_palettes_protect(palettes, 17, true) == 0);
	CHECK(st_rip3_ports_screen_clear_black(ports) == 0);
	CHECK(st_rip3_ports_switch(ports, 0) == 0 && st_rip3_ports_viewport(ports, &view) == 0
	      && st_rip3_ports_activate(ports, false) == 0);
	CHECK(st_rip3_ports_switch(ports, 2) == 0);
	uint64_t generation = st_rip3_ports_generation(ports, 0);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_BASE, 0, &screen) == 0);
	unsigned                    selected;
	struct st_rip3_palette      value;
	struct st_rip3_palette_info info;
	CHECK(st_rip3_palettes_current(palettes, &selected) == 0 && selected == 17);
	CHECK(st_rip3_palettes_get(palettes, 17, &value) == 0 && value.rgb[15] == 0xaa0000);
	CHECK(st_rip3_palettes_info(palettes, 17, &info) == 0 && info.defined && info.protected);
	CHECK(st_rip3_palettes_get(palettes, 5, &value) == 0 && value.rgb[15] == 0x0000aa);
	struct st_rip3_port_info port;
	uint8_t                  pixel;
	CHECK(st_rip3_ports_current(ports, &selected) == 0 && selected == 2
	      && st_rip3_ports_generation(ports, 0) == generation);
	CHECK(st_rip3_ports_info(ports, 0, &port) == 0 && !port.viewport_active && port.viewport.x0 == 2);
	CHECK(st_rip3_ports_info(ports, 1, &port) == 0 && port.protected && !port.viewport_active
	      && port.bounds.x0 == 20);
	CHECK(st_rip3_ports_read(ports, 2, 0, 0, &pixel) == 0 && pixel == 9);
	struct st_pixel_buffer *raw = NULL;
	CHECK(st_rip3_ports_screen_capture(ports, &raw) == 0);
	CHECK(raw->width == width && raw->height == height);
	for (size_t i = 0; i < (size_t)width * height; i++)
		CHECK(raw->primary[i] == 15);
	st_pixel_buffer_destroy(raw);
	/* A selected-but-undefined palette is overwritten without becoming defined. */
	CHECK(palette(palettes, 18, 0x00aa00) == 0 && st_rip3_palettes_reset_entry(palettes, 18) == 0);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_BASE, 0, &screen) == 0);
	CHECK(st_rip3_palettes_info(palettes, 18, &info) == 0 && !info.defined && !info.protected);
	CHECK(st_rip3_palettes_current_palette(palettes, &value) == 0 && value.rgb[15] == 0xaa0000);
	/* No resize: independently shaped destination receives the unscaled overlap. */
	CHECK(st_rip3_ports_create_device(&other, 720, 400) == 0 && st_rip3_palettes_create(&other_palettes) == 0);
	CHECK(st_rip3_ports_erase(other, 5) == 0);
	struct st_rip3_screen target = {.ports = other, .palettes = other_palettes};
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_BASE, 0, &target) == 0);
	CHECK(st_rip3_ports_screen_capture(other, &raw) == 0 && raw->width == 720 && raw->height == 400);
	for (uint32_t y = 0; y < 400; y++)
		for (uint32_t x = 0; x < 720; x++)
			CHECK(raw->primary[(size_t)y * 720 + x] == (x < width && y < height ? 15u : 5u));
	st_pixel_buffer_destroy(raw);
	st_rip3_backup_destroy(backup);
	st_rip3_ports_destroy(ports);
	st_rip3_palettes_destroy(palettes);
	st_rip3_ports_destroy(other);
	st_rip3_palettes_destroy(other_palettes);
	return 0;
}

static int
actions(void)
{
	struct st_rip3_ports    *ports    = NULL;
	struct st_rip3_palettes *palettes = NULL;
	struct st_rip3_backup   *backup   = NULL;
	CHECK(st_rip3_ports_create_device(&ports, 640, 480) == 0 && st_rip3_palettes_create(&palettes) == 0
	      && st_rip3_screen_backup_create(&backup) == 0);
	CHECK(palette(palettes, 5, 0xaa0000) == 0);
	struct backend            b       = {.valid = true};
	struct st_rip3_screen     screen  = {.ports = ports,
	    .palettes                              = palettes,
	    .context                               = &b,
	    .capture                               = capture,
	    .restore                               = restore};
	struct st_rip3_table_host host    = {.operation_context = &b, .operation_failed = failed};
	host.tables[ST_RIP3_TABLE_SCREEN] = (struct st_rip3_table_binding) {&screen, backup};
	char                       out[100];
	struct st_rip3_backup_info info;
	CHECK(execute(&host, "$SAVE$", out, sizeof(out)) == 0 && out[0] == 0 && b.captures == 1);
	b.fail_capture = true;
	CHECK(execute(&host, "$SAVE$", out, sizeof(out)) == 0 && b.errors == 1);
	b.fail_capture    = false;
	b.invalid_capture = true;
	CHECK(execute(&host, "$SAVE$", out, sizeof(out)) == 0 && b.errors == 2);
	b.invalid_capture = false;
	CHECK(execute(&host, "$RESTORE$", out, sizeof(out)) == 0 && b.restores == 1 && b.valid);
	/* A restored native RGB pixel must be matched as RGB when an indexed BMP
	 * consumer reads it, not truncated to the low byte as an index. */
	struct st_rip3_palette export_palette;
	CHECK(st_rip3_palettes_current_palette(palettes, &export_palette) == 0);
	export_palette.rgb[7]           = 0x123456;
	struct st_rip3_bitmap    *image = NULL;
	const struct st_rip3_rect one   = {0, 0, 0, 0};
	CHECK(st_rip3_bitmap_capture_port(&image, ports, 0, &one, &export_palette) == 0 && image->bit_depth == 8
	      && image->pixels->primary[0] == 7);
	st_rip3_bitmap_destroy(image);
	for (unsigned i = 0; i < 10; i++) {
		char command[30];
		snprintf(command, sizeof(command), "$SAVE%u$", i);
		CHECK(execute(&host, command, out, sizeof(out)) == 0);
		snprintf(command, sizeof(command), "$RESTORE%u$", i);
		CHECK(execute(&host, command, out, sizeof(out)) == 0);
		CHECK(st_rip3_backup_info(backup, &info) == 0 && !info.slot_used[i]);
		snprintf(command, sizeof(command), "$SAVE%u(1)$", i);
		CHECK(execute(&host, command, out, sizeof(out)) == IO_ERROR_INVALID_FORMAT);
		snprintf(command, sizeof(command), "$RESTORE%u(1)$", i);
		CHECK(execute(&host, command, out, sizeof(out)) == IO_ERROR_INVALID_FORMAT);
	}
	CHECK(execute(&host, "$SAVE(3)$", out, sizeof(out)) == 0);
	b.fail_restore = true;
	CHECK(execute(&host, "$RESTORE3$", out, sizeof(out)) == 0);
	CHECK(st_rip3_backup_info(backup, &info) == 0 && !info.slot_used[3]);
	CHECK(execute(&host, "$SAVE(3)$", out, sizeof(out)) == 0 && st_rip3_backup_protect(backup, 3, true) == 0);
	CHECK(execute(&host, "$RESTORE(3)$", out, sizeof(out)) == 0);
	CHECK(st_rip3_backup_info(backup, &info) == 0 && info.slot_used[3] && info.slot_protected[3]);
	CHECK(execute(&host, "$SAVE(PUSH)$", out, sizeof(out)) == 0
	      && execute(&host, "$RESTORE(POP)$", out, sizeof(out)) == 0);
	CHECK(st_rip3_backup_info(backup, &info) == 0 && info.stack == 1);
	b.fail_restore = false;
	CHECK(execute(&host, "$RESTORE(pop)$", out, sizeof(out)) == 0);
	CHECK(st_rip3_backup_info(backup, &info) == 0 && info.stack == 0 && b.valid);
	CHECK(execute(&host, "$SAVE(10)$", out, sizeof(out)) == IO_ERROR_INVALID_FORMAT);
	CHECK(execute(&host, "$SAVE(BASE,PUSH)$", out, sizeof(out)) == IO_ERROR_INVALID_FORMAT);
	unsigned calls = b.captures;
	CHECK(execute(&host, "$SAVE$", out, 0) == IO_ERROR_OUT_OF_RANGE && b.captures == calls);
	CHECK(execute(&host, "$RESET(SCREEN,TBL)$", out, sizeof(out)) == IO_ERROR_INVALID_FORMAT);
	CHECK(execute(&host, "$RESET(SCREEN)$", out, sizeof(out)) == 0 && pixels(ports, 640, 480, 0, 0) == 0);
	CHECK(execute(&host, "$RESET(SCREEN,BACKUP)$", out, sizeof(out)) == 0);
	CHECK(st_rip3_backup_info(backup, &info) == 0 && !info.base && info.slot_used[3]);
	CHECK(execute(&host, "$UNPROT(SCREEN,S3)$", out, sizeof(out)) == 0
	      && execute(&host, "$RESET(SCREEN,ALL)$", out, sizeof(out)) == 0);
	CHECK(st_rip3_backup_info(backup, &info) == 0 && !info.slot_used[3]);
	CHECK(st_rip3_ports_screen_capture(NULL, NULL) == IO_ERROR_INVALID_ARGUMENT
	      && st_rip3_ports_screen_restore(ports, NULL) == IO_ERROR_INVALID_ARGUMENT);
	st_rip3_backup_destroy(backup);
	st_rip3_ports_destroy(ports);
	st_rip3_palettes_destroy(palettes);
	return 0;
}

int
main(void)
{
	CHECK(binding(640, 480) == 0 && binding(800, 600) == 0 && binding(1024, 768) == 0 && binding(720, 400) == 0
	      && actions() == 0);
	return 0;
}

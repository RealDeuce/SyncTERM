/* Independent RIP_COPY_REGION geometry, overlap and wire expectations. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_rip_engine.h"
#include "st_pixels.h"

#include <stdio.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); return 1; } } while (0)

struct copy_expected {
	unsigned x, source_y, width, height, destination_y;
	bool repeat_source_row;
};

static int
passthrough(void *context, const uint8_t *bytes, size_t size)
{
	(void)bytes;
	*(size_t *)context += size;
	return 0;
}

static bool
at_column(void *context)
{
	(void)context;
	return true;
}

static int
feed(struct cterm_rip_engine *engine, const char *text, size_t size)
{
	size_t consumed = 0;
	int status = cterm_rip_engine_feed(engine, (const uint8_t *)text, size, &consumed);
	return status == 0 && consumed == size ? 0 : 1;
}

static int
copy_case(const char *name, const char *arguments, int version,
    struct copy_expected expected, bool reduced, bool disabled, size_t split)
{
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	/* Padding around the logical screen catches incorrect raster strides
	 * and writes beyond the displayed RIP rows/columns. */
	const struct st_console_config config = {.columns = 80, .rows = 43,
	    .cell_width = 8, .cell_height = 8, .pixel_width = 648, .pixel_height = 360};
	const struct st_console_rect full = {0, 0, 648, 360};
	const struct st_pixel_blit blit = {.width = 648, .height = 360, .scale_x = 1, .scale_y = 1};
	size_t passed = 0;
	const struct cterm_rip_parser_sink host = {.context = &passed,
	    .passthrough = passthrough, .at_carriage_return_column = at_column};
	struct cterm_rip_engine engine;
	st_console_t *console = NULL;
	struct st_pixel_buffer *seed = NULL, *after = NULL;
	char wire[64];
	CHECK(st_console_create(&console, &display, &config) == 0);
	CHECK(cterm_rip_engine_init(&engine, console, &host) == 0);
	struct cterm_rip_raster *rip = &engine.raster;
	rip->version = version;
	CHECK(st_pixel_buffer_create(&seed, 648, 360, true) == 0);
	for (unsigned y = 0; y < 360; ++y) for (unsigned x = 0; x < 648; ++x) {
		size_t at = (size_t)y * 648 + x;
		seed->primary[at] = ST_PIXEL_COLOR_RGB_TAG | ((y + 1) << 11) | x;
		seed->alternate[at] = seed->primary[at] | UINT32_C(0x00800000);
	}
	CHECK(st_console_blit_pixels(console, seed, NULL, &blit) == 0);
	CHECK(st_pixel_buffer_create(&rip->clipboard, 2, 2, true) == 0);
	CHECK(st_pixel_buffer_fill(rip->clipboard, 19, 23) == 0);
	struct st_pixel_buffer *clipboard = rip->clipboard;
	rip->clipx = 300; rip->clipy = 200;
	rip->x = 31; rip->y = 41; rip->colour = 3;
	rip->xor_mode = true; rip->line_pattern = 0; rip->line_width = 3;
	rip->fill_colour = 5; rip->fill_style = 2;
	rip->viewport.sx = rip->viewport.sy = disabled ? 0 : 300;
	rip->viewport.ex = rip->viewport.ey = disabled ? 0 : 310;
	if (reduced) { rip->x_max = 320; rip->y_max = 175; }
	int written = snprintf(wire, sizeof(wire), "!|1G%s\r\n", arguments);
	CHECK(written > 0 && (size_t)written < sizeof(wire));
	size_t size = (size_t)written;
	if (split > size) {
		for (size_t i = 0; i < size; ++i) CHECK(feed(&engine, wire + i, 1) == 0);
	}
	else {
		CHECK(feed(&engine, wire, split) == 0);
		CHECK(feed(&engine, wire + split, size - split) == 0);
	}
	CHECK(engine.commands_seen == 1 && rip->error == 0 && passed == 0);
	CHECK(st_console_get_pixels(&after, console, &full, false) == 0);
	CHECK(after->alternate != NULL);
	for (unsigned y = 0; y < 360; ++y) for (unsigned x = 0; x < 648; ++x) {
		size_t at = (size_t)y * 648 + x, source = at;
		if (x >= expected.x && x < expected.x + expected.width
		    && y >= expected.destination_y && y < expected.destination_y + expected.height) {
			unsigned row = expected.source_y
			    + (expected.repeat_source_row ? 0 : y - expected.destination_y);
			source = (size_t)row * 648 + x;
		}
		if (after->primary[at] != seed->primary[source]
		    || after->alternate[at] != seed->alternate[source]) {
			fprintf(stderr, "%s v%d split %zu pixel %u,%u\n", name, version, split, x, y);
			CHECK(false);
		}
	}
	CHECK(rip->clipboard == clipboard && rip->clipx == 300 && rip->clipy == 200);
	for (size_t i = 0; i < 4; ++i)
		CHECK(clipboard->primary[i] == 19 && clipboard->alternate[i] == 23);
	CHECK(rip->x == 31 && rip->y == 41 && rip->colour == 3 && rip->xor_mode);
	CHECK(rip->line_pattern == 0 && rip->line_width == 3 && rip->fill_colour == 5 && rip->fill_style == 2);
	CHECK(rip->viewport.sx == (disabled ? 0 : 300) && rip->viewport.sy == (disabled ? 0 : 300));
	CHECK(rip->viewport.ex == (disabled ? 0 : 310) && rip->viewport.ey == (disabled ? 0 : 310));
	CHECK(rip->drawing_pixels == NULL && rip->drawing_damage == NULL);
	st_pixel_buffer_destroy(after);
	st_pixel_buffer_destroy(seed);
	cterm_rip_engine_destroy(&engine);
	st_console_destroy(console);
	return 0;
}

int
st_cterm_rip_copy_test_main(void)
{
	/* Literal rectangles from 0c16:0623 and its downward helper 0293,
	 * cross-checked with the local legacy copy-region research/tests.
	 * RIPv3 uses ripscrip.txt's outward rounding and strict bottom fit.
	 * Zero rectangles mean unchanged pixels, never generated emulator output. */
	static const struct {
		const char *name, *arguments;
		struct copy_expected v1, v3;
	} cases[] = {
		{"up", "080A0O0C0002", {8,10,16,3,2,false}, {8,10,16,3,2,false}},
		{"down overlap", "080A0O0D000B", {8,10,16,4,11,false}, {8,10,16,4,11,false}},
		{"up overlap", "080A0O0D0009", {8,10,16,4,9,false}, {8,10,16,4,9,false}},
		{"reversed", "0O0D080A0002", {8,10,16,4,2,false}, {8,10,16,4,2,false}},
		{"same line", "080A0O0C000A", {0}, {0}},
		{"one row", "080A0O0A0002", {0}, {8,10,16,1,2,false}},
		{"unaligned", "0E0A120C0002", {8,10,24,3,2,false}, {8,10,32,3,2,false}},
		{"reserved positive", "0E0A120C0702", {0,10,32,3,2,false}, {8,10,32,3,2,false}},
		{"reserved negative", "0E0A120C-702", {16,10,16,3,2,false}, {8,10,32,3,2,false}},
		{"negative left", "-A0A0O0C0002", {0,10,24,3,2,false}, {0,10,24,3,2,false}},
		{"negative source row", "08-10O02000A", {8,0,16,3,10,false}, {8,0,16,3,10,false}},
		{"negative destination", "080A0O0C00-1", {8,10,16,3,0,false}, {8,10,16,3,0,false}},
		{"pre-clamp height", "089G0OB4008C", {0}, {8,340,16,10,300,false}},
		{"source bottom clamp", "089G0OB40000", {8,340,16,10,0,false}, {8,340,16,10,0,false}},
		{"source right clamp", "HK0AJG0C0002", {0}, {632,10,8,3,2,false}},
		{"large copy", "0000HR9O0001", {0,0,632,349,1,false}, {0,0,640,349,1,false}},
		{"full width", "0000HR9O0101", {0,0,640,349,1,false}, {0,0,640,349,1,false}},
		{"loose bottom guard", "080A0O0B009P", {8,10,16,1,349,false}, {0}},
		{"destination overflow", "080A0O0D009O", {0}, {0}},
		{"negative source rows", "08-20O-10002", {8,0,16,1,2,false}, {8,0,16,1,2,false}},
		{"missing field", "080A0O0C00", {0}, {0}},
		{"invalid reserved", "080A0O0C?002", {0}, {0}},
		{"no whole byte", "080A080C0002", {0}, {0}},
		{"negative x bounds", "-A0A-10C0002", {0}, {0}},
		{"resort adjusted x down", "080A090C-70K", {8,10,8,3,20,false}, {8,10,8,3,20,false}},
		{"inverted adjusted x up", "080A090C-702", {0}, {8,10,8,3,2,false}},
		{"short final field", "080A0O0C002", {8,10,16,3,2,false}, {8,10,16,3,2,false}},
	};
	for (int version = 1; version <= 3; version += 2) {
		for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); ++i) {
			struct copy_expected expected = version == 1 ? cases[i].v1 : cases[i].v3;
			CHECK(copy_case(cases[i].name, cases[i].arguments, version, expected, false, i % 2 == 0, 0) == 0);
			CHECK(copy_case(cases[i].name, cases[i].arguments, version, expected, false, i % 2 != 0, 64) == 0);
		}
		const char *arguments = "080A0O0D000B";
		for (size_t split = 0; split <= strlen(arguments) + 6; ++split)
			CHECK(copy_case("every split", arguments, version,
			    (struct copy_expected){8,10,16,4,11,false}, false, true, split) == 0);
		/* Independently rounded logical rows: source 10,11,12 -> 5,6,6;
		 * destinations 3,4,5 -> 2,2,3. Last logical write wins. */
		CHECK(copy_case("reduced up", "080A0O0C0003", version,
		    (struct copy_expected){4,6,9,2,2,true}, true, true, 64) == 0);
		/* Downward aliasing must read the original source row 6 even after
		 * an earlier logical write copies row 5 onto that physical row. */
		CHECK(copy_case("reduced down", "080A0O0C000B", version,
		    (struct copy_expected){4,6,9,1,7,false}, true, false, 64) == 0);
	}
	return 0;
}

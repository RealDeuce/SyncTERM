/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_commands.h"
#include "st_rip3_environments.h"
#include "st_rip3_palettes.h"
#include "st_rip3_host_variables.h"
#include "st_rip3_table_host.h"
#include "st_pixels.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
		return 1; \
	} \
} while (0)

struct observation { unsigned errors; enum st_rip3_command_diagnostic kind; };

static void
error(void *context, enum st_rip3_command_diagnostic kind)
{
	struct observation *seen = context;
	seen->errors++;
	seen->kind = kind;
}

static int
process(const struct st_rip3_command_host *host, const char *command, int diagnostic)
{
	struct observation seen = {0};
	const struct st_rip3_command_sink sink = {&seen,error};

	st_rip3_commands_process(host, &sink, (const uint8_t *)command, strlen(command));
	CHECK(seen.errors == (diagnostic < 0 ? 0u : 1u));
	if (diagnostic >= 0) CHECK((int)seen.kind == diagnostic);
	return 0;
}

struct answer { const char *expected; unsigned replies, errors; bool mismatch; };

static void
reply(void *context, const uint8_t *bytes, size_t length)
{
	struct answer *seen = context;
	seen->replies++;
	seen->mismatch = length != strlen(seen->expected)
	    || memcmp(bytes, seen->expected, length) != 0;
}

static void
query_error(void *context, enum st_rip3_host_diagnostic kind)
{
	struct answer *seen = context;
	(void)kind;
	seen->errors++;
}

static int
query(struct st_rip3_palettes *palettes, const char *reference, const char *expected)
{
	struct st_rip3_table_host tables = {0};
	const struct st_rip3_host_variables host = {.tables=&tables};
	struct answer seen = {.expected=expected};
	const struct st_rip3_host_sink sink = {&seen,reply,query_error};

	tables.tables[ST_RIP3_TABLE_PAL].live = palettes;
	st_rip3_host_variables_process(&host, &sink, (const uint8_t *)reference, strlen(reference));
	CHECK(seen.replies == 1 && seen.errors == 0 && !seen.mismatch);
	return 0;
}

static int
scene(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	/* Literal master colours encode each low/high RGB bit separately, plus
	 * full white. Expectations are independent RGB values, not a conversion
	 * through the palette routine under test. */
	const char *all = "Q000W04100G020I0801091R0000000000";
	const uint32_t expected[16] = {0xff0000,0x550000,0xaa0000,0xff0000,
	    0x005500,0x0000ff,0x00ff00,0x000055,0x0000aa,0x0000ff,0xffffff,
	    0,0,0,0,0x0000aa};
	struct st_rip3_environments *env = NULL;
	struct st_rip3_palettes *palettes = NULL;
	struct st_rip3_surface *surface = NULL;
	struct st_pixel_buffer *capture = NULL;
	struct st_rip3_environment before, after;
	struct st_rip3_palette palette, other;
	struct st_rip3_command_host host = {0};
	const uint32_t higher = 0x123456;
	const struct st_rip3_rect high_area = {20,2,29,4};
	unsigned current;

	CHECK(st_rip3_environments_create(&env, canvas) == 0);
	CHECK(st_rip3_palettes_create(&palettes) == 0);
	CHECK(st_rip3_surface_create(&surface, width, height) == 0);
	host.environments = env; host.palettes = palettes;
	CHECK(st_rip3_palettes_get(palettes, 0, &other) == 0);
	CHECK(st_rip3_palettes_switch(palettes, 7) == 0);
	CHECK(st_rip3_environments_get(env, 0, &before) == 0);
	before.palette = 3; /* No implicit ENV reference/table activation. */
	CHECK(st_rip3_environments_set(env, 0, &before) == 0);
	CHECK(st_rip3_palettes_rgb(palettes, 200, 8, &higher, 1) == 0);
	for (int32_t x = 0; x < 16; x++) {
		const struct st_rip3_rect column = {x,0,x,7};
		CHECK(st_rip3_surface_fill(surface, NULL, &column, (uint8_t)x, ST_RIP3_COPY) == 0);
	}
	CHECK(st_rip3_surface_fill(surface, NULL, &high_area, 200, ST_RIP3_COPY) == 0);
	CHECK(process(&host, all, -1) == 0);
	CHECK(process(&host, "a0010", -1) == 0);
	CHECK(process(&host, "a0509", -1) == 0);
	CHECK(process(&host, "a0F1", -1) == 0);
	CHECK(query(palettes, "$CUR(PAL)$", "7") == 0);
	CHECK(query(palettes, "$PALENTRY(CUR,0)$", "6;63:0:0") == 0);
	CHECK(query(palettes, "$PALENTRY(CUR,5)$", "6;0:0:63") == 0);
	CHECK(st_rip3_palettes_get(palettes, 7, &palette) == 0);
	CHECK(st_rip3_surface_snapshot(surface, &palette, &capture) == 0);
	for (uint32_t y = 0; y < height; y++) {
		for (uint32_t x = 0; x < width; x++) {
			uint32_t rgb = x < 16 && y < 8 ? expected[x]
			    : x >= 20 && x <= 29 && y >= 2 && y <= 4 ? higher : expected[0];
			CHECK(capture->primary[(size_t)y * width + x] == (ST_PIXEL_COLOR_RGB_TAG | rgb));
		}
	}
	CHECK(st_rip3_palettes_current(palettes, &current) == 0 && current == 7);
	CHECK(st_rip3_palettes_get(palettes, 0, &palette) == 0);
	CHECK(memcmp(&palette, &other, sizeof(palette)) == 0);
	CHECK(st_rip3_environments_get(env, 0, &after) == 0);
	CHECK(after.palette == 3 && after.base_math == before.base_math
	    && after.world_width == before.world_width && after.world_height == before.world_height);
	st_pixel_buffer_destroy(capture);
	st_rip3_surface_destroy(surface);
	st_rip3_palettes_destroy(palettes);
	st_rip3_environments_destroy(env);
	return 0;
}

static int
validation(enum st_rip3_canvas canvas)
{
	const char *invalid[] = {"Q", "a", "a0", "a00", "a000 comment", "a0G01", "a001S",
	    "a0F?", "a?001", "Q000000000000000000000000000000", /* 15 values */
	    "Q0000000000000000000000000000000 comment", /* unfinished field */
	    "Q0101010101010101010101010101011S", /* late range error */
	    "Q0101010101010101010101010101010?"};
	struct st_rip3_environments *env = NULL;
	struct st_rip3_palettes *palettes = NULL;
	struct st_rip3_command_host host = {0}, missing = {0};
	struct st_rip3_palette before, after;
	struct st_rip3_environment value;

	CHECK(st_rip3_environments_create(&env, canvas) == 0);
	CHECK(st_rip3_palettes_create(&palettes) == 0);
	host.environments = env; host.palettes = palettes;
	CHECK(st_rip3_palettes_get(palettes, 0, &before) == 0);
	for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		CHECK(process(&host, invalid[i], ST_RIP3_COMMAND_INVALID) == 0);
		CHECK(st_rip3_palettes_get(palettes, 0, &after) == 0);
		CHECK(memcmp(&before, &after, sizeof(before)) == 0);
	}
	CHECK(process(&missing, "a0001", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	missing.environments = env;
	CHECK(process(&missing, "a0001", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	missing.environments = NULL; missing.palettes = palettes;
	CHECK(process(&missing, "a0001", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(process(&host, "Q000102030405060708090A0B0C0D0E0F tail", -1) == 0);
	CHECK(process(&host, "a0202 tail", -1) == 0);
	CHECK(st_rip3_palettes_get(palettes, 0, &after) == 0 && after.rgb[2] == 0x00aa00);
	CHECK(process(&host, "a--W", ST_RIP3_COMMAND_INVALID) == 0);
	CHECK(process(&host, "a.W", -1) == 0);
	CHECK(st_rip3_palettes_get(palettes, 0, &after) == 0 && after.rgb[0] == 0x550000);
	CHECK(process(&host, "Q0000000000000000000000000000001", -1) == 0);
	CHECK(st_rip3_palettes_get(palettes, 0, &after) == 0 && after.rgb[15] == 0x0000aa);
	CHECK(process(&host, "J1S", -1) == 0);
	CHECK(process(&host, "a0F&", -1) == 0);
	CHECK(st_rip3_palettes_get(palettes, 0, &before) == 0 && before.rgb[15] == 0xffffff);
	CHECK(process(&host, "a0F10", ST_RIP3_COMMAND_INVALID) == 0); /* UltraNum 64. */
	CHECK(process(&host, "a--W", ST_RIP3_COMMAND_INVALID) == 0);
	CHECK(st_rip3_palettes_get(palettes, 0, &after) == 0);
	CHECK(memcmp(&before, &after, sizeof(before)) == 0);
	CHECK(process(&host, "Q0000000000000000000000000000000&", -1) == 0);
	CHECK(process(&host, "a010#", -1) == 0);
	CHECK(st_rip3_palettes_get(palettes, 0, &after) == 0 && after.rgb[1] == 0xffff55);
	CHECK(st_rip3_environments_get(env, 0, &value) == 0);
	CHECK(st_rip3_environments_define(env, 1, &value) == 0);
	CHECK(st_rip3_environments_select(env, 1) == 0);
	CHECK(st_rip3_environments_protect(env, 1, true) == 0);
	CHECK(process(&host, "a0001", -1) == 0); /* ENV protection is not palette protection. */
	CHECK(st_rip3_palettes_switch(palettes, 1) == 0);
	CHECK(st_rip3_palettes_get(palettes, 1, &before) == 0);
	CHECK(st_rip3_palettes_protect(palettes, 1, true) == 0);
	CHECK(process(&host, "a0001", ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	CHECK(process(&host, "Q00000000000000000000000000000000", ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	CHECK(st_rip3_palettes_get(palettes, 1, &after) == 0);
	CHECK(memcmp(&before, &after, sizeof(before)) == 0);
	CHECK(st_rip3_palettes_protect(palettes, 1, false) == 0);
	st_rip3_commands_process(&host, NULL, (const uint8_t *)"a0F10", 5);
	CHECK(process(&host, "a0001", -1) == 0);
	CHECK(query(palettes, "$PALENTRY(CUR,0)$", "6;0:0:42") == 0);
	st_rip3_palettes_destroy(palettes);
	st_rip3_environments_destroy(env);
	return 0;
}

static int
wire_drawing_palette(void)
{
	struct st_rip3_environments *env = NULL;
	struct st_rip3_palettes *palettes = NULL;
	struct st_rip3_palette rgb, before;
	struct st_rip3_palette_info info;
	CHECK(st_rip3_environments_create(&env, ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_palettes_create(&palettes) == 0);
	const struct st_rip3_command_host host = {.environments = env, .palettes = palettes};
	CHECK(process(&host, "2A500", -1) == 0 && query(palettes, "$CUR(PAL)$", "5") == 0);
	CHECK(process(&host, "D020#8&m004ZHM", -1) == 0);
	CHECK(query(palettes, "$PALENTRY(CUR,62,63)$", "6;63:0:0,4:13:21") == 0);
	CHECK(process(&host, "d3&8&", -1) == 0 && query(palettes, "$PALENTRY(CUR,255)$", "6;0:0:15") == 0);
	CHECK(process(&host, "d0z84ZHMnote", -1) == 0);
	CHECK(query(palettes, "$PALENTRY(CUR,61)$", "6;4:13:21") == 0);
	CHECK(process(&host, "D010A84ZH", -1) == 0 && query(palettes, "$PALENTRY(CUR,10)$", "6;0:18:52") == 0);
	CHECK(st_rip3_palettes_get(palettes, 5, &before) == 0);
	const char *malformed[] = {
		"D", "D00008A", "D020#8&m00", "D010#8&m000000", "D020#8&m00?000",
		"D023&8&m000000", "D003&7", "D00408", "D41008", "d0#74ZHM", "d0#8", "d408&H00",
	};
	for (size_t i = 0; i < sizeof(malformed) / sizeof(*malformed); i++)
		CHECK(process(&host, malformed[i], ST_RIP3_COMMAND_INVALID) == 0);
	CHECK(process(&host, "D00008", -1) == 0);
	CHECK(st_rip3_palettes_get(palettes, 5, &rgb) == 0 && memcmp(&rgb, &before, sizeof(rgb)) == 0);
	CHECK(rgb.rgb[61] == 0x103454 && rgb.rgb[62] == 0xff0000 && rgb.rgb[63] == 0x103454);
	CHECK(rgb.rgb[255] == 0x00003c);

	CHECK(process(&host, "2A705", -1) == 0 && query(palettes, "$CUR(PAL)$", "7") == 0);
	CHECK(st_rip3_palettes_info(palettes, 5, &info) == 0 && info.defined && info.protected);
	CHECK(st_rip3_palettes_info(palettes, 7, &info) == 0 && info.defined && info.protected);
	CHECK(process(&host, "d0#8&m00", ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	CHECK(process(&host, "2A50F", -1) == 0 && query(palettes, "$CUR(PAL)$", "5") == 0);
	CHECK(st_rip3_palettes_info(palettes, 5, &info) == 0 && !info.protected);
	CHECK(st_rip3_palettes_info(palettes, 7, &info) == 0 && !info.protected);
	CHECK(process(&host, "2A7", ST_RIP3_COMMAND_INVALID) == 0);
	CHECK(process(&host, "2A001", ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	CHECK(query(palettes, "$CUR(PAL)$", "0") == 0);
	CHECK(process(&host, "2A304", ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	CHECK(query(palettes, "$CUR(PAL)$", "3") == 0);
	CHECK(process(&host, "2A04", -1) == 0 && query(palettes, "$CUR(PAL)$", "0") == 0);
	CHECK(st_rip3_palettes_info(palettes, 3, &info) == 0 && info.protected);
	CHECK(process(&host, "2A7a", ST_RIP3_COMMAND_INVALID) == 0);
	CHECK(process(&host, "J1S", -1) == 0
	    && process(&host, "2A7a", ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	CHECK(query(palettes, "$CUR(PAL)$", "7") == 0);
	CHECK(process(&host, "2Aa01", ST_RIP3_COMMAND_INVALID) == 0);
	CHECK(query(palettes, "$CUR(PAL)$", "7") == 0);

	/* Wire protection can mark the selected but undefined record. First
	 * use keeps its RGB when the protected reset reports an error. */
	CHECK(st_rip3_palettes_reset_entry(palettes, 7) == 0);
	CHECK(process(&host, "d0#84ZHM", -1) == 0 && process(&host, "2A504", -1) == 0);
	CHECK(st_rip3_palettes_info(palettes, 7, &info) == 0 && !info.defined && info.protected);
	CHECK(process(&host, "2A702", ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	CHECK(query(palettes, "$CUR(PAL)$", "7") == 0 && query(palettes, "$PALENTRY(CUR,62)$", "6;4:13:21") == 0);
	CHECK(st_rip3_palettes_info(palettes, 7, &info) == 0 && info.defined && !info.protected);

	char full[sizeof("D40008") + 256 * 4];
	memcpy(full, "D40008", 6);
	for (size_t i = 0; i < 256; i++)
		memcpy(full + 6 + i * 4, "00&m", 4);
	full[sizeof(full) - 1] = 0;
	CHECK(process(&host, full, -1) == 0 && st_rip3_palettes_get(palettes, 7, &rgb) == 0);
	for (size_t i = 0; i < 256; i++)
		CHECK(rgb.rgb[i] == 0x000cf0);
	st_rip3_palettes_destroy(palettes);
	st_rip3_environments_destroy(env);
	return 0;
}

int
main(void)
{
	CHECK(scene(ST_RIP3_CANVAS_640_480, 640,480) == 0);
	CHECK(scene(ST_RIP3_CANVAS_800_600, 800,600) == 0);
	CHECK(scene(ST_RIP3_CANVAS_1024_768, 1024,768) == 0);
	CHECK(validation(ST_RIP3_CANVAS_640_480) == 0);
	CHECK(validation(ST_RIP3_CANVAS_800_600) == 0);
	CHECK(validation(ST_RIP3_CANVAS_1024_768) == 0);
	CHECK(wire_drawing_palette() == 0);
	return 0;
}

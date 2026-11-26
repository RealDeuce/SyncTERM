/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "io_error.h"
#include "st_pixels.h"
#include "st_rip3_backup.h"
#include "st_rip3_commands.h"
#include "st_rip3_environments.h"
#include "st_rip3_palettes.h"
#include "st_rip3_ports.h"
#include "st_rip3_stream.h"
#include "st_rip3_styles.h"

#define CHECK(expression)                                                                        \
	do {                                                                                     \
		if (!(expression)) {                                                             \
			fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
			return 1;                                                                \
		}                                                                                \
	} while (0)

struct observation {
	unsigned                        errors;
	enum st_rip3_command_diagnostic kind;
};

static void
diagnostic(void *context, enum st_rip3_command_diagnostic kind)
{
	struct observation *seen = context;
	seen->errors++;
	seen->kind = kind;
}

static int
process(const struct st_rip3_command_host *host, const char *command, int error)
{
	struct observation                seen = {0};
	const struct st_rip3_command_sink sink = {&seen, diagnostic};
	st_rip3_commands_process(host, &sink, (const uint8_t *)command, strlen(command));
	if (seen.errors != (error < 0 ? 0u : 1u) || (error >= 0 && (int)seen.kind != error))
		fprintf(stderr, "%s: errors %u, kind %d, expected %d\n", command, seen.errors, (int)seen.kind,
		    error);
	CHECK(seen.errors == (error < 0 ? 0u : 1u));
	if (error >= 0)
		CHECK((int)seen.kind == error);
	return 0;
}

static int
create(struct st_rip3_command_host *host, enum st_rip3_canvas canvas, uint32_t world_width, uint32_t world_height)
{
	struct st_rip3_style initial = {
	    .drawing_color    = 12,
	    .background_color = 1,
	    .fill_color       = 7,
	    .pen              = {.pattern = 0xffff, .thickness = 1, .draw_background = true},
	    .font             = {0, 0, 1, 0},
	    .write_mode       = ST_RIP3_COPY,
	    .borders          = true
        };
	struct st_rip3_environment environment;
	uint32_t                   colors[256];
	CHECK(st_rip3_fill_solid(&initial.fill, true) == 0);
	memcpy(initial.fill.rows, (const uint8_t[]) {170, 85, 170, 85, 170, 85, 170, 85}, 8);
	CHECK(st_rip3_styles_create(&host->styles, &initial) == 0);
	CHECK(st_rip3_environments_create(&host->environments, canvas) == 0);
	CHECK(st_rip3_environments_get(host->environments, 0, &environment) == 0);
	environment.world_width  = world_width;
	environment.world_height = world_height;
	CHECK(st_rip3_environments_set(host->environments, 0, &environment) == 0);
	CHECK(st_rip3_ports_create(&host->ports, canvas) == 0);
	CHECK(st_rip3_palettes_create(&host->palettes) == 0);
	for (unsigned i = 0; i < 256; i++)
		colors[i] = i * UINT32_C(0x010101);
	CHECK(st_rip3_palettes_rgb(host->palettes, 0, 8, colors, 256) == 0);
	return 0;
}

static void
destroy(struct st_rip3_command_host *host)
{
	st_rip3_styles_destroy(host->styles);
	st_rip3_environments_destroy(host->environments);
	st_rip3_ports_destroy(host->ports);
	st_rip3_palettes_destroy(host->palettes);
}

struct stream_state {
	const struct st_rip3_command_host *host;
	unsigned                           errors;
};

static void
stream_error(void *context, enum st_rip3_command_diagnostic kind)
{
	struct stream_state *state = context;
	(void)kind;
	state->errors++;
}

static void
stream_command(void *context, const uint8_t *bytes, size_t length)
{
	struct stream_state              *state = context;
	const struct st_rip3_command_sink sink  = {state, stream_error};
	st_rip3_commands_process(state->host, &sink, bytes, length);
}

static int
state(void)
{
	struct st_rip3_command_host host = {0}, missing;
	struct st_rip3_environment  environment;
	struct st_rip3_style        before, after;
	struct st_rip3_backup      *backup = NULL;
	unsigned                    current;
	CHECK(create(&host, ST_RIP3_CANVAS_640_480, 640, 480) == 0);
	CHECK(st_rip3_styles_get(host.styles, 0, &before) == 0 && before.borders);
	for (unsigned base = 36; base <= 64; base += 28) {
		CHECK(st_rip3_environments_get(host.environments, 0, &environment) == 0);
		environment.base_math       = base;
		environment.coordinate_size = 5;
		CHECK(st_rip3_environments_set(host.environments, 0, &environment) == 0);
		const char *const commands[] = {"N00", "N01", "N0", "N1", "N02", "N2", base == 36 ? "NZZ" : "N&&"};
		for (unsigned i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
			CHECK(process(&host, commands[i], -1) == 0);
			CHECK(st_rip3_styles_get(host.styles, 0, &after) == 0
			      && after.borders == (i != 0 && i != 2));
			CHECK(after.drawing_color == before.drawing_color
			      && after.background_color == before.background_color
			      && after.fill_color == before.fill_color && after.pen.pattern == before.pen.pattern
			      && after.line_type == before.line_type && after.pen.thickness == before.pen.thickness
			      && after.pen.draw_background == before.pen.draw_background
			      && after.write_mode == before.write_mode
			      && memcmp(after.fill.rows, before.fill.rows, 8) == 0);
		}
		const char *const invalid[] = {"N", "N0 comment", "N?", "N0?"};
		for (unsigned i = 0; i < 4; i++) {
			CHECK(process(&host, invalid[i], ST_RIP3_COMMAND_INVALID) == 0);
			CHECK(st_rip3_styles_get(host.styles, 0, &after) == 0 && after.borders);
		}
		CHECK(process(&host, "N01 comment", -1) == 0);
	}
	/* Style ownership is independent of ENV/PORT activity and protection. */
	CHECK(st_rip3_environments_define(host.environments, 1, &environment) == 0);
	CHECK(st_rip3_environments_select(host.environments, 1) == 0
	      && st_rip3_environments_protect(host.environments, 1, true) == 0);
	CHECK(st_rip3_ports_activate(host.ports, false) == 0 && process(&host, "N00", -1) == 0);
	CHECK(st_rip3_styles_get(host.styles, 0, &after) == 0 && !after.borders);
	CHECK(st_rip3_styles_switch(host.styles, 1) == 0);
	CHECK(st_rip3_styles_get(host.styles, 1, &after) == 0 && after.borders && after.drawing_color == 15);
	CHECK(st_rip3_styles_protect(host.styles, 1, true) == 0);
	CHECK(process(&host, "N00", ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	CHECK(process(&host, "N02", ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	CHECK(st_rip3_styles_get(host.styles, 1, &after) == 0 && after.borders);
	CHECK(st_rip3_styles_protect(host.styles, 1, false) == 0 && process(&host, "N00", -1) == 0);
	CHECK(st_rip3_styles_switch(host.styles, 0) == 0 && st_rip3_styles_get(host.styles, 0, &after) == 0
	      && !after.borders);
	CHECK(process(&host, "N01", -1) == 0);
	CHECK(st_rip3_styles_switch(host.styles, 1) == 0 && st_rip3_styles_get(host.styles, 1, &after) == 0
	      && !after.borders);
	CHECK(st_rip3_styles_backup_create(&backup) == 0
	      && st_rip3_backup_save(backup, ST_RIP3_BACKUP_BASE, 0, host.styles) == 0);
	CHECK(process(&host, "N01", -1) == 0 && st_rip3_styles_switch(host.styles, 0) == 0);
	CHECK(process(&host, "N00", -1) == 0);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_BASE, 0, host.styles) == 0);
	CHECK(st_rip3_styles_current(host.styles, &current) == 0 && current == 0);
	CHECK(st_rip3_styles_get(host.styles, 0, &after) == 0 && after.borders);
	CHECK(st_rip3_styles_get(host.styles, 1, &after) == 0 && !after.borders);
	missing          = host;
	missing.ports    = NULL;
	missing.palettes = NULL;
	CHECK(process(&missing, "N00", -1) == 0);
	missing.styles = NULL;
	CHECK(process(&missing, "N01", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	missing              = host;
	missing.environments = NULL;
	CHECK(process(&missing, "N01", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	struct stream_state              state  = {&host, 0};
	const struct st_rip3_stream_sink sink   = {.context = &state,
	    .command                                      = stream_command,
	    .diagnostic                                   = stream_error};
	struct st_rip3_stream           *stream = NULL;
	CHECK(st_rip3_stream_create(&stream, &sink, host.environments) == 0);
	const char *wire = "!|N?|N00|N02\r";
	for (size_t i = 0; wire[i] != 0; i++)
		st_rip3_stream_process(stream, (const uint8_t *)wire + i, 1);
	CHECK(state.errors == 1 && st_rip3_styles_get(host.styles, 0, &after) == 0 && after.borders);
	st_rip3_stream_destroy(stream);
	st_rip3_backup_destroy(backup);
	destroy(&host);
	return 0;
}

static int
render(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	struct st_rip3_command_host host = {0};
	struct st_rip3_style        style;
	struct st_rip3_palette      palette;
	struct st_pixel_buffer     *capture = NULL;
	CHECK(create(&host, canvas, width, height) == 0);
	CHECK(st_rip3_styles_get(host.styles, 0, &style) == 0 && st_rip3_fill_solid(&style.fill, true) == 0);
	CHECK(st_rip3_styles_set(host.styles, 0, &style) == 0);
	CHECK(st_rip3_styles_define(host.styles, 1, &style) == 0);
	CHECK(process(&host, "N00", -1) == 0);
	CHECK(process(&host, "K01010505", -1) == 0);
	CHECK(st_rip3_styles_switch(host.styles, 1) == 0 && process(&host, "K08010C05", -1) == 0);
	CHECK(process(&host, "N00", -1) == 0 && process(&host, "B0F010J05", -1) == 0);
	CHECK(process(&host, "N01", -1) == 0 && process(&host, "B0M010Q05", -1) == 0);
	CHECK(st_rip3_palettes_get(host.palettes, 0, &palette) == 0
	      && st_rip3_ports_snapshot(host.ports, &palette, &capture) == 0);
	for (uint32_t y = 0; y < height; y++)
		for (uint32_t x = 0; x < width; x++) {
			unsigned expected = 0;
			if (x >= 1 && x < 5 && y >= 1 && y < 5)
				expected = 7;
			if (x >= 8 && x <= 12 && y >= 1 && y <= 5)
				expected = x == 8 || x == 12 || y == 1 || y == 5 ? 12 : 7;
			if (((x >= 15 && x <= 19) || (x >= 22 && x <= 26)) && y >= 1 && y <= 5)
				expected = 7;
			CHECK(capture->primary[(size_t)y * width + x]
			      == (ST_PIXEL_COLOR_RGB_TAG | expected * UINT32_C(0x010101)));
		}
	st_pixel_buffer_destroy(capture);
	destroy(&host);
	return 0;
}

int
main(void)
{
	CHECK(state() == 0);
	CHECK(render(ST_RIP3_CANVAS_640_480, 640, 480) == 0);
	CHECK(render(ST_RIP3_CANVAS_800_600, 800, 600) == 0);
	CHECK(render(ST_RIP3_CANVAS_1024_768, 1024, 768) == 0);
	return 0;
}

/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "st_rip3_backup.h"
#include "st_rip3_commands.h"
#include "st_rip3_environments.h"
#include "st_rip3_ports.h"
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
process(const struct st_rip3_command_host *host, const char *command, int expected)
{
	struct observation                seen = {0};
	const struct st_rip3_command_sink sink = {&seen, diagnostic};
	st_rip3_commands_process(host, &sink, (const uint8_t *)command, strlen(command));
	if (seen.errors != (expected < 0 ? 0u : 1u) || (expected >= 0 && (int)seen.kind != expected))
		fprintf(stderr, "command %s: errors %u, kind %d, expected %d\n", command, seen.errors,
		    (int)seen.kind, expected);
	CHECK(seen.errors == (expected < 0 ? 0u : 1u));
	if (expected >= 0)
		CHECK((int)seen.kind == expected);
	return 0;
}

static int
create(struct st_rip3_command_host *host, enum st_rip3_canvas canvas)
{
	struct st_rip3_style style;
	CHECK(st_rip3_style_defaults(&style) == 0);
	CHECK(st_rip3_styles_create(&host->styles, &style) == 0);
	CHECK(st_rip3_environments_create(&host->environments, canvas) == 0);
	return 0;
}

static void
destroy(struct st_rip3_command_host *host)
{
	st_rip3_styles_destroy(host->styles);
	st_rip3_environments_destroy(host->environments);
	st_rip3_ports_destroy(host->ports);
}

static bool
equal(const struct st_rip3_outline_style *a, const struct st_rip3_outline_style *b)
{
	return a->compatibility_s == b->compatibility_s && a->compatibility_f == b->compatibility_f
	       && a->flags == b->flags && a->size == b->size && a->shadow == b->shadow
	       && a->string_rotation == b->string_rotation && a->character_rotation == b->character_rotation
	       && a->spacing == b->spacing && a->baseline == b->baseline && strcmp(a->name, b->name) == 0;
}

static void
number(char *output, size_t width, unsigned value)
{
	static const char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz#&";
	while (width != 0) {
		output[--width] = digits[value % 64];
		value /= 64;
	}
}

static int
fields(void)
{
	struct st_rip3_command_host host = {0};
	struct st_rip3_style        before, after;
	struct st_rip3_environment  env;
	CHECK(create(&host, ST_RIP3_CANVAS_800_600) == 0);
	CHECK(st_rip3_styles_get(host.styles, 0, &before) == 0);
	CHECK(before.font_mode == ST_RIP3_FONT_CLASSIC && before.outline.spacing == 100
	      && before.outline.name[0] == 0 && before.outline.size == 0 && before.outline.shadow == 0);
	CHECK(st_rip3_environments_get(host.environments, 0, &env) == 0);
	env.world_width  = 800;
	env.world_height = 600;
	for (unsigned radix = 36; radix <= 64; radix += 28) {
		env.base_math       = radix;
		env.coordinate_size = 5;
		CHECK(st_rip3_environments_set(host.environments, 0, &env) == 0);
		/* Literal example and independently decoded values from the pinned text. */
		CHECK(process(&host, "y0000BW1Q080000001a1a000000Marin", -1) == 0);
		CHECK(st_rip3_styles_get(host.styles, 0, &after) == 0);
		const struct st_rip3_outline_style expected = {.flags = 736,
		    .size                                             = 90,
		    .shadow                                           = 8,
		    .spacing                                          = 100,
		    .name                                             = "Marin"};
		CHECK(after.font_mode == ST_RIP3_FONT_OUTLINE && equal(&after.outline, &expected));
		CHECK(after.font.font == before.font.font && after.font.magnification == before.font.magnification
		      && after.drawing_color == before.drawing_color && after.write_mode == before.write_mode);
		CHECK(process(&host, "y..000W0M....1a..Dixon", -1) == 0);
		CHECK(st_rip3_styles_get(host.styles, 0, &after) == 0);
		const struct st_rip3_outline_style short_form = {.flags = 32,
		    .size                                               = 22,
		    .spacing                                            = 100,
		    .name                                               = "Dixon"};
		CHECK(after.font_mode == ST_RIP3_FONT_OUTLINE && equal(&after.outline, &short_form));
	}
	for (unsigned sr = 0; sr <= 2700; sr += 900)
		for (unsigned cr = 0; cr <= 2700; cr += 900) {
			char command[] = "y&&&&&&&&&&0000&&01&&&&&&&&Unavailable Face";
			number(command + 11, 2, sr);
			number(command + 13, 2, cr);
			CHECK(process(&host, command, -1) == 0);
			CHECK(st_rip3_styles_get(host.styles, 0, &after) == 0);
			CHECK(after.outline.compatibility_s == 63 && after.outline.compatibility_f == 63
			      && after.outline.flags == 0xffffff && after.outline.size == 4095
			      && after.outline.shadow == 4095 && after.outline.string_rotation == sr
			      && after.outline.character_rotation == cr && after.outline.baseline == 4095
			      && after.outline.spacing == 1
			      && strcmp(after.outline.name, "Unavailable Face") == 0);
		}
	/* Zero size is a deferred render error. Ignored fields accept their entire
	 * digit domains, including the 36-bit reserved value. */
	CHECK(process(&host, "y0000000000000000&&&&&&&&&&DEFAULT", -1) == 0);
	CHECK(st_rip3_styles_get(host.styles, 0, &after) == 0 && after.outline.size == 0
	      && after.outline.spacing == 4095);
	char long_name[140] = "y00000001000000001a00000000";
	memset(long_name + 27, 'A', 100);
	long_name[127] = 0;
	CHECK(process(&host, long_name, -1) == 0);
	memset(long_name, 'B', sizeof(long_name)); /* No borrowed argument storage. */
	CHECK(st_rip3_styles_get(host.styles, 0, &after) == 0 && strlen(after.outline.name) == 39);
	for (unsigned i = 0; i < 39; i++)
		CHECK(after.outline.name[i] == 'A');
	destroy(&host);
	return 0;
}

static int
baseline(enum st_rip3_canvas canvas, unsigned height)
{
	struct st_rip3_command_host host = {0};
	struct st_rip3_environment  env;
	struct st_rip3_style        style;
	struct st_rip3_port_info    port;
	CHECK(create(&host, canvas) == 0);
	CHECK(st_rip3_ports_create(&host.ports, canvas) == 0);
	CHECK(st_rip3_ports_activate(host.ports, false) == 0);
	CHECK(st_rip3_environments_get(host.environments, 0, &env) == 0);
	env.world_height   = 960;
	env.graphics_style = 7;
	env.port           = 9;
	CHECK(st_rip3_environments_define(host.environments, 1, &env) == 0
	      && st_rip3_environments_select(host.environments, 1) == 0
	      && st_rip3_environments_protect(host.environments, 1, true) == 0);
	/* Neither current port activation nor ENV protection owns this STYLE edit. */
	CHECK(process(&host, "y0000C001000000011a00000000Marin", -1) == 0);
	CHECK(st_rip3_styles_get(host.styles, 0, &style) == 0 && style.outline.baseline == height / 960
	      && style.outline.flags == 768);
	CHECK(process(&host, "y00008001000000&&1a00000000Marin", -1) == 0);
	CHECK(st_rip3_styles_get(host.styles, 0, &style) == 0 && style.outline.baseline == 4095u * height / 960);
	unsigned stored = style.outline.baseline;
	CHECK(st_rip3_environments_protect(host.environments, 1, false) == 0);
	env.world_height = 1;
	CHECK(st_rip3_environments_set(host.environments, 1, &env) == 0);
	CHECK(st_rip3_styles_get(host.styles, 0, &style) == 0 && style.outline.baseline == stored);
	CHECK(process(&host, "y00008001000000&&1a00000000Marin", -1) == 0);
	CHECK(st_rip3_styles_get(host.styles, 0, &style) == 0 && style.outline.baseline == 4095u * height);
	CHECK(st_rip3_ports_info(host.ports, 0, &port) == 0 && !port.viewport_active && port.cursor.x == 0
	      && port.cursor.y == 0);
	destroy(&host);
	return 0;
}

static int
errors_and_attributes(void)
{
	struct st_rip3_command_host host = {0};
	struct st_rip3_style        before, after;
	struct st_rip3_environment  env;
	CHECK(create(&host, ST_RIP3_CANVAS_640_480) == 0);
	CHECK(process(&host, "q01", ST_RIP3_COMMAND_INVALID) == 0);
	CHECK(process(&host, "y00000001000000001a00000000Marin", -1) == 0);
	CHECK(st_rip3_styles_get(host.styles, 0, &before) == 0);
	static const char *invalid[] = {
	    "y",
	    "y00000001000000001a00000000",
	    "y00000001000000000000000000Marin",
	    "y00000001000100001a00000000Marin",
	    "y00000001000001001a00000000Marin",
	    "y00000001000000001a0000000?Marin",
	    "y00000001000000001a0000000-Marin",
	    "y..000W0M....1a..",
	    "y..000W0M....1a.00.000Dixon",
	    "q",
	    "q0.",
	    "q0G",
	    "q?0",
	};
	for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		CHECK(process(&host, invalid[i], ST_RIP3_COMMAND_INVALID) == 0);
		CHECK(st_rip3_styles_get(host.styles, 0, &after) == 0 && equal(&after.outline, &before.outline)
		      && after.font_mode == before.font_mode);
	}
	CHECK(process(&host, "y00000001000000001a00000000$&FONT$", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(st_rip3_environments_get(host.environments, 0, &env) == 0);
	for (unsigned base = 36; base <= 64; base += 28) {
		env.base_math = base;
		CHECK(st_rip3_environments_set(host.environments, 0, &env) == 0);
		CHECK(process(&host, "y0000&m01000000001a00000000Marin", -1) == 0); /* flags=4080 */
		CHECK(process(&host, "q1", -1) == 0 && process(&host, "q02 comment", -1) == 0
		      && process(&host, "q04", -1) == 0 && process(&host, "q08", -1) == 0
		      && process(&host, "q.", -1) == 0);
		CHECK(st_rip3_styles_get(host.styles, 0, &after) == 0 && after.outline.flags == 4095);
		CHECK(process(&host, "q0F", -1) == 0);
		CHECK(process(&host, "q0G", ST_RIP3_COMMAND_INVALID) == 0);
	}
	CHECK(st_rip3_styles_define(host.styles, 1, &after) == 0 && st_rip3_styles_switch(host.styles, 1) == 0
	      && st_rip3_styles_protect(host.styles, 1, true) == 0);
	before = after;
	CHECK(process(&host, "q01", ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	CHECK(process(&host, "y00000001000000001a00000000Dixon", ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	CHECK(st_rip3_styles_get(host.styles, 1, &after) == 0 && equal(&after.outline, &before.outline));
	CHECK(st_rip3_styles_protect(host.styles, 1, false) == 0);
	CHECK(process(&host, "Y00000100", -1) == 0 && process(&host, "q01", ST_RIP3_COMMAND_INVALID) == 0);
	CHECK(st_rip3_styles_get(host.styles, 1, &after) == 0 && after.font_mode == ST_RIP3_FONT_CLASSIC
	      && equal(&after.outline, &before.outline));
	/* Parser-owned failure does not poison later commands, including without a sink. */
	st_rip3_commands_process(&host, NULL, (const uint8_t *)"q?", 2);
	CHECK(process(&host, "y00000001000000001a00000000Dixon", -1) == 0);
	destroy(&host);
	return 0;
}

static int
ownership(void)
{
	struct st_rip3_command_host host   = {0};
	struct st_rip3_backup      *backup = NULL;
	struct st_rip3_style        saved, after;
	CHECK(create(&host, ST_RIP3_CANVAS_640_480) == 0);
	CHECK(process(&host, "y0000BW1Q080000001a1a000000Marin", -1) == 0);
	CHECK(st_rip3_styles_get(host.styles, 0, &saved) == 0);
	CHECK(st_rip3_styles_define(host.styles, 1, &saved) == 0);
	CHECK(st_rip3_styles_backup_create(&backup) == 0
	      && st_rip3_backup_save(backup, ST_RIP3_BACKUP_BASE, 0, host.styles) == 0);
	CHECK(process(&host, "y00000001000000001a00000000Dixon", -1) == 0);
	CHECK(st_rip3_styles_switch(host.styles, 1) == 0 && st_rip3_styles_get(host.styles, 1, &after) == 0
	      && equal(&saved.outline, &after.outline));
	CHECK(process(&host, "q0F", -1) == 0);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_BASE, 0, host.styles) == 0
	      && st_rip3_styles_get(host.styles, 1, &after) == 0 && equal(&saved.outline, &after.outline)
	      && after.font_mode == ST_RIP3_FONT_OUTLINE);
	CHECK(st_rip3_styles_get(host.styles, 0, &after) == 0 && equal(&saved.outline, &after.outline));
	CHECK(st_rip3_styles_switch(host.styles, 2) == 0 && st_rip3_styles_get(host.styles, 2, &after) == 0
	      && after.font_mode == ST_RIP3_FONT_CLASSIC && after.outline.spacing == 100
	      && after.outline.name[0] == 0);
	st_rip3_backup_destroy(backup);
	destroy(&host);
	return 0;
}

int
main(void)
{
	CHECK(fields() == 0 && errors_and_attributes() == 0 && ownership() == 0);
	CHECK(baseline(ST_RIP3_CANVAS_640_480, 480) == 0);
	CHECK(baseline(ST_RIP3_CANVAS_800_600, 600) == 0);
	CHECK(baseline(ST_RIP3_CANVAS_1024_768, 768) == 0);
	return 0;
}

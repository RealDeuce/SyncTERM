/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_stream.h"
#include "st_rip3_commands.h"
#include "st_rip3_environments.h"
#include "st_rip3_palettes.h"
#include "st_pixels.h"
#include "io_error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
		return 1; \
	} \
} while (0)

_Static_assert(_Generic(&st_rip3_stream_process,
    void (*)(struct st_rip3_stream *, const uint8_t *, size_t): 1, default: 0),
    "Incoming RIP stream errors must never escape to the session");

struct observation {
	uint8_t text[256], commands[512];
	size_t text_length, command_length, calls;
	unsigned errors;
	bool overflow, column_one;
};

static void
text(void *context, const uint8_t *bytes, size_t length)
{
	struct observation *seen = context;
	if (length > sizeof(seen->text) - seen->text_length) {
		seen->overflow = true;
		return;
	}
	memcpy(seen->text + seen->text_length, bytes, length);
	seen->text_length += length;
}

static void
command(void *context, const uint8_t *bytes, size_t length)
{
	struct observation *seen = context;
	if (length + 1 > sizeof(seen->commands) - seen->command_length) {
		seen->overflow = true;
		return;
	}
	memcpy(seen->commands + seen->command_length, bytes, length);
	seen->command_length += length;
	seen->commands[seen->command_length++] = 0;
	seen->calls++;
}

static bool
column_one(void *context)
{
	return ((struct observation *)context)->column_one;
}

static void
diagnostic(void *context, enum st_rip3_stream_diagnostic kind)
{
	struct observation *seen = context;
	(void)kind;
	seen->errors++;
}

static int
example(const uint8_t *input, size_t length, const uint8_t *tty, size_t tty_length,
    const uint8_t *commands, size_t commands_length, size_t calls)
{
	/* Every single split, plus one-byte input, must produce the same literal
	 * expected transcript. No output from the parser supplies expectations. */
	for (size_t split = 0; split <= length + 1; split++) {
		struct observation seen = {0};
		const struct st_rip3_stream_sink sink = {&seen,text,command,column_one,diagnostic};
		struct st_rip3_stream *stream = NULL;

		CHECK(st_rip3_stream_create(&stream, &sink) == 0);
		if (split <= length) {
			st_rip3_stream_process(stream, input, split);
			st_rip3_stream_process(stream, input + split, length - split);
		}
		else for (size_t i = 0; i < length; i++)
			st_rip3_stream_process(stream, input + i, 1);
		CHECK(!seen.overflow && seen.errors == 0 && seen.calls == calls);
		CHECK(seen.text_length == tty_length && memcmp(seen.text, tty, tty_length) == 0);
		CHECK(seen.command_length == commands_length
		    && memcmp(seen.commands, commands, commands_length) == 0);
		st_rip3_stream_destroy(stream);
	}
	return 0;
}

#define EXAMPLE(input, tty, commands, calls) \
	CHECK(example((const uint8_t *)(input), sizeof(input)-1, \
	    (const uint8_t *)(tty), sizeof(tty)-1, \
	    (const uint8_t *)(commands), sizeof(commands)-1, calls) == 0)

static int
boundaries(void)
{
	EXAMPLE("!|J10|n2000|fZKQO\r\nraw\r\n", "\nraw\r\n", "J10\0n2000\0fZKQO\0", 3);
	EXAMPLE("prefix !|J10\r\n!oops\r\n!|a0001\r", "prefix !|J10\r\n!oops\r\n", "a0001\0", 1);
	EXAMPLE("raw\x01|J10|fZKQO\r!|a0F1\r", "raw", "J10\0fZKQO\0a0F1\0", 3);
	EXAMPLE("raw\x02|n2\r", "raw", "n2\0", 1);
	EXAMPLE("\x01oops\x02?\r!|J10\r", "\x01oops\x02?\r", "J10\0", 1);
	EXAMPLE("\\!|J10\r!|J10\r", "\\!|J10\r", "J10\0", 1);
	EXAMPLE("\\\x01|J10\r!|J10\r", "\\\x01|J10\r", "J10\0", 1);
	EXAMPLE("x\v!|J10\r", "x\v", "J10\0", 1);
	EXAMPLE("x\0!|J10\r", "x\0", "J10\0", 1);
	EXAMPLE("!|||\r!|\r!|n2||J10|\r", "", "n2\0J10\0", 2);
	EXAMPLE("!|fZK\\\r\nQO|J1\\\r0\r", "", "fZKQO\0J10\0", 2);
	EXAMPLE("!|@0000one\\|two\\!three\\\\\r", "", "@0000one\\|two\\!three\\\\\0", 1);
	EXAMPLE("!|@0000end\\\\\\\r\ncontinued\r", "", "@0000end\\\\continued\0", 1);
	EXAMPLE("!|@0000end\\\\|J10\r", "", "@0000end\\\\\0J10\0", 2);
	EXAMPLE("!|J1\n0\r", "", "J10\0", 1);
	EXAMPLE("!|!$Name$\\|J10|!comment\\\r\ncontinued|n2\r", "", "!$Name$\\\0J10\0!commentcontinued\0n2\0", 4);
	/* Levels, ESC command characters and host quoting stay opaque here. */
	EXAMPLE("!|15Largs|1\x1b" "0000\\$Name\\$|123456789X0\r", "", "15Largs\0" "1\x1b" "0000\\$Name\\$\0" "123456789X0\0", 3);
	EXAMPLE("\x1b[31m\xc4\xb3\r!|@0000\xc4\xb3\r", "\x1b[31m\xc4\xb3\r", "@0000\xc4\xb3\0", 1);
	EXAMPLE("\xc3\xa9\xe2\x94\x82\r!|@0000\xc3\xa9\xe2\x94\x82\r", "\xc3\xa9\xe2\x94\x82\r", "@0000\xc3\xa9\xe2\x94\x82\0", 1);
	return 0;
}

static int
lifetime(void)
{
	struct observation seen = {0};
	const struct st_rip3_stream_sink sink = {&seen,text,command,column_one,diagnostic};
	struct st_rip3_stream *stream = NULL, *sentinel = NULL;

	CHECK(st_rip3_stream_create(NULL, &sink) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_stream_create(&sentinel, NULL) == 0);
	st_rip3_stream_process(sentinel, (const uint8_t *)"!|J10\r", 6);
	st_rip3_stream_destroy(sentinel);
	CHECK(st_rip3_stream_create(&stream, &sink) == 0);
	st_rip3_stream_process(stream, (const uint8_t *)"x", 1);
	seen.column_one = true; /* The text service observed ANSI cursor motion. */
	st_rip3_stream_process(stream, (const uint8_t *)"!|J10\r", 6);
	CHECK(seen.calls == 1 && seen.text_length == 1);
	st_rip3_stream_process(stream, (const uint8_t *)"!|fZK", 5);
	st_rip3_stream_process(stream, NULL, 0);
	st_rip3_stream_process(stream, NULL, 1);
	st_rip3_stream_process(stream, (const uint8_t *)"bad", SIZE_MAX);
	CHECK(seen.errors == 2 && seen.calls == 1);
	st_rip3_stream_process(stream, (const uint8_t *)"QO\r", 3);
	CHECK(seen.calls == 2 && memcmp(seen.commands + 4, "fZKQO", 5) == 0);
	st_rip3_stream_process(stream, (const uint8_t *)"!|a000", 6);
	st_rip3_stream_reset(stream); /* Partial commands must never execute. */
	st_rip3_stream_process(stream, (const uint8_t *)"!|n2\r", 5);
	CHECK(seen.calls == 3);
	st_rip3_stream_process(stream, (const uint8_t *)"!", 1);
	st_rip3_stream_reset(stream);
	st_rip3_stream_process(stream, (const uint8_t *)"ok", 2);
	CHECK(seen.text_length == 3 && memcmp(seen.text, "xok", 3) == 0);
	st_rip3_stream_process(stream, (const uint8_t *)"\r!|unfinished", 13);
	st_rip3_stream_destroy(stream);
	CHECK(seen.calls == 3 && seen.errors == 2);
	st_rip3_stream_reset(NULL);
	st_rip3_stream_destroy(NULL);
	st_rip3_stream_process(NULL, NULL, 0);
	return 0;
}

struct large_observation { size_t length, calls; bool mismatch; };

static void
large_command(void *context, const uint8_t *bytes, size_t length)
{
	struct large_observation *seen = context;
	seen->calls++;
	seen->length = length;
	for (size_t i = 0; i < length; i++)
		if (bytes[i] != 'x') seen->mismatch = true;
}

static int
long_command(void)
{
	const size_t count = 1024 * 1024 + 7;
	struct large_observation seen = {0};
	const struct st_rip3_stream_sink sink = {.context=&seen,.command=large_command};
	struct st_rip3_stream *stream = NULL;
	uint8_t *input = malloc(count);

	CHECK(input != NULL && st_rip3_stream_create(&stream, &sink) == 0);
	memset(input, 'x', count);
	st_rip3_stream_process(stream, (const uint8_t *)"!|", 2);
	for (size_t start = 0; start < count;) {
		size_t chunk = count - start > 127 ? 127 : count - start;
		st_rip3_stream_process(stream, input + start, chunk);
		start += chunk;
	}
	st_rip3_stream_process(stream, (const uint8_t *)"\r", 1);
	CHECK(seen.calls == 1 && seen.length == count && !seen.mismatch);
	free(input);
	st_rip3_stream_destroy(stream);
	return 0;
}

struct scene {
	struct st_rip3_command_host host;
	unsigned errors;
};

static void
scene_error(void *context, enum st_rip3_command_diagnostic kind)
{
	struct scene *scene = context;
	(void)kind;
	scene->errors++;
}

static void
scene_command(void *context, const uint8_t *bytes, size_t length)
{
	struct scene *scene = context;
	const struct st_rip3_command_sink sink = {scene,scene_error};
	st_rip3_commands_process(&scene->host, &sink, bytes, length);
}

static int
noops(void)
{
	struct scene scene = {0};
	const char *commands[] = {"!", "!$Name$\\[literal\\]", "(", ")", ")", "(", "("};
	const struct st_rip3_command_sink sink = {&scene,scene_error};

	for (size_t i = 0; i < sizeof(commands)/sizeof(commands[0]); i++)
		scene_command(&scene, (const uint8_t *)commands[i], strlen(commands[i]));
	CHECK(scene.errors == 0); /* No bindings, expansion, stack or matching needed. */
	st_rip3_commands_process(&scene.host, &sink, (const uint8_t *)"(extra", 6);
	st_rip3_commands_process(&scene.host, &sink, (const uint8_t *)")extra", 6);
	CHECK(scene.errors == 2);
	scene_command(&scene, (const uint8_t *)"!$NoExpansion$", 14);
	CHECK(scene.errors == 2);
	return 0;
}

static int
integration(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	static const uint8_t input[] = "!|!$NoExpansion$|(|(|)|)|J1S|n2000|fK0F0|a010&|a0F10|a020#\r\n"
	    "!|fZK\\\r\nQO|J10|fZKQO|a0304\r";
	struct scene scene = {0};
	const struct st_rip3_stream_sink sink = {.context=&scene,.command=scene_command};
	struct st_rip3_stream *stream = NULL;
	struct st_rip3_surface *surface = NULL;
	struct st_rip3_palette palette;
	struct st_rip3_environment env;
	struct st_pixel_buffer *capture = NULL;

	CHECK(st_rip3_environments_create(&scene.host.environments, canvas) == 0);
	CHECK(st_rip3_palettes_create(&scene.host.palettes) == 0);
	CHECK(st_rip3_surface_create(&surface, width,height) == 0);
	for (int32_t x = 1; x <= 3; x++)
		CHECK(st_rip3_surface_plot(surface, NULL, x,0, (uint8_t)x, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_stream_create(&stream, &sink) == 0);
	for (size_t i = 0; i < sizeof(input)-1; i++)
		st_rip3_stream_process(stream, input+i, 1);
	CHECK(scene.errors == 1); /* a0F10 is 64 in UltraNum, then parsing recovers. */
	CHECK(st_rip3_environments_get(scene.host.environments, 0, &env) == 0);
	CHECK(env.base_math == 36 && env.coordinate_size == 2
	    && env.world_width == 1280 && env.world_height == 960);
	CHECK(st_rip3_palettes_get(scene.host.palettes, 0, &palette) == 0);
	CHECK(st_rip3_surface_snapshot(surface, &palette, &capture) == 0);
	for (uint32_t y = 0; y < height; y++) {
		for (uint32_t x = 0; x < width; x++) {
			uint32_t rgb = y == 0 && x == 1 ? 0xffffff
			    : y == 0 && x == 2 ? 0xffff55 : y == 0 && x == 3 ? 0xaa0000 : 0;
			CHECK(capture->primary[(size_t)y * width + x] == (ST_PIXEL_COLOR_RGB_TAG | rgb));
		}
	}
	st_pixel_buffer_destroy(capture);
	st_rip3_surface_destroy(surface);
	st_rip3_stream_destroy(stream);
	st_rip3_palettes_destroy(scene.host.palettes);
	st_rip3_environments_destroy(scene.host.environments);
	return 0;
}

int
main(void)
{
	CHECK(boundaries() == 0);
	CHECK(lifetime() == 0);
	CHECK(long_command() == 0);
	CHECK(noops() == 0);
	CHECK(integration(ST_RIP3_CANVAS_640_480, 640,480) == 0);
	CHECK(integration(ST_RIP3_CANVAS_800_600, 800,600) == 0);
	CHECK(integration(ST_RIP3_CANVAS_1024_768, 1024,768) == 0);
	return 0;
}

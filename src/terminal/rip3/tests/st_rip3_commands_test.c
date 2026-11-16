/* Independent complete-command expectations and parser-owned recovery. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_commands.h"
#include "st_rip3_environments.h"
#include "st_rip3_host_variables.h"
#include "st_rip3_table_host.h"
#include "st_rip3_audio.h"
#include "st_audio_file.h"
#include "filesystem.h"
#include "path.h"
#include "io_error.h"

#include <stdbool.h>
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

_Static_assert(_Generic(&st_rip3_commands_process,
    void (*)(const struct st_rip3_command_host *, const struct st_rip3_command_sink *,
    const uint8_t *, size_t): 1, default: 0),
    "RIP command processing must not return a session error");

struct observations {unsigned count; enum st_rip3_command_diagnostic last;};

static void
diagnostic(void *context, enum st_rip3_command_diagnostic kind)
{
	struct observations *observed = context;
	observed->count++; observed->last = kind;
}

static int
process(const struct st_rip3_command_host *host, const char *command, int error)
{
	struct observations observed = {0};
	const struct st_rip3_command_sink sink = {&observed,diagnostic};
	st_rip3_commands_process(host, &sink, (const uint8_t *)command, strlen(command));
	CHECK(observed.count == (error < 0 ? 0u : 1u));
	if (error >= 0) CHECK((int)observed.last == error);
	return 0;
}

static int
expect(struct st_rip3_environments *env, unsigned number, unsigned base,
    unsigned coordinate, uint32_t width, uint32_t height)
{
	struct st_rip3_environment value;
	CHECK(st_rip3_environments_get(env, number, &value) == 0);
	CHECK(value.base_math == base && value.coordinate_size == coordinate
	    && value.world_width == width && value.world_height == height);
	return 0;
}

struct answer {const char *expected; unsigned replies, errors; bool mismatch;};

static void
reference_reply(void *context, const uint8_t *bytes, size_t length)
{
	struct answer *answer = context;
	answer->replies++;
	answer->mismatch = strlen(answer->expected) != length
	    || memcmp(answer->expected, bytes, length) != 0;
}

static void
reference_error(void *context, enum st_rip3_host_diagnostic kind)
{
	struct answer *answer = context;
	(void)kind;
	answer->errors++;
}

static int
query(struct st_rip3_environments *env, const char *reference, const char *expected)
{
	struct st_rip3_table_host tables = {0};
	const struct st_rip3_host_variables host = {.tables=&tables};
	struct answer answer = {.expected=expected};
	const struct st_rip3_host_sink sink = {&answer,reference_reply,reference_error};

	tables.tables[ST_RIP3_TABLE_ENV].live = env;
	st_rip3_host_variables_process(&host, &sink, (const uint8_t *)reference, strlen(reference));
	CHECK(answer.replies == 1 && answer.errors == 0 && !answer.mismatch);
	return 0;
}

static int
geometry(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	struct st_rip3_environments *env = NULL;
	struct st_rip3_command_host host = {0};
	struct st_rip3_environment value;
	struct st_rip3_frame frame;
	struct st_rip3_point world = {1280,960}, device;

	CHECK(st_rip3_environments_create(&env, canvas) == 0);
	host.environments = env;
	CHECK(st_rip3_environments_get(env, 0, &value) == 0);
	value.graphics_style = 7; value.button_style = 6; value.port = 5;
	value.text_window = 4; value.palette = 3; value.rgb_bits = 8;
	value.mouse_pointer = 2; value.baud_rate = 9600;
	CHECK(st_rip3_environments_set(env, 0, &value) == 0);
	CHECK(process(&host, "J1S", -1) == 0);
	CHECK(expect(env, 0, 64,2,640,350) == 0);
	CHECK(process(&host, "fK0F0", -1) == 0); /* UltraNum 1280x960. */
	CHECK(expect(env, 0, 64,2,1280,960) == 0);
	CHECK(query(env, "$BASEMATH$", "64") == 0);
	CHECK(query(env, "$WORLD$", "1280:960") == 0);
	CHECK(query(env, "$COORDSIZE$", "2") == 0);
	CHECK(process(&host, "fa0b0", -1) == 0);
	CHECK(expect(env, 0, 64,2,2304,2368) == 0); /* Lowercase UltraNum digits. */
	CHECK(process(&host, "f&&##", -1) == 0);
	CHECK(expect(env, 0, 64,2,4095,4030) == 0);
	CHECK(process(&host, "fK0F0", -1) == 0);
	CHECK(process(&host, "n&", -1) == 0); /* UltraNum 63 is an invalid size. */
	CHECK(expect(env, 0, 64,2,1280,960) == 0);
	CHECK(process(&host, "J10", -1) == 0); /* Always MegaNum, even from base 64. */
	CHECK(process(&host, "n2000", -1) == 0);
	CHECK(process(&host, "fHSDC", -1) == 0);
	CHECK(expect(env, 0, 36,2,640,480) == 0);
	CHECK(process(&host, "fHR9S", -1) == 0);
	CHECK(expect(env, 0, 36,2,639,352) == 0);
	CHECK(process(&host, "fHRDC", -1) == 0);
	CHECK(expect(env, 0, 36,2,639,480) == 0);
	CHECK(process(&host, "fZKQO                 Set world coordinats to 1280x960", -1) == 0);
	CHECK(st_rip3_environments_frame(env, 0, &frame) == 0);
	CHECK(frame.device_width == width && frame.device_height == height);
	CHECK(st_rip3_world_to_device(&frame, &world, &device) == 0
	    && device.x == (int32_t)width && device.y == (int32_t)height);
	CHECK(process(&host, "J1s", -1) == 0);
	CHECK(expect(env, 0, 64,2,1280,960) == 0);
	CHECK(process(&host, "J1C", -1) == 0); /* Numeric 48 falls back to base 36. */
	CHECK(expect(env, 0, 36,2,1280,960) == 0);
	CHECK(process(&host, "J1S", -1) == 0);
	CHECK(process(&host, "J1", -1) == 0); /* Short last parameter. */
	CHECK(expect(env, 0, 36,2,1280,960) == 0);
	CHECK(process(&host, "J1S", -1) == 0);
	CHECK(process(&host, "J--", -1) == 0); /* MegaNum wire '-' means zero. */
	CHECK(expect(env, 0, 36,2,1280,960) == 0);
	CHECK(process(&host, "J1S comment", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(expect(env, 0, 36,2,1280,960) == 0);
	CHECK(process(&host, "n4000", -1) == 0);
	CHECK(process(&host, "f00ZK00QO", -1) == 0);
	CHECK(expect(env, 0, 36,4,1280,960) == 0);
	CHECK(process(&host, "n5", -1) == 0); /* Omitted reserved field. */
	CHECK(expect(env, 0, 36,5,1280,960) == 0);
	CHECK(process(&host, "f0000100002", -1) == 0);
	CHECK(expect(env, 0, 36,5,1,2) == 0);
	CHECK(process(&host, "f000011", -1) == 0);
	CHECK(expect(env, 0, 36,5,1,1) == 0);
	CHECK(process(&host, "n6000", -1) == 0); /* Invalid size defaults to two. */
	CHECK(expect(env, 0, 36,2,1,1) == 0);
	CHECK(process(&host, "n-", -1) == 0);
	CHECK(process(&host, "f-1-1", -1) == 0);
	CHECK(expect(env, 0, 36,2,1,1) == 0);
	CHECK(process(&host, "nZ001", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(expect(env, 0, 36,2,1,1) == 0);
	CHECK(process(&host, "n3", -1) == 0);
	CHECK(process(&host, "fPA7PA7", -1) == 0); /* 32767 in each axis. */
	CHECK(expect(env, 0, 36,3,32767,32767) == 0);
	CHECK(process(&host, "fPA8PA7", ST_RIP3_COMMAND_INVALID) == 0);
	CHECK(expect(env, 0, 36,3,32767,32767) == 0);
	CHECK(query(env, "$WORLD$", "32767:32767") == 0);
	CHECK(process(&host, "n2", -1) == 0);
	static const char *const invalid[] = {"J","J?","J100","n","n?000","n20000",
	    "f","fZK","fZKQO1","fZZ00","f0000","fZK?0"};
	for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		CHECK(process(&host, "fZKQO", -1) == 0);
		CHECK(process(&host, invalid[i], ST_RIP3_COMMAND_INVALID) == 0);
		CHECK(expect(env, 0, 36,2,1280,960) == 0);
	}
	CHECK(process(&host, "J1S", -1) == 0);
	CHECK(process(&host, "f-1-1", ST_RIP3_COMMAND_INVALID) == 0); /* Not an UltraNum digit. */
	CHECK(process(&host, "fK0F0", -1) == 0);
	CHECK(process(&host, "1Qanything", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(process(&host, "J10", -1) == 0);
	CHECK(st_rip3_environments_get(env, 0, &value) == 0);
	CHECK(st_rip3_environments_define(env, 1, &value) == 0);
	CHECK(st_rip3_environments_select(env, 1) == 0);
	CHECK(st_rip3_environments_protect(env, 1, true) == 0);
	CHECK(process(&host, "J1S", ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	CHECK(process(&host, "n5", ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	CHECK(process(&host, "f0101", ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	CHECK(expect(env, 1, 36,2,1280,960) == 0);
	CHECK(st_rip3_environments_protect(env, 1, false) == 0);
	st_rip3_commands_process(&host, NULL, (const uint8_t *)"f0101", 5);
	CHECK(expect(env, 1, 36,2,1,1) == 0);
	struct observations observed = {0};
	const struct st_rip3_command_sink sink = {&observed,diagnostic};
	const uint8_t nul[] = {'f','0','1',0,'1'};
	st_rip3_commands_process(&host, &sink, nul, sizeof(nul));
	CHECK(observed.count == 1 && observed.last == ST_RIP3_COMMAND_INVALID);
	st_rip3_commands_process(&host, &sink, (const uint8_t *)"J10", SIZE_MAX);
	CHECK(observed.count == 2 && observed.last == ST_RIP3_COMMAND_INVALID);
	CHECK(expect(env, 1, 36,2,1,1) == 0);
	CHECK(process(&host, "J1S", -1) == 0);
	CHECK(query(env, "$BASEMATH$", "64") == 0);
	CHECK(st_rip3_environments_get(env, 1, &value) == 0);
	CHECK(value.graphics_style == 7 && value.button_style == 6 && value.port == 5
	    && value.text_window == 4 && value.palette == 3 && value.rgb_bits == 8
	    && value.mouse_pointer == 2 && value.baud_rate == 9600);
	st_rip3_environments_destroy(env);
	return 0;
}

static int
pcm(audio_engine_t *engine, size_t sounding)
{
	int16_t frames[6 * 2];
	CHECK(audio_engine_pull(engine, frames, 6) == 0);
	for (size_t i = 0; i < 6; i++) {
		CHECK(frames[2*i] == (i < sounding ? 998 : 0));
		CHECK(frames[2*i+1] == (i < sounding ? -999 : 0));
	}
	return 0;
}

int
main(int argc, char **argv)
{
	struct st_rip3_command_host host = {0};
	audio_engine_t *engine = NULL;
	struct st_rip3_audio *audio = NULL;
	struct st_rip3_resource_paths resources = {0};
	char *file = NULL, *bad = NULL;
	static const uint8_t wave[] = {
		'R','I','F','F',52,0,0,0,'W','A','V','E',
		'f','m','t',' ',16,0,0,0,1,0,2,0,0x44,0xac,0,0,
		0x10,0xb1,2,0,4,0,16,0,'d','a','t','a',16,0,0,0,
		0xe8,3,0x18,0xfc,0xe8,3,0x18,0xfc,0xe8,3,0x18,0xfc,0xe8,3,0x18,0xfc};
	CHECK(geometry(ST_RIP3_CANVAS_640_480, 640,480) == 0);
	CHECK(geometry(ST_RIP3_CANVAS_800_600, 800,600) == 0);
	CHECK(geometry(ST_RIP3_CANVAS_1024_768, 1024,768) == 0);
	CHECK(process(&host, "J10", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(process(&host, "1w0000tone.wav", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(process(NULL, "J10", ST_RIP3_COMMAND_INVALID) == 0);
	CHECK(argc == 2 && fs_create_directories(argv[1]) == 0);
	CHECK(path_join_alloc(&file, argv[1], "tone name.wav") == 0);
	CHECK(path_join_alloc(&bad, argv[1], "bad.wav") == 0);
	CHECK(fs_replace_file(file, wave, sizeof(wave)) == 0);
	CHECK(fs_replace_file(bad, "bad", 3) == 0);
	CHECK(audio_engine_create_pull(&engine) == 0);
	CHECK(st_rip3_audio_create(&audio, engine, 0,0) == 0);
	resources.host = argv[1]; host.audio = audio; host.resources = &resources;
	CHECK(process(&host, "1w0000", ST_RIP3_COMMAND_INVALID) == 0);
	CHECK(process(&host, "1w1234tone name.wav", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(process(&host, "1w0000bad.wav", ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	CHECK(process(&host, "1w0000missing.wav", -1) == 0);
	host.resources = NULL;
	CHECK(process(&host, "1w0000$OFF$", -1) == 0);
	CHECK(pcm(engine, 0) == 0);
	host.resources = &resources;
	if (st_audio_file_available()) {
		CHECK(process(&host, "1w0000tone name.wav", -1) == 0);
		CHECK(pcm(engine, 4) == 0);
		CHECK(process(&host, "1w0000tone name.wav", -1) == 0);
		CHECK(process(&host, "1w0010tone name.wav", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
		CHECK(process(&host, "1w0000missing.wav", -1) == 0);
		CHECK(process(&host, "1w0000bad.wav", ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
		CHECK(pcm(engine, 4) == 0);
		st_rip3_commands_process(&host, NULL, (const uint8_t *)"1w0000tone name.wav", 19);
		CHECK(pcm(engine, 4) == 0);
		CHECK(process(&host, "1w0000tone name.wav", -1) == 0);
		host.resources = NULL;
		CHECK(process(&host, "1w0000$OFF$", -1) == 0);
		CHECK(pcm(engine, 0) == 0);
		host.resources = &resources;
	}
	else
		CHECK(process(&host, "1w0000tone name.wav", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	st_rip3_audio_destroy(audio); audio_engine_destroy(engine);
	CHECK(fs_remove_file(file) == 0 && fs_remove_file(bad) == 0);
	CHECK(fs_remove_directory(argv[1]) == 0);
	free(file); free(bad);
	return 0;
}

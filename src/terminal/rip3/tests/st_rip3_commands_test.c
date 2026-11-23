/* Independent complete-command expectations and parser-owned recovery. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_commands.h"
#include "st_rip3_environments.h"
#include "st_rip3_host_variables.h"
#include "st_rip3_table_host.h"
#include "st_rip3_stream.h"
#include "st_rip3_number.h"
#include "st_rip3_audio.h"
#include "st_rip3_session.h"
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
	CHECK(process(&host, "fPA8PA7", -1) == 0);
	CHECK(expect(env, 0, 36,3,32768,32767) == 0);
	CHECK(query(env, "$WORLD$", "32768:32767") == 0);
	CHECK(process(&host, "n2", -1) == 0);
	static const char *const invalid[] = {"J","J?","J100","n","n?000","n20000",
	    "f","fZK","fZKQO1","fZK?0"};
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
world_wire(void)
{
	static const unsigned inputs[]={0,1,32767,32768,65535,65536,65537};
	struct st_rip3_command_host host={0};
	CHECK(st_rip3_environments_create(&host.environments,ST_RIP3_CANVAS_640_480)==0);
	for (unsigned radix=36;radix<=64;radix+=28) {
		CHECK(process(&host,radix==36 ? "J10" : "J1S",-1)==0 && process(&host,"n5",-1)==0);
		for (size_t x=0;x<7;x++) for (size_t y=0;y<7;y++) {
			struct st_rip3_environment initial;
			CHECK(st_rip3_environments_get(host.environments,0,&initial)==0);
			initial.world_width=640;initial.world_height=350;
			CHECK(st_rip3_environments_set(host.environments,0,&initial)==0);
			char wire[12]={'f'};wire[11]=0;
			CHECK(st_rip3_number_encode(wire+1,5,inputs[x],5,radix)==0
			    && st_rip3_number_encode(wire+6,5,inputs[y],5,radix)==0);
			bool rejected=inputs[x]>65535 || inputs[y]>65535;
			CHECK(process(&host,wire,rejected ? ST_RIP3_COMMAND_INVALID : -1)==0);
			CHECK(expect(host.environments,0,radix,5,rejected ? 640 : inputs[x],rejected ? 350 : inputs[y])==0);
		}
	}
	st_rip3_environments_destroy(host.environments);return 0;
}

static int
color_modes(void)
{
	struct st_rip3_command_host host={0},missing={0};
	CHECK(st_rip3_environments_create(&host.environments,ST_RIP3_CANVAS_640_480)==0);
	CHECK(process(&missing,"M08",ST_RIP3_COMMAND_UNSUPPORTED)==0);
	for (unsigned radix=36;radix<=64;radix+=28) {
		CHECK(process(&host,radix==36 ? "J10" : "J1S",-1)==0);
		for (unsigned bits=0;bits<radix;bits++) {
			char wire[4]={'M','1',0,0};
			CHECK(st_rip3_number_encode(wire+2,1,bits,1,radix)==0);
			CHECK(process(&host,"M08",-1)==0 && process(&host,wire,-1)==0);
			CHECK(query(host.environments,"$COLORMODE$",bits==8 ? "8" : "0")==0);
			CHECK(process(&host,"M18",-1)==0 && process(&host,wire,-1)==0);
			CHECK(query(host.environments,"$COLORMODE$","8")==0);
			wire[1]='0';CHECK(process(&host,wire,-1)==0);
			CHECK(query(host.environments,"$COLORMODE$","0")==0);
		}
	}
	const char *invalid[]={"M","M0","M080","M?8","M0?","M28"};
	for (size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
		CHECK(process(&host,"M18",-1)==0);
		CHECK(process(&host,invalid[i],ST_RIP3_COMMAND_INVALID)==0);
		CHECK(query(host.environments,"$COLORMODE$","8")==0);
	}
	struct st_rip3_environment value;
	CHECK(st_rip3_environments_get(host.environments,0,&value)==0
	    && st_rip3_environments_define(host.environments,1,&value)==0
	    && st_rip3_environments_select(host.environments,1)==0);
	CHECK(st_rip3_environments_protect(host.environments,1,true)==0);
	CHECK(process(&host,"M08",ST_RIP3_COMMAND_OPERATION_FAILED)==0);
	CHECK(process(&host,"M17",-1)==0 && query(host.environments,"$COLORMODE$","8")==0);
	CHECK(st_rip3_environments_protect(host.environments,1,false)==0);
	CHECK(process(&host,"M08",-1)==0 && query(host.environments,"$COLORMODE$","0")==0);
	CHECK(query(host.environments,"$COLORMODE(0)$","8")==0);
	st_rip3_environments_destroy(host.environments);return 0;
}

static int
baud(enum st_rip3_canvas canvas)
{
	struct st_rip3_command_host host = {0}, missing = {0};
	struct st_rip3_environment before, value, other;
	unsigned current;
	const char *invalid[] = {"3e","3e?","3e00000","3e00?0"};
	CHECK(st_rip3_environments_create(&host.environments, canvas) == 0);
	CHECK(process(&missing, "3e0000", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(query(host.environments, "$BAUDEMUL$", "0") == 0);
	CHECK(st_rip3_environments_get(host.environments, 0, &before) == 0);
	before.graphics_style = 7; before.button_style = 6; before.port = 5;
	before.text_window = 4; before.palette = 3; before.rgb_bits = 8;
	before.mouse_pointer = 2; before.baud_rate = 111; before.coordinate_size = 5;
	CHECK(st_rip3_environments_set(host.environments, 0, &before) == 0);
	CHECK(st_rip3_environments_define(host.environments, 1, &before) == 0);
	CHECK(st_rip3_environments_select(host.environments, 1) == 0);
	CHECK(process(&host, "3e01UN", -1) == 0); /* Preserved example means 2399, not a preset. */
	CHECK(query(host.environments, "$BAUDEMUL$", "2399") == 0);
	CHECK(process(&host, "3e01UO", -1) == 0);
	CHECK(query(host.environments, "$BAUDEMUL(CUR)$", "2400") == 0);
	CHECK(process(&host, "3eZZZZ", -1) == 0);
	CHECK(query(host.environments, "$BAUDEMUL$", "1679615") == 0);
	CHECK(process(&host, "3eA", -1) == 0);
	CHECK(query(host.environments, "$BAUDEMUL$", "10") == 0);
	for (size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); i++) {
		CHECK(process(&host, invalid[i], ST_RIP3_COMMAND_INVALID) == 0);
		CHECK(query(host.environments, "$BAUDEMUL$", "10") == 0);
	}
	CHECK(process(&host, "3e0000 comment", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(query(host.environments, "$BAUDEMUL$", "10") == 0);
	CHECK(process(&host, "3e----", -1) == 0);
	CHECK(query(host.environments, "$BAUDEMUL$", "0") == 0);
	CHECK(process(&host, "J1S", -1) == 0);
	CHECK(process(&host, "3e&&&&", -1) == 0);
	CHECK(query(host.environments, "$BAUDEMUL$", "16777215") == 0);
	CHECK(process(&host, "3e000a", -1) == 0);
	CHECK(query(host.environments, "$BAUDEMUL$", "36") == 0);
	CHECK(process(&host, "3e#", -1) == 0);
	CHECK(query(host.environments, "$BAUDEMUL$", "62") == 0);
	CHECK(process(&host, "3e-", ST_RIP3_COMMAND_INVALID) == 0);
	CHECK(query(host.environments, "$BAUDEMUL$", "62") == 0);
	CHECK(st_rip3_environments_protect(host.environments, 1, true) == 0);
	CHECK(process(&host, "3e0", ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	CHECK(query(host.environments, "$BAUDEMUL$", "62") == 0);
	CHECK(st_rip3_environments_protect(host.environments, 1, false) == 0);
	CHECK(process(&host, "3e0", -1) == 0);
	CHECK(query(host.environments, "$BAUDEMUL$", "0") == 0);
	CHECK(query(host.environments, "$BAUDEMUL(0)$", "111") == 0);
	CHECK(st_rip3_environments_current(host.environments, &current) == 0 && current == 1);
	CHECK(st_rip3_environments_get(host.environments, 1, &value) == 0);
	CHECK(value.graphics_style == 7 && value.button_style == 6 && value.port == 5
	    && value.text_window == 4 && value.palette == 3 && value.rgb_bits == 8
	    && value.mouse_pointer == 2 && value.coordinate_size == 5 && value.base_math == 64
	    && value.world_width == before.world_width && value.world_height == before.world_height);
	CHECK(st_rip3_environments_get(host.environments, 0, &other) == 0 && other.baud_rate == 111
	    && other.base_math == 36 && other.coordinate_size == 5);
	st_rip3_environments_destroy(host.environments);
	return 0;
}

struct baud_stream {
	const struct st_rip3_command_host *host;
	struct observations diagnostics;
	uint64_t rates[6];
	size_t count;
	bool invalid;
};

static void
baud_command(void *context, const uint8_t *command, size_t length)
{
	struct baud_stream *seen = context;
	const struct st_rip3_command_sink sink = {&seen->diagnostics,diagnostic};
	struct st_rip3_environment value;
	st_rip3_commands_process(seen->host, &sink, command,length);
	if (seen->count >= 6 || st_rip3_environments_get(seen->host->environments, 0, &value) != 0)
		seen->invalid = true;
	else seen->rates[seen->count++] = value.baud_rate;
}

static int
baud_stream(void)
{
	static const uint64_t expected[] = {2400,2400,1679615,1679615,16777215,0};
	static const char bytes[] = "!|3e01UO|3e0?00|3eZZZZ\r\n!|J1S|3e&&&&|3e0\r";
	struct st_rip3_command_host host = {0};
	struct baud_stream seen = {.host=&host};
	const struct st_rip3_stream_sink sink = {.context=&seen,.command=baud_command};
	struct st_rip3_stream *stream = NULL;
	CHECK(st_rip3_environments_create(&host.environments, ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_stream_create(&stream, &sink) == 0);
	for (size_t i = 0; i < sizeof(bytes)-1; i++) st_rip3_stream_process(stream, (const uint8_t *)bytes+i, 1);
	CHECK(!seen.invalid && seen.count == 6 && seen.diagnostics.count == 1
	    && seen.diagnostics.last == ST_RIP3_COMMAND_INVALID);
	for (size_t i = 0; i < 6; i++) CHECK(seen.rates[i] == expected[i]);
	CHECK(query(host.environments, "$BAUDEMUL$", "0") == 0);
	st_rip3_stream_destroy(stream); st_rip3_environments_destroy(host.environments);
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

struct playback_input {
	const char *wire;
	struct answer answer;
	unsigned errors;
};

static uint64_t
playback_now(void *context)
{
	(void)context;
	return 0;
}

static bool
playback_next(void *context, uint8_t *byte)
{
	struct playback_input *input=context;
	if (*input->wire=='\0') return false;
	*byte=(uint8_t)*input->wire++;
	return true;
}

static void
playback_error(void *context, enum st_rip3_command_diagnostic kind)
{
	struct playback_input *input=context;
	(void)kind;
	input->errors++;
}

static void
playback_reply(void *context, const uint8_t *bytes, size_t length)
{
	struct playback_input *input=context;
	reference_reply(&input->answer,bytes,length);
}

static int
host_wave(struct st_rip3_audio *audio, const struct st_rip3_resource_paths *resources,
    audio_engine_t *engine)
{
	struct playback_input input={"!|1\0330000before$)TONE NAME$after^m\r",{.expected="beforeafter\r"},0};
	struct st_rip3_session *session=NULL;
	const struct st_rip3_session_config config={.canvas=ST_RIP3_CANVAS_640_480,
	    .encoding=ST_RIP3_TEXT_CP437,.source={&input,playback_now,playback_next},
	    .sink={.context=&input,.reply=playback_reply,.diagnostic=playback_error},
	    .audio=audio,.resources=resources};
	CHECK(st_rip3_session_create(&session,&config)==0);
	st_rip3_session_process(session,4096);
	CHECK(input.answer.replies==1 && !input.answer.mismatch);
	CHECK(input.errors==(st_audio_file_available() ? 0u : 1u));
	CHECK(pcm(engine,st_audio_file_available() ? 4 : 0)==0);
	input=(struct playback_input){"!|1\0330000$)bad.wav$$)missing$alive\r",{.expected="alive"},0};
	st_rip3_session_process(session,4096);
	CHECK(input.answer.replies==1 && !input.answer.mismatch && input.errors==1);
	CHECK(pcm(engine,0)==0);
	st_rip3_session_destroy(session);
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
	CHECK(color_modes() == 0);
	CHECK(world_wire() == 0);
	CHECK(baud(ST_RIP3_CANVAS_640_480) == 0);
	CHECK(baud(ST_RIP3_CANVAS_800_600) == 0);
	CHECK(baud(ST_RIP3_CANVAS_1024_768) == 0);
	CHECK(baud_stream() == 0);
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
	CHECK(host_wave(audio,&resources,engine)==0);
	st_rip3_audio_destroy(audio); audio_engine_destroy(engine);
	CHECK(fs_remove_file(file) == 0 && fs_remove_file(bad) == 0);
	CHECK(fs_remove_directory(argv[1]) == 0);
	free(file); free(bad);
	return 0;
}

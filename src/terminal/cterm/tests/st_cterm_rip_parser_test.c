/* RIP wire recognizer tests. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "cterm_rip_parser.h"
#include "io_error.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
		return 1; \
	} \
} while (0)

struct capture {
	uint8_t text[1024];
	size_t text_size;
	uint8_t control[64];
	size_t control_size;
	char args[128];
	int level;
	int sublevel;
	uint8_t code;
	unsigned commands;
	unsigned controls;
	unsigned text_calls;
	unsigned warnings;
	int command_error;
	bool bol;
	bool disabled;
	bool music;
};

static int
capture_text(void *context, const uint8_t *bytes, size_t size)
{
	struct capture *capture = context;

	if (size > sizeof(capture->text) - capture->text_size)
		return -1;
	memcpy(capture->text + capture->text_size, bytes, size);
	capture->text_size += size;
	capture->text_calls++;
	return 0;
}

static int
capture_command(void *context, int level, int sublevel, uint8_t code,
    const char *args)
{
	struct capture *capture = context;
	const size_t size = strlen(args);
	if (code == '?' && capture->command_error != 0) return capture->command_error;

	if (size >= sizeof(capture->args))
		return -1;
	memcpy(capture->args, args, size + 1);
	capture->level = level;
	capture->sublevel = sublevel;
	capture->code = code;
	capture->commands++;
	return 0;
}

static int
capture_control(void *context, const uint8_t *bytes, size_t size)
{
	struct capture *capture = context;

	if (size > sizeof(capture->control))
		return -1;
	memcpy(capture->control, bytes, size);
	capture->control_size = size;
	capture->controls++;
	return 0;
}

static bool
capture_bol(void *context)
{
	return ((struct capture *)context)->bol;
}

static bool
capture_disabled(void *context)
{
	return ((struct capture *)context)->disabled;
}

static bool
capture_music(void *context, uint8_t final)
{
	return ((struct capture *)context)->music && final == 'M';
}

static void
capture_warning(void *context, const char *message)
{
	struct capture *capture = context;
	if (message[0] != '\0') capture->warnings++;
}

static struct cterm_rip_parser_sink
sink_for(struct capture *capture)
{
	return (struct cterm_rip_parser_sink){
		.context = capture,
		.passthrough = capture_text,
		.command = capture_command,
		.control = capture_control,
		.at_carriage_return_column = capture_bol,
		.text_disabled = capture_disabled,
		.music_introducer = capture_music,
		.warning = capture_warning,
	};
}

static int
write_bytes(struct cterm_rip_parser *parser, struct capture *capture,
    const void *bytes, size_t size)
{
	struct cterm_rip_parser_sink sink = sink_for(capture);
	size_t consumed = 0;
	int status = cterm_rip_parser_feed(parser, &sink, bytes, size, &consumed);

	return status != 0 || consumed != size ? -1 : 0;
}

static int
test_split_and_ebc(void)
{
	struct cterm_rip_parser parser;
	struct capture capture = {0};
	const uint8_t first[] = "!|1P00\\|";
	const uint8_t second[] = "A\r\nX";

	cterm_rip_parser_init(&parser, true);
	CHECK(write_bytes(&parser, &capture, first, sizeof(first) - 1) == 0);
	CHECK(capture.commands == 0);
	CHECK(write_bytes(&parser, &capture, second, sizeof(second) - 1) == 0);
	CHECK(capture.commands == 1);
	CHECK(capture.level == 1 && capture.sublevel == 0);
	CHECK(capture.code == 'P');
	CHECK(strcmp(capture.args, "00|A") == 0);
	CHECK(capture.text_size == 1 && capture.text[0] == 'X');
	cterm_rip_parser_destroy(&parser);
	return 0;
}

static int
test_ansi_and_bol(void)
{
	struct cterm_rip_parser parser;
	struct capture capture = {0};
	const uint8_t stream[] = "A\x1b[5;1H!|c0F\r";

	cterm_rip_parser_init(&parser, true);
	capture.bol = true;
	CHECK(write_bytes(&parser, &capture, stream, sizeof(stream) - 1) == 0);
	CHECK(capture.commands == 1 && capture.code == 'c');
	CHECK(strcmp(capture.args, "0F") == 0);
	CHECK(capture.text_size == sizeof("A\x1b[5;1H") - 1);
	CHECK(memcmp(capture.text, "A\x1b[5;1H", capture.text_size) == 0);
	cterm_rip_parser_destroy(&parser);
	return 0;
}

static int
test_bogus_introducer_batch(void)
{
	struct cterm_rip_parser parser;
	struct capture capture = {0};
	const uint8_t first = 0x01;
	const uint8_t second = 0x03;

	cterm_rip_parser_init(&parser, true);
	CHECK(write_bytes(&parser, &capture, &first, 1) == 0);
	CHECK(capture.text_calls == 0);
	CHECK(write_bytes(&parser, &capture, &second, 1) == 0);
	CHECK(capture.text_size == 2 && capture.text[0] == first
	    && capture.text[1] == second && capture.text_calls == 1);
	cterm_rip_parser_destroy(&parser);
	return 0;
}

static int
test_controls_and_disable(void)
{
	struct cterm_rip_parser parser;
	struct capture capture = {0};
	const uint8_t query[] = "\x1b[0!";
	const uint8_t skypix[] = "\x1b[12;3!";
	const uint8_t disable[] = "\x1b[1!";
	const uint8_t after[] = "!|c0F\r";

	cterm_rip_parser_init(&parser, true);
	CHECK(write_bytes(&parser, &capture, query, sizeof(query) - 1) == 0);
	CHECK(capture.controls == 1 && capture.control_size == sizeof(query) - 1);
	CHECK(memcmp(capture.control, query, sizeof(query) - 1) == 0);
	CHECK(write_bytes(&parser, &capture, skypix, sizeof(skypix) - 1) == 0);
	CHECK(capture.controls == 1);
	CHECK(capture.text_size == sizeof(skypix) - 1);
	CHECK(memcmp(capture.text, skypix, sizeof(skypix) - 1) == 0);
	capture.text_size = 0;
	CHECK(write_bytes(&parser, &capture, disable, sizeof(disable) - 1) == 0);
	CHECK(!parser.enabled);
	CHECK(write_bytes(&parser, &capture, after, sizeof(after) - 1) == 0);
	CHECK(capture.commands == 0);
	CHECK(capture.text_size == sizeof(after) - 1);
	CHECK(memcmp(capture.text, after, sizeof(after) - 1) == 0);
	cterm_rip_parser_destroy(&parser);
	return 0;
}

static int
test_text_window_controls(void)
{
	struct cterm_rip_parser parser;
	struct capture capture = {0};
	const uint8_t stream[] = "A\v\x0e\x0f\x1b[M\x0e";

	cterm_rip_parser_init(&parser, true);
	capture.music = true;
	CHECK(write_bytes(&parser, &capture, stream, sizeof(stream) - 1) == 0);
	CHECK(capture.text_size == sizeof("A\x1b[B\x1b[M\x0e") - 1);
	CHECK(memcmp(capture.text, "A\x1b[B\x1b[M\x0e",
	    capture.text_size) == 0);
	capture.disabled = true;
	CHECK(write_bytes(&parser, &capture, "Z", 1) == 0);
	CHECK(capture.text_size == sizeof("A\x1b[B\x1b[M\x0e") - 1);
	cterm_rip_parser_destroy(&parser);
	return 0;
}

static bool
same_capture(const struct capture *left, const struct capture *right)
{
	return left->text_size == right->text_size
	    && memcmp(left->text, right->text, left->text_size) == 0
	    && left->control_size == right->control_size
	    && memcmp(left->control, right->control, left->control_size) == 0
	    && strcmp(left->args, right->args) == 0
	    && left->level == right->level
	    && left->sublevel == right->sublevel
	    && left->code == right->code
	    && left->commands == right->commands
	    && left->controls == right->controls;
}

static int
test_every_split(void)
{
	const uint8_t stream[] = "A\x1b[5;1H!|c0F|1P00\\|A\r\n"
	    "\x1b[0!\x1b[12;3!Z";
	struct cterm_rip_parser parser;
	struct capture whole = {0};
	const size_t size = sizeof(stream) - 1;

	whole.bol = true;
	cterm_rip_parser_init(&parser, true);
	CHECK(write_bytes(&parser, &whole, stream, size) == 0);
	cterm_rip_parser_destroy(&parser);
	for (size_t split = 0; split <= size; split++) {
		struct capture pieces = {0};

		pieces.bol = true;
		cterm_rip_parser_init(&parser, true);
		CHECK(write_bytes(&parser, &pieces, stream, split) == 0);
		CHECK(write_bytes(&parser, &pieces, stream + split,
		    size - split) == 0);
		CHECK(same_capture(&whole, &pieces));
		cterm_rip_parser_destroy(&parser);
	}
	return 0;
}

static int
test_deferred_transfer_recognition(void)
{
	struct cterm_rip_parser parser;
	struct capture capture = {0};
	const uint8_t transfer[] = "!|9\x1b" "0100FILE<>\r";

	cterm_rip_parser_init(&parser, true);
	for (size_t i = 0; i < sizeof(transfer) - 1; i++)
		CHECK(write_bytes(&parser, &capture, transfer + i, 1) == 0);
	CHECK(capture.commands == 1);
	CHECK(capture.level == 9 && capture.sublevel == 0);
	CHECK(capture.code == 0x1b);
	CHECK(strcmp(capture.args, "0100FILE<>") == 0);
	cterm_rip_parser_destroy(&parser);
	return 0;
}

static int
test_plain_batch(void)
{
	struct cterm_rip_parser parser;
	struct capture capture = {0};
	uint8_t bytes[900];

	memset(bytes, 'A', sizeof(bytes));
	cterm_rip_parser_init(&parser, true);
	CHECK(write_bytes(&parser, &capture, bytes, sizeof(bytes)) == 0);
	CHECK(capture.text_calls == 1);
	CHECK(capture.text_size == sizeof(bytes));
	CHECK(memcmp(capture.text, bytes, sizeof(bytes)) == 0);
	cterm_rip_parser_destroy(&parser);
	memset(&capture, 0, sizeof(capture));
	cterm_rip_parser_init(&parser, true);
	CHECK(write_bytes(&parser, &capture, "A\r\nB", 4) == 0);
	CHECK(capture.text_size == 4 && capture.text_calls == 1);
	CHECK(memcmp(capture.text, "A\r\nB", 4) == 0);
	cterm_rip_parser_destroy(&parser);
	return 0;
}

static int
test_escape_utf8_when_text_disabled(void)
{
	const struct {
		uint8_t bytes[6];
		size_t size;
	} cases[] = {
		{{0x1b, 0xc3, 0xa5, 'X'}, 4},
		{{0x1b, 0xe2, 0x82, 0xac, 'X'}, 5},
		{{0x1b, 0xf0, 0x9f, 0x98, 0x80, 'X'}, 6},
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		for (size_t split = 0; split <= cases[i].size; split++) {
			struct cterm_rip_parser parser;
			struct capture capture = {.disabled = true};

			cterm_rip_parser_init(&parser, true);
			parser.utf8_input = true;
			CHECK(write_bytes(&parser, &capture, cases[i].bytes,
			    split) == 0);
			CHECK(write_bytes(&parser, &capture,
			    cases[i].bytes + split,
			    cases[i].size - split) == 0);
			CHECK(capture.text_size == cases[i].size - 1);
			CHECK(memcmp(capture.text, cases[i].bytes,
			    capture.text_size) == 0);
			cterm_rip_parser_destroy(&parser);
		}
	}
	{
		struct cterm_rip_parser parser;
		struct capture capture = {.disabled = true};

		cterm_rip_parser_init(&parser, true);
		CHECK(write_bytes(&parser, &capture, cases[0].bytes,
		    cases[0].size) == 0);
		CHECK(capture.text_size == 2);
		CHECK(memcmp(capture.text, cases[0].bytes, 2) == 0);
		cterm_rip_parser_destroy(&parser);
	}
	return 0;
}

static int
test_lf_does_not_separate_commands(void)
{
	const uint8_t stream[] = "!|c0F\n!|1U0K0F3W1T0000<>\n!|#\r";
	const size_t size = sizeof(stream) - 1;

	for (size_t split = 0; split <= size; split++) {
		struct cterm_rip_parser parser;
		struct capture capture = {0};

		cterm_rip_parser_init(&parser, true);
		CHECK(write_bytes(&parser, &capture, stream, split) == 0);
		CHECK(write_bytes(&parser, &capture, stream + split,
		    size - split) == 0);
		CHECK(capture.commands == 3 && capture.code == '#');
		cterm_rip_parser_destroy(&parser);
	}
	{
		struct cterm_rip_parser parser;
		struct capture capture = {0};
		const uint8_t first[] = "!|c0F\n!|";
		const uint8_t second[] = "1U0K0F3W1T0000<>\n!|";

		cterm_rip_parser_init(&parser, true);
		CHECK(write_bytes(&parser, &capture, first,
		    sizeof(first) - 1) == 0);
		CHECK(capture.commands == 1 && capture.code == 'c');
		CHECK(strcmp(capture.args, "0F!") == 0);
		CHECK(write_bytes(&parser, &capture, second,
		    sizeof(second) - 1) == 0);
		CHECK(capture.commands == 2 && capture.code == 'U');
		CHECK(strcmp(capture.args, "0K0F3W1T0000<>!") == 0);
		cterm_rip_parser_destroy(&parser);
	}
	return 0;
}

static int
test_lf_inside_command(void)
{
	struct cterm_rip_parser parser;
	struct capture capture = {0};
	const uint8_t numeric[] = "!|c0\nF\r";
	const uint8_t text[] = "!|Tfoo\n!x\r";

	cterm_rip_parser_init(&parser, true);
	CHECK(write_bytes(&parser, &capture, numeric,
	    sizeof(numeric) - 1) == 0);
	CHECK(capture.commands == 1 && capture.code == 'c');
	CHECK(strcmp(capture.args, "0F") == 0);
	CHECK(write_bytes(&parser, &capture, text, sizeof(text) - 1) == 0);
	CHECK(capture.commands == 2 && capture.code == 'T');
	CHECK(strcmp(capture.args, "foo!x") == 0);
	cterm_rip_parser_destroy(&parser);
	return 0;
}

static int
test_continued_crlf_before_introducer(void)
{
	const uint8_t stream[] = "!|l02FL98FI99\\\r\n!|";
	const size_t size = sizeof(stream) - 1;

	for (size_t split = 0; split <= size; split++) {
		struct cterm_rip_parser parser;
		struct capture capture = {0};

		cterm_rip_parser_init(&parser, true);
		CHECK(write_bytes(&parser, &capture, stream, split) == 0);
		CHECK(write_bytes(&parser, &capture, stream + split,
		    size - split) == 0);
		CHECK(capture.commands == 1 && capture.code == 'l');
		CHECK(strcmp(capture.args, "02FL98FI99!") == 0);
		CHECK(write_bytes(&parser, &capture, "#\r", 2) == 0);
		CHECK(capture.commands == 2 && capture.code == '#');
		cterm_rip_parser_destroy(&parser);
	}
	return 0;
}

static int
test_bare_lf_after_rip_cr(void)
{
	const uint8_t stream[] = "!|#\r\n!|Tfoo\n!|";
	const size_t size = sizeof(stream) - 1;

	for (size_t split = 0; split <= size; split++) {
		struct cterm_rip_parser parser;
		struct capture capture = {0};

		cterm_rip_parser_init(&parser, true);
		CHECK(write_bytes(&parser, &capture, stream, split) == 0);
		CHECK(write_bytes(&parser, &capture, stream + split,
		    size - split) == 0);
		CHECK(capture.commands == 2 && capture.code == 'T');
		CHECK(strcmp(capture.args, "foo!") == 0);
		cterm_rip_parser_destroy(&parser);
	}
	return 0;
}

/* These expectations come from the RIPterm 1.54 disassembly notes in
 * rip_test/cmd_parse.md, not from comparing one parser feed to another.
 * Exercise every two-part split and a bytewise feed of each vector. */
static int
test_v154_dispatch_vectors(void)
{
	static const struct {
		const char *name;
		const char *wire;
		const char *text;
		const char *args;
		unsigned commands;
		uint8_t code;
		int level;
		int sublevel;
		bool flush;
	} cases[] = {
		{"escaped pipe", "!|Tleft\\|right\r\nA", "A", "left|right", 1, 'T', 0, 0, false},
		{"literal slash", "!|Tleft\\\\right\r\nA", "A", "left\\right", 1, 'T', 0, 0, false},
		{"odd continuation", "!|Tleft\\\r\nright\r\nA", "A", "leftright", 1, 'T', 0, 0, false},
		{"even slash CR", "!|Tleft\\\\\r\nA", "A", "left\\", 1, 'T', 0, 0, false},
		{"triple continuation", "!|Tleft\\\\\\\r\nright\r\nA", "A", "left\\right", 1, 'T', 0, 0, false},
		{"double bang literal", "!!|c0F\r\n", "!!|c0F\r\n", "", 0, 0, 0, 0, false},
		{"lone bang CR", "!\r\n", "!\r\n", "", 0, 0, 0, 0, false},
		{"unknown command EBC", "!|~ignored\r\nA", "A", "ignored", 1, '~', 0, 0, false},
		{"C0 argument data", "!|T\a\b\t\v\f\x0e\x0f\x1a\x1b\r\nA", "A",
		 "\a\b\t\v\f\x0e\x0f\x1a\x1b", 1, 'T', 0, 0, false},
		{"no-more LF data", "!|#\nTRAIL\r\nA", "A", "TRAIL", 1, '#', 0, 0, false},
		{"repeated no-more", "!|#|#|c0F\r\nA", "A", "0F", 3, 'c', 0, 0, false},
		{"no-more new introducer", "!|#!|c0F\r\nA", "A", "0F", 2, 'c', 0, 0, false},
		{"sublevel", "!|12qargs\r\nA", "A", "args", 1, 'q', 1, 2, false},
		{"unfinished command", "!|Ttail", "", "", 0, 0, 0, 0, false},
		{"flush final command", "!|Ttail", "", "tail", 1, 'T', 0, 0, true},
	};

	for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		size_t size = strlen(cases[index].wire);

		for (size_t split = 0; split <= size + 1; split++) {
			struct cterm_rip_parser parser;
			struct capture capture = {0};
			struct cterm_rip_parser_sink sink = sink_for(&capture);

			cterm_rip_parser_init(&parser, true);
			if (split == size + 1) {
				for (size_t i = 0; i < size; i++)
					CHECK(write_bytes(&parser, &capture,
					    cases[index].wire + i, 1) == 0);
			}
			else {
				CHECK(write_bytes(&parser, &capture, cases[index].wire, split) == 0);
				CHECK(write_bytes(&parser, &capture, cases[index].wire + split, size - split) == 0);
			}
			if (cases[index].flush)
				CHECK(cterm_rip_parser_flush(&parser, &sink) == 0);
			if (capture.commands != cases[index].commands
			    || capture.code != cases[index].code
			    || capture.level != cases[index].level
			    || capture.sublevel != cases[index].sublevel
			    || strcmp(capture.args, cases[index].args) != 0
			    || capture.text_size != strlen(cases[index].text)
			    || memcmp(capture.text, cases[index].text, capture.text_size) != 0
			    || capture.controls != 0) {
				fprintf(stderr, "RIP 1.54 vector '%s', split %zu: commands=%u code=%u text=%zu args='%s'\n",
				    cases[index].name, split, capture.commands, capture.code,
				    capture.text_size, capture.args);
				cterm_rip_parser_destroy(&parser);
				return 1;
			}
			cterm_rip_parser_destroy(&parser);
		}
	}
	return 0;
}

static int
test_rejected_command(void)
{
	const uint8_t wire[] = "!|?bad|c02\r\n\x1b[31mOK";
	for (size_t split = 0; split <= sizeof(wire); ++split) {
		struct cterm_rip_parser parser;
		struct capture capture = {.command_error = IO_ERROR_NOT_SUPPORTED};
		cterm_rip_parser_init(&parser, true);
		size_t size = sizeof(wire)-1;
		if (split == sizeof(wire)) {
			for (size_t i = 0; i < size; ++i)
				CHECK(write_bytes(&parser,&capture,wire+i,1) == 0);
		} else {
			CHECK(write_bytes(&parser,&capture,wire,split) == 0);
			CHECK(write_bytes(&parser,&capture,wire+split,size-split) == 0);
		}
		CHECK(capture.warnings == 1 && capture.commands == 1);
		CHECK(capture.code == 'c' && strcmp(capture.args,"02") == 0);
		CHECK(capture.text_size == sizeof("\x1b[31mOK")-1);
		CHECK(memcmp(capture.text,"\x1b[31mOK",capture.text_size) == 0);
		cterm_rip_parser_destroy(&parser);
	}
	return 0;
}

int
st_cterm_rip_parser_test_main(void)
{
	if (test_rejected_command() != 0 || test_v154_dispatch_vectors() != 0
	    || test_split_and_ebc() != 0 || test_ansi_and_bol() != 0
	    || test_bogus_introducer_batch() != 0
	    || test_controls_and_disable() != 0
	    || test_text_window_controls() != 0 || test_every_split() != 0
	    || test_deferred_transfer_recognition() != 0
	    || test_plain_batch() != 0
	    || test_escape_utf8_when_text_disabled() != 0
	    || test_lf_does_not_separate_commands() != 0
	    || test_lf_inside_command() != 0
	    || test_continued_crlf_before_introducer() != 0
	    || test_bare_lf_after_rip_cr() != 0)
		return 1;
	return 0;
}

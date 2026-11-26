/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "io_error.h"
#include "st_ooii.h"

#define CHECK(e)                                                                \
	do {                                                                    \
		if (!(e)) {                                                     \
			fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); \
			return 1;                                               \
		}                                                               \
	} while (0)

static const char expected_init1[] =
    "\xdb\b \xdb\b \xdb\b[\xdb\b[\xdb\b \xdb\bM\xdb\ba\xdb\bi\xdb\bn\xdb\bt\xdb\be\xdb\bn\xdb\ba\xdb\bn\xdb\bc\xdb\be\xdb\b \xdb\bC\xdb\bo\xdb\bm\xdb\bp\xdb\bl\xdb\be\xdb\bt\xdb\be\xdb\b \xdb\b]\xdb\b]\xdb\b \b\r\n\r\n\r\n\x1b[0;0;36mDo you have the Overkill Ansiterm installed? (y/N)  \xe9 ";
static const char expected_init2[] =
    "\xdb\b \xdb\b \xdb\b[\xdb\b[\xdb\b \xdb\bM\xdb\ba\xdb\bi\xdb\bn\xdb\bt\xdb\be\xdb\bn\xdb\ba\xdb\bn\xdb\bc\xdb\be\xdb\b \xdb\bC\xdb\bo\xdb\bm\xdb\bp\xdb\bl\xdb\be\xdb\bt\xdb\be\xdb\b \xdb\b]\xdb\b]\xdb\b \b\r\n\r\n\x1b[0m\x1b[2J\r\n\r\n\x1b[0;1;30mHX Force retinal scan in progress ... \x1b[0;0;30m";

struct recorder {
	st_ooii_t *change_mode;
	char       pass[16384], render[131072], reply[32];
	size_t     passed, rendered, replied, sounds, samples;
	int        error;
};

/* Legacy scenes in this fixture use CP437's AB for U+00BD. */
static int
feed(st_ooii_t *ooii, struct recorder *r, const char *bytes, size_t size)
{
	for (size_t i = 0; i < size; ++i) {
		uint8_t byte = (uint8_t)bytes[i];
		bool    consumed;
		int     status = st_ooii_character(ooii, byte == 0xab ? 0x00bd : byte, &consumed);
		if (status != 0)
			return status;
		if (!consumed && r != NULL) {
			if (r->passed + 1 >= sizeof(r->pass))
				return IO_ERROR_OUT_OF_RANGE;
			r->pass[r->passed++] = bytes[i];
			r->pass[r->passed]   = 0;
		}
	}
	return 0;
}

static int
render(void *context, const uint8_t *bytes, size_t size)
{
	struct recorder *r = context;
	if (r->change_mode != NULL) {
		st_ooii_set_mode(r->change_mode, 0);
		r->change_mode = NULL;
	}
	if (size >= sizeof(r->render) - r->rendered)
		return IO_ERROR_OUT_OF_RANGE;
	memcpy(r->render + r->rendered, bytes, size);
	r->rendered += size;
	r->render[r->rendered] = 0;
	return r->error;
}

static int
reply(void *context, const uint8_t *bytes, size_t size)
{
	struct recorder *r = context;
	if (size >= sizeof(r->reply) - r->replied)
		return IO_ERROR_OUT_OF_RANGE;
	memcpy(r->reply + r->replied, bytes, size);
	r->replied += size;
	r->reply[r->replied] = 0;
	return r->error;
}

static void
sound(void *context, const uint8_t *samples, size_t size)
{
	struct recorder *r = context;
	if (samples != NULL && size != 0) {
		++r->sounds;
		r->samples += size;
	}
}

static int
discard_render(void *context, const uint8_t *bytes, size_t size)
{
	(void)context;
	(void)bytes;
	(void)size;
	return 0;
}

static int
command(st_ooii_t *ooii, const char *text)
{
	int status = feed(ooii, NULL, "\xab", 1);
	if (status == 0)
		status = feed(ooii, NULL, text, strlen(text));
	if (status == 0)
		status = feed(ooii, NULL, "|", 1);
	return status;
}

int
main(void)
{
	struct recorder             r    = {0};
	st_ooii_t                  *ooii = NULL, *other = NULL;
	const struct st_ooii_config config = {
	    .render  = render,
	    .reply   = reply,
	    .sound   = sound,
	    .context = &r,
	};
	CHECK(st_ooii_create(&ooii, &config) == 0);
	CHECK(st_ooii_create(&other, &config) == 0);
	CHECK(st_ooii_mode(ooii) == 0);
	const char *banners[] = {expected_init1, expected_init2};
	for (size_t mode = 0; mode < 2; ++mode) {
		size_t size = strlen(banners[mode]);
		st_ooii_set_mode(ooii, 0);
		for (size_t i = 0; i < size; ++i)
			CHECK(st_ooii_detect(ooii, (uint8_t)banners[mode][i]) == (i + 1 == size));
		CHECK(st_ooii_mode(ooii) == 0);
		st_ooii_finish_banner(ooii);
		CHECK(st_ooii_mode(ooii) == (int)mode + 1);
	}
	st_ooii_set_mode(ooii, 0);
	r = (struct recorder) {0};
	CHECK(feed(ooii, &r, "ordinary terminal output", 24) == 0);
	CHECK(r.passed == 24);
	r = (struct recorder) {0};
	CHECK(feed(ooii, &r, "plain\xab?|", 8) == 0);
	CHECK(r.passed == 8 && r.rendered == 0 && r.replied == 0);
	st_ooii_set_mode(ooii, 99);
	CHECK(st_ooii_mode(ooii) == 3 && st_ooii_mode(other) == 0);
	st_ooii_set_mode(ooii, -1);
	CHECK(st_ooii_mode(ooii) == 0);
	st_ooii_set_mode(ooii, 1);
	r = (struct recorder) {0};
	CHECK(feed(ooii, &r, "ordinary terminal output", 24) == 0);
	CHECK(r.passed == 24);
	CHECK(command(ooii, "?_2") == 0);
	CHECK(st_ooii_mode(ooii) == 3
	      && memcmp(r.reply,
		     "\xaf"
		     "2|",
		     3)
	             == 0);
	r = (struct recorder) {0};
	CHECK(command(ooii, "?") == 0);
	CHECK(st_ooii_mode(ooii) == 2
	      && memcmp(r.reply,
		     "\xaf"
		     "1|",
		     3)
	             == 0);
	for (int mode = 1; mode <= ST_OOII_MAX_MODE; ++mode) {
		st_ooii_set_mode(ooii, mode);
		for (const char *p = "ABCDEFGHIJK0"; *p; ++p) {
			char picture[] = {*p, 0};
			r              = (struct recorder) {0};
			CHECK(command(ooii, picture) == 0 && r.rendered > 11);
			CHECK(r.render[r.rendered - 1] == '\r');
		}
	}
	r = (struct recorder) {.change_mode = ooii};
	CHECK(command(ooii, "AB") == 0 && st_ooii_mode(ooii) == 0);
	st_ooii_set_mode(ooii, 1);
	r = (struct recorder) {0};
	CHECK(command(ooii, "a") == 0 && strstr(r.render, "Supply Room") != NULL);
	r = (struct recorder) {0};
	CHECK(command(ooii, "1") == 0 && strstr(r.render, "barstool") != NULL);
	r = (struct recorder) {0};
	CHECK(command(ooii, "$3") == 0 && r.sounds == 2 && r.samples == 177213);
	r = (struct recorder) {0};
	CHECK(feed(ooii, &r,
		  "\xab"
		  "a",
		  2)
	          == 0
	      && r.rendered == 0);
	CHECK(feed(other, &r, "|", 1) == 0 && r.passed == 1);
	CHECK(feed(ooii, &r, "|", 1) == 0 && strstr(r.render, "Supply Room") != NULL);
	r = (struct recorder) {0};
	CHECK(command(ooii, "\\") == 0 && st_ooii_mode(ooii) == 0);
	CHECK(feed(ooii, &r, "after", 5) == 0 && strcmp(r.pass, "after") == 0);
	st_ooii_set_mode(ooii, 1);
	CHECK(feed(ooii, &r, "\xab", 1) == 0);
	for (int i = 0; i < 1024; ++i)
		CHECK(feed(ooii, &r, "a", 1) == 0);
	r = (struct recorder) {0};
	CHECK(feed(ooii, &r, "|end", 4) == 0 && r.rendered == 0 && strcmp(r.pass, "end") == 0);
	/* Every command handler must tolerate empty fields and truncated input. */
	for (unsigned byte = 1; byte < 127; ++byte) {
		char text[] = {(char)byte, 0};
		st_ooii_set_mode(ooii, 1);
		r = (struct recorder) {0};
		CHECK(command(ooii, text) == 0);
	}

	struct st_ooii_config fuzz_config = config;
	fuzz_config.render                = discard_render;
	st_ooii_t *fuzz                   = NULL;
	CHECK(st_ooii_create(&fuzz, &fuzz_config) == 0);
	uint32_t random = 1234567;
	for (unsigned iteration = 0; iteration < 2000; ++iteration) {
		char text[96];
		for (size_t i = 0; i < sizeof(text) - 1; ++i) {
			random  = random * UINT32_C(1664525) + UINT32_C(1013904223);
			text[i] = (char)(32 + (random >> 16) % 95);
			if (text[i] == '|')
				text[i] = '_';
		}
		text[sizeof(text) - 1] = 0;
		r                      = (struct recorder) {0};
		st_ooii_set_mode(fuzz, 1);
		CHECK(command(fuzz, text) == 0);
	}
	st_ooii_destroy(fuzz);
	r = (struct recorder) {.error = IO_ERROR_IO};
	st_ooii_set_mode(ooii, 1);
	CHECK(command(ooii, "a") == IO_ERROR_IO);
	st_ooii_destroy(other);
	st_ooii_destroy(ooii);
	return 0;
}

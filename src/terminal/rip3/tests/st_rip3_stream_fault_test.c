/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool fail_calloc, fail_realloc;

static void *
test_calloc(size_t count, size_t size)
{
	if (fail_calloc) {
		fail_calloc = false;
		return NULL;
	}
	return calloc(count, size);
}

static void *
test_realloc(void *old, size_t size)
{
	if (fail_realloc) {
		fail_realloc = false;
		return NULL;
	}
	return realloc(old, size);
}

/* Test-local fault injection, with the production implementation unchanged. */
#define calloc test_calloc
#define realloc test_realloc
#include "../st_rip3_stream.c"
#undef calloc
#undef realloc

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
		return 1; \
	} \
} while (0)

struct observation { unsigned commands, errors; bool mismatch; };

static void
command(void *context, const uint8_t *bytes, size_t length)
{
	struct observation *seen = context;
	seen->commands++;
	if (length != 3 || memcmp(bytes, "J10", 3) != 0)
		seen->mismatch = true;
}

static void
diagnostic(void *context, enum st_rip3_stream_diagnostic kind)
{
	struct observation *seen = context;
	seen->errors++;
	if (kind != ST_RIP3_STREAM_RESOURCE_FAILURE)
		seen->mismatch = true;
}

int
main(void)
{
	struct st_rip3_stream *stream = NULL;
	struct observation seen = {0};
	const struct st_rip3_stream_sink sink = {.context=&seen,.command=command,.diagnostic=diagnostic};
	uint8_t block[256];

	fail_calloc = true;
	CHECK(st_rip3_stream_create(&stream, &sink) == IO_ERROR_OUT_OF_MEMORY && stream == NULL);
	CHECK(st_rip3_stream_create(&stream, &sink) == 0);
	fail_realloc = true;
	st_rip3_stream_process(stream, (const uint8_t *)"!|x", 3);
	/* One error for the whole discarded command. Neither quoted pipes nor
	 * continued line endings may resume dispatch inside discarded bytes. */
	static const uint8_t recover[] = "\\|bad\\\r\n\fmore|J10\n";
	st_rip3_stream_process(stream, recover, sizeof(recover)-1);
	CHECK(seen.errors == 1 && seen.commands == 1 && !seen.mismatch);
	st_rip3_stream_reset(stream);
	memset(block, 'x', sizeof(block));
	st_rip3_stream_process(stream, (const uint8_t *)"!|", 2);
	st_rip3_stream_process(stream, block, sizeof(block));
	fail_realloc = true;
	st_rip3_stream_process(stream, (const uint8_t *)"x\\|bad\r!|J10\r", sizeof("x\\|bad\r!|J10\r")-1);
	CHECK(seen.errors == 2 && seen.commands == 2 && !seen.mismatch);
	/* Reset while discarding releases the error state, retains usable space. */
	st_rip3_stream_process(stream, (const uint8_t *)"!|", 2);
	st_rip3_stream_process(stream, block, sizeof(block));
	fail_realloc = true;
	st_rip3_stream_process(stream, (const uint8_t *)"x", 1);
	st_rip3_stream_reset(stream);
	st_rip3_stream_process(stream, (const uint8_t *)"!|J10\r", 6);
	CHECK(seen.errors == 3 && seen.commands == 3 && !seen.mismatch);
	/* A comment's pipe remains a boundary even when preceded by a slash,
	 * including after a failed buffer growth. */
	st_rip3_stream_process(stream, (const uint8_t *)"!|", 2);
	block[0] = '!';
	st_rip3_stream_process(stream, block, sizeof(block));
	fail_realloc = true;
	st_rip3_stream_process(stream, (const uint8_t *)"x\\|J10\r", sizeof("x\\|J10\r")-1);
	CHECK(seen.errors == 4 && seen.commands == 4 && !seen.mismatch);
	st_rip3_stream_destroy(stream);
	return 0;
}

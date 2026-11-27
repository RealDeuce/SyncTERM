/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "st_ddofx.h"

#define CHECK(e)                                                   \
	do {                                                       \
		if (!(e)) {                                        \
			fprintf(stderr, "%d: %s\n", __LINE__, #e); \
			return EXIT_FAILURE;                       \
		}                                                  \
	} while (0)
#define DDO(body) "\x1d{" body "}"

struct output {
	char   text[256];
	size_t length;
};

static int
text(void *context, const uint8_t *s, size_t size, size_t *used)
{
	struct output *out = context;
	if (size > sizeof(out->text) - 1 - out->length)
		return -1;
	memcpy(out->text + out->length, s, size);
	out->length += size;
	out->text[out->length] = 0;
	*used                  = size;
	return 0;
}

static bool
feed(struct st_ddofx *ddo, struct st_ddofx_sink *sink, const char *s, size_t size)
{
	size_t used = 0;
	return st_ddofx_feed(ddo, sink, (const uint8_t *)s, size, &used) == 0 && used == size;
}

int
main(void)
{
	struct st_ddofx      ddo  = {0};
	struct output        out  = {0};
	struct st_ddofx_sink sink = {.context = &out, .text = text};
	/* The macro ends with an incomplete local command. The next line gets
	 * a fresh framer and the queued online command keeps its own state. */
	static const char batch[] =
	    DDO("4DEFM,1,1,X\x1d(w0)Y\x1d(unfinished") DDO("4DEFM,1,2,Z") DDO("4RUNM,1") "a\x1d{4EXCM,1";
	for (size_t split = 0; split < sizeof(batch); ++split) {
		st_ddofx_reset(&ddo);
		out = (struct output) {0};
		CHECK(feed(&ddo, &sink, batch, split));
		CHECK(feed(&ddo, &sink, batch + split, sizeof(batch) - 1 - split));
		CHECK(feed(&ddo, &sink, ",2}b", 4));
		for (int i = 0; i < 10 && st_ddofx_busy(&ddo); ++i)
			CHECK(st_ddofx_poll(&ddo, &sink) == 0);
		CHECK(!st_ddofx_busy(&ddo) && strcmp(out.text, "XYZaZb") == 0);
	}
	static const char endless[] = DDO("~DEFVAR,1,2,0") DDO("~REPVAR,DEFVAR1,=,1,\x1d(w0)") "lost";
	CHECK(feed(&ddo, &sink, endless, sizeof(endless) - 1));
	CHECK(st_ddofx_busy(&ddo) && st_ddofx_poll(&ddo, &sink) == 0 && st_ddofx_busy(&ddo));
	st_ddofx_reset(&ddo);
	CHECK(!st_ddofx_busy(&ddo));
	CHECK(feed(&ddo, &sink, "!", 1) && strcmp(out.text, "XYZaZb!") == 0);
	static const char recursive[] = DDO("4DEFM,1,1,\x1d(4EXCM,1,1)") DDO("4EXCM,1,1") "Q";
	CHECK(feed(&ddo, &sink, recursive, sizeof(recursive) - 1));
	for (int i = 0; i < 10 && st_ddofx_busy(&ddo); ++i)
		CHECK(st_ddofx_poll(&ddo, &sink) == 0);
	CHECK(!st_ddofx_busy(&ddo) && strcmp(out.text, "XYZaZb!Q") == 0);
	st_ddofx_reset(&ddo);
	return EXIT_SUCCESS;
}

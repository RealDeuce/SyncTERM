/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "st_cterm.h"
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

static int32_t
memory_available(void)
{
	return 1200000;
}

static void
warning(void *context, const char *title, const char *message)
{
	struct output *out = context;
	if (!strcmp(title, "DDO F/X") && strstr(message, "200000 bytes"))
		out->text[out->length++] = '!';
}

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

static void
reply(void *context, const uint8_t *bytes, size_t size)
{
	size_t used;
	if (text(context, bytes, size, &used) != 0)
		abort();
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
	out                       = (struct output) {0};
	sink.available_memory     = memory_available;
	sink.alert                = warning;
	sink.alert_context        = &out;
	static const char guard[] = DDO("dVMEM,1200000") "A" DDO("4DEFM,1,1,B\x1d(dVMEM,1400000)C")
	    DDO("4DEFM,2,1,D\x1d(4EXCM,1,1)E") DDO("4EXCM,2,1") "F";
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config mode   = {.columns = 80, .rows = 25, .cell_width = 8, .cell_height = 16};
	struct st_cterm_config         config = {.codepage = ST_CODEPAGE_CP437, .quiet = true};
	CHECK(st_console_create(&config.console, &display, &mode) == 0);
	sink.text_config          = &config;
	static const char enter[] = DDO("LIGMODE") DDO("LIGMODE");
	for (int normal = 0; normal <= 1; ++normal) {
		out = (struct output) {0};
		if (normal)
			CHECK(feed(&ddo, &sink, enter, sizeof(enter) - 1) && ddo.graphics);
		CHECK(feed(&ddo, &sink, guard, sizeof(guard) - 1));
		for (int i = 0; i < 10 && st_ddofx_busy(&ddo); ++i)
			CHECK(st_ddofx_poll(&ddo, &sink) == 0);
		CHECK(!ddo.graphics && !st_ddofx_busy(&ddo) && !strcmp(out.text, "ADB!F"));
		struct st_console_config restored;
		CHECK(st_console_get_config(config.console, &restored) == 0 && restored.rows == mode.rows);
	}
	out                      = (struct output) {0};
	sink.reply               = reply;
	static const char ping[] = DDO("5PING,7,P^@");
	CHECK(feed(&ddo, &sink, ping, sizeof(ping) - 1) && !ddo.ping.interval_ms);
	CHECK(feed(&ddo, &sink, enter, sizeof(enter) - 1) && ddo.graphics);
	CHECK(feed(&ddo, &sink, ping, sizeof(ping) - 1) && ddo.ping.interval_ms == 420000);
	CHECK(st_ddofx_poll(&ddo, &sink) == 0 && out.length == 0);

	/* An overdue ping fires once after control of the wire returns. */
	ddo.ping.due = 0;
	st_ddofx_background_download(&ddo, true);
	CHECK(st_ddofx_poll(&ddo, &sink) == 0 && out.length == 0);
	st_ddofx_background_download(&ddo, false);
	CHECK(st_ddofx_wait_ms(&ddo) == 0 && st_ddofx_poll(&ddo, &sink) == 0);
	CHECK(out.length == 2 && !memcmp(out.text, "P\0", 2));
	CHECK(st_ddofx_poll(&ddo, &sink) == 0 && out.length == 2);

	static const char disable[] = DDO("5PING,0,");
	CHECK(feed(&ddo, &sink, disable, sizeof(disable) - 1) && !ddo.ping.interval_ms);
	CHECK(feed(&ddo, &sink, ping, sizeof(ping) - 1));
	static const char leave[] = DDO("2GEXIT") DDO("2RESET") DDO("5PING,0,");
	CHECK(feed(&ddo, &sink, leave, sizeof(leave) - 1) && !ddo.graphics);
	ddo.ping.due = 0;
	CHECK(st_ddofx_poll(&ddo, &sink) == 0 && out.length == 2);
	CHECK(feed(&ddo, &sink, enter, sizeof(enter) - 1) && st_ddofx_poll(&ddo, &sink) == 0);
	CHECK(out.length == 4 && !memcmp(out.text, "P\0P\0", 4));
	static const char reset[] = DDO("2RESET");
	CHECK(feed(&ddo, &sink, reset, sizeof(reset) - 1) && !ddo.ping.interval_ms);
	st_ddofx_reset(&ddo);
	st_console_destroy(config.console);
	return EXIT_SUCCESS;
}

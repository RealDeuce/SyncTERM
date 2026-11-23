/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_host_variables.h"
#include "st_rip3_table_host.h"
#include "st_rip3_palettes.h"
#include "st_rip3_backup.h"
#include "st_rip3_geometry.h"
#include "st_rip3_features.h"
#include "io_error.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
		return 1; \
	} \
} while (0)

struct observations {
	char reply[3074];
	size_t length, replies, diagnostics;
	enum st_rip3_host_diagnostic last;
	bool overflow;
};

static void
reply(void *context, const uint8_t *bytes, size_t length)
{
	struct observations *observed = context;

	observed->replies++;
	if (length >= sizeof(observed->reply)) {
		observed->overflow = true;
		return;
	}
	memcpy(observed->reply, bytes, length);
	observed->reply[length] = 0;
	observed->length = length;
}

static void
diagnostic(void *context, enum st_rip3_host_diagnostic kind)
{
	struct observations *observed = context;
	observed->diagnostics++;
	observed->last = kind;
}

/* Assignment without a cast verifies the public parser boundary returns void;
 * no syntax/command result can enter a caller's session-error return path. */
static void (*const process)(const struct st_rip3_host_variables *,
    const struct st_rip3_host_sink *, const uint8_t *, size_t)
    = st_rip3_host_variables_process;

static int
run(const struct st_rip3_host_variables *host, const struct st_rip3_host_sink *sink,
    const char *text, int expected_diagnostic, const char *expected_reply)
{
	struct observations *observed = sink->context;
	size_t replies = observed->replies, diagnostics = observed->diagnostics;

	process(host, sink, (const uint8_t *)text, strlen(text));
	CHECK(!observed->overflow);
	CHECK(observed->diagnostics == diagnostics + (expected_diagnostic >= 0 ? 1u : 0u));
	if (expected_diagnostic >= 0)
		CHECK((int)observed->last == expected_diagnostic);
	CHECK(observed->replies == replies + (expected_reply != NULL && expected_reply[0] != 0 ? 1u : 0u));
	if (expected_reply != NULL && expected_reply[0] != 0)
		CHECK(strcmp(observed->reply, expected_reply) == 0);
	return 0;
}

static bool fail_clone, fail_restore;

static int
clone_palette(const void *live, void **result)
{
	struct st_rip3_palettes *saved = NULL;
	int status;

	if (fail_clone)
		return IO_ERROR_OUT_OF_MEMORY;
	status = st_rip3_palettes_clone(&saved, live);
	if (status == 0)
		*result = saved;
	return status;
}

static int
restore_palette(void *live, const void *saved)
{
	return fail_restore ? IO_ERROR_IO : st_rip3_palettes_restore(live, saved);
}

static void
destroy_palette(void *saved)
{
	st_rip3_palettes_destroy(saved);
}

int
main(void)
{
	struct st_rip3_host_variables host = {0};
	struct st_rip3_table_host tables = {0};
	struct st_rip3_features features = {0};
	struct st_rip3_frame frame;
	struct st_rip3_palettes *palettes = NULL;
	struct st_rip3_backup *backup = NULL;
	struct st_rip3_backup_info info;
	struct observations observed = {0};
	struct st_rip3_host_sink sink = {&observed,reply,diagnostic};
	const struct st_rip3_backup_ops ops = {clone_palette,restore_palette,destroy_palette};
	const char *const bad[] = {
		"", "$", "not a variable", "$COPY(PAL,TBL,S0$", "$CUR(PAL,99)$",
		"$COPY(PAL,TBL,S0,BAD)$", "$PROT(PAL,4,S0,STACK)$", "$IFS$",
		"$PALENTRY(CUR,256)$", "$PALENTRY(CUR,ALL,255)$",
	};
	const char command[] = "$CUR(PAL,7)$";
	char excessive[160];
	size_t length;
	uint32_t colors[256];
	unsigned current;

	CHECK(st_rip3_frame_init(&frame, ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_palettes_create(&palettes) == 0);
	CHECK(st_rip3_backup_create(&backup, &ops) == 0);
	tables.tables[ST_RIP3_TABLE_PAL] = (struct st_rip3_table_binding){palettes,backup};
	host.tables = &tables; host.features = &features; host.frame = &frame;
	CHECK(run(&host, &sink, "$CUR(PAL,4)$", -1, "") == 0);
	for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
		CHECK(run(&host, &sink, bad[i], ST_RIP3_HOST_INVALID_REFERENCE, NULL) == 0);
		CHECK(run(&host, &sink, "$CUR(PAL)$", -1, "4") == 0);
		CHECK(st_rip3_backup_info(backup, &info) == 0 && !info.slot_used[0]);
	}
	for (size_t i = 0; i < strlen(command); i++) {
		size_t previous = observed.diagnostics;
		process(&host, &sink, (const uint8_t *)command, i);
		CHECK(observed.diagnostics == previous + 1 && observed.last == ST_RIP3_HOST_INVALID_REFERENCE);
		CHECK(run(&host, &sink, "$CUR(PAL)$", -1, "4") == 0);
	}
	memcpy(excessive, "$IFS(", 5); length = 5;
	for (unsigned i = 0; i < 41; i++) {
		if (i != 0) excessive[length++] = ',';
		excessive[length++] = 'X';
	}
	memcpy(excessive + length, ")$", 3);
	CHECK(run(&host, &sink, excessive, ST_RIP3_HOST_INVALID_REFERENCE, NULL) == 0);
	CHECK(run(&host, &sink, "$RESX$", -1, "640") == 0);
	CHECK(run(&host, &sink, "$FIRST_NAME$", ST_RIP3_HOST_UNKNOWN_REFERENCE, NULL) == 0);
	CHECK(run(&host, &sink, "$SHIFT$", ST_RIP3_HOST_UNSUPPORTED_REFERENCE, NULL) == 0);
	CHECK(run(&host, &sink, "$APP0$", ST_RIP3_HOST_UNSUPPORTED_REFERENCE, NULL) == 0);
	CHECK(run(&host, &sink, "$NULL$", -1, "") == 0);
	CHECK(run(&host, &sink, "$RESY$", -1, "480") == 0);
	CHECK(run(&host, &sink, "$RCP$", ST_RIP3_HOST_OPERATION_FAILED, NULL) == 0);
	CHECK(run(&host, &sink, "$CUR(PAL)$", -1, "4") == 0);
	fail_clone = true;
	CHECK(run(&host, &sink, "$SCP$", ST_RIP3_HOST_RESOURCE_FAILURE, NULL) == 0);
	CHECK(st_rip3_backup_info(backup, &info) == 0 && !info.base);
	CHECK(run(&host, &sink, "$RESX$", -1, "640") == 0);
	fail_clone = false;
	CHECK(run(&host, &sink, "$SCP$", -1, "") == 0);
	CHECK(run(&host, &sink, "$CUR(PAL,5)$", -1, "") == 0);
	fail_restore = true;
	CHECK(run(&host, &sink, "$RCP$", ST_RIP3_HOST_OPERATION_FAILED, NULL) == 0);
	CHECK(run(&host, &sink, "$CUR(PAL)$", -1, "5") == 0);
	fail_restore = false;
	CHECK(run(&host, &sink, "$RCP$", -1, "") == 0);
	CHECK(run(&host, &sink, "$CUR(PAL)$", -1, "4") == 0);
	/* A caller supplies no reply buffer: the parser owns capacity for the
	 * largest implemented reply, rather than turning short buffers into errors. */
	for (size_t i = 0; i < 256; i++) colors[i] = 0xffffff;
	CHECK(st_rip3_palettes_rgb(palettes, 0, 8, colors, 256) == 0);
	{
		size_t previous = observed.replies, diagnostics = observed.diagnostics;
		process(&host, &sink, (const uint8_t *)"$PALENTRY(CUR,ALL)$", 19);
		CHECK(!observed.overflow && observed.replies == previous + 1);
		CHECK(observed.diagnostics == diagnostics && observed.length == 2305);
		CHECK(strncmp(observed.reply, "6;63:63:63,", 11) == 0);
	}
	process(&host, NULL, (const uint8_t *)"bad", 3);
	process(&host, NULL, (const uint8_t *)"$CUR(PAL,6)$", 12);
	CHECK(st_rip3_palettes_current(palettes, &current) == 0 && current == 6);
	CHECK(run(&host, &sink, "$CUR(PAL)$", -1, "6") == 0);
	st_rip3_backup_destroy(backup);
	st_rip3_palettes_destroy(palettes);
	return 0;
}

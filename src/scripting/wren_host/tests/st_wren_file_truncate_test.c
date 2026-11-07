/* File resizing: real Wren calls, grants, buffering and native failure paths. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "wren_host_internal.h"
#include "filesystem.h"
#include "io_error.h"
#include "st_app_paths.h"
#include "string_compat.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); \
	exit(EXIT_FAILURE); } } while (false)

extern unsigned st_wren_test_truncate_positions, st_wren_test_truncate_seeks;
extern unsigned st_wren_test_truncate_calls, st_wren_test_truncate_closes;
extern uint64_t st_wren_test_truncate_length;
extern int st_wren_test_truncate_position_error, st_wren_test_truncate_seek_error;
extern int st_wren_test_truncate_error, st_wren_test_truncate_errno;
extern WrenVM *st_wren_test_truncate_vm;
void st_wren_test_truncate_grant(WrenVM *, const char *, enum st_wren_picker_disposition);
void st_wren_test_truncate_invalidate(WrenVM *);

static void
run(WrenVM *vm, const char *source)
{
	CHECK(wrenInterpret(vm, "file_truncate_test", source) == WREN_RESULT_SUCCESS);
}

static char *
read_source(void)
{
	FILE *stream = fopen(ST_WREN_TRUNCATE_TEST_SOURCE, "rb");
	CHECK(stream != NULL && fseek(stream, 0, SEEK_END) == 0);
	long size = ftell(stream);
	CHECK(size > 0 && size < 65536 && fseek(stream, 0, SEEK_SET) == 0);
	char *source = malloc((size_t)size + 1);
	CHECK(source != NULL && fread(source, 1, (size_t)size, stream) == (size_t)size);
	source[size] = '\0';
	CHECK(fclose(stream) == 0);
	return source;
}

static void
grant(WrenVM *vm, enum st_wren_picker_disposition disposition)
{
	const char *path = ST_WREN_TRUNCATE_TEST_DIRECTORY "/consent.bin";
	(void)fs_remove_file(path);
	if (disposition != ST_WREN_PICKER_DISPOSITION_CREATE) {
		FILE *stream = NULL;
		CHECK(fs_open(&stream, path, "wb") == 0);
		CHECK(fwrite("abcdef", 1, 6, stream) == 6 && fclose(stream) == 0);
	}
	wrenEnsureSlots(vm, 3);
	st_wren_test_truncate_grant(vm, path, disposition);
	wrenGetVariable(vm, "file_truncate_test", "TruncateChecks", 0);
	WrenHandle *setter = wrenMakeCallHandle(vm, "granted=(_)");
	CHECK(wrenCall(vm, setter) == WREN_RESULT_SUCCESS);
	wrenReleaseHandle(vm, setter);
}

static void
denied(WrenVM *vm)
{
	unsigned positions = st_wren_test_truncate_positions;
	unsigned seeks = st_wren_test_truncate_seeks;
	unsigned calls = st_wren_test_truncate_calls;
	run(vm, "TruncateChecks.denied()\n");
	CHECK(st_wren_test_truncate_positions == positions
	    && st_wren_test_truncate_seeks == seeks && st_wren_test_truncate_calls == calls);
}

static void
boundaries(WrenVM *vm)
{
	unsigned positions = st_wren_test_truncate_positions;
	unsigned seeks = st_wren_test_truncate_seeks;
	unsigned calls = st_wren_test_truncate_calls;
	run(vm, "TruncateChecks.invalid()\n");
	CHECK(st_wren_test_truncate_positions == positions
	    && st_wren_test_truncate_seeks == seeks && st_wren_test_truncate_calls == calls);
	run(vm, "TruncateChecks.intact()\n");

	char probe[160];
	for (unsigned failure = 0; failure < 2; ++failure) {
		positions = st_wren_test_truncate_positions;
		seeks = st_wren_test_truncate_seeks;
		calls = st_wren_test_truncate_calls;
		st_wren_test_truncate_position_error = failure == 0 ? IO_ERROR_IO : 0;
		st_wren_test_truncate_seek_error = failure == 1 ? IO_ERROR_NO_SPACE : 0;
		(void)snprintf(probe, sizeof(probe), "TruncateChecks.fail(2, %d, %d)\n",
		    failure == 0 ? IO_ERROR_IO : IO_ERROR_NO_SPACE, failure == 0 ? EIO : ENOSPC);
		run(vm, probe);
		CHECK(st_wren_test_truncate_positions == positions + 1
		    && st_wren_test_truncate_seeks == seeks + failure
		    && st_wren_test_truncate_calls == calls);
		st_wren_test_truncate_position_error = st_wren_test_truncate_seek_error = 0;
		run(vm, "TruncateChecks.intact()\n");
	}
	const int statuses[] = {IO_ERROR_PERMISSION_DENIED, IO_ERROR_NO_SPACE,
	    IO_ERROR_OUT_OF_RANGE, -701};
	for (size_t i = 0; i < sizeof(statuses) / sizeof(statuses[0]); ++i) {
		positions = st_wren_test_truncate_positions;
		seeks = st_wren_test_truncate_seeks;
		calls = st_wren_test_truncate_calls;
		st_wren_test_truncate_error = statuses[i];
		st_wren_test_truncate_errno = i == 0 ? EACCES : 0;
		(void)snprintf(probe, sizeof(probe), "TruncateChecks.fail(2, %d, %d)\n",
		    statuses[i], st_wren_test_truncate_errno);
		run(vm, probe);
		CHECK(st_wren_test_truncate_calls == calls + 1 && st_wren_test_truncate_length == 2);
		CHECK(st_wren_test_truncate_positions == positions + 1
		    && st_wren_test_truncate_seeks == seeks + 1);
		st_wren_test_truncate_error = 0;
		run(vm, "TruncateChecks.intact()\n");
	}
	/* Validate full-width forwarding without asking a filesystem to grow huge files. */
	st_wren_test_truncate_error = IO_ERROR_OUT_OF_RANGE;
	st_wren_test_truncate_errno = 0;
	const uint64_t sizes[] = {UINT64_C(4294967297), UINT64_C(9007199254740991)};
	for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
		calls = st_wren_test_truncate_calls;
		(void)snprintf(probe, sizeof(probe), "TruncateChecks.fail(%.0f, IOErr.outOfRange, 0)\n",
		    (double)sizes[i]);
		run(vm, probe);
		CHECK(st_wren_test_truncate_calls == calls + 1 && st_wren_test_truncate_length == sizes[i]);
	}
	st_wren_test_truncate_error = 0;
	run(vm, "TruncateChecks.errorsSurvive()\nTruncateChecks.intact()\n");
	/* A failed resize can follow a successful flush of preceding buffered data. */
	run(vm, "TruncateChecks.check(TruncateChecks.file.writeBytes(\"XY\") == null)\n");
	st_wren_test_truncate_error = IO_ERROR_NO_SPACE;
	st_wren_test_truncate_errno = ENOSPC;
	(void)snprintf(probe, sizeof(probe), "TruncateChecks.fail(2, IOErr.noSpace, %d)\n", ENOSPC);
	run(vm, probe);
	st_wren_test_truncate_error = 0;
	FILE *persisted = NULL;
	char bytes[6];
	CHECK(fs_open(&persisted, ST_WREN_TRUNCATE_TEST_DIRECTORY "/truncate.bin", "rb") == 0);
	CHECK(fread(bytes, 1, sizeof(bytes), persisted) == sizeof(bytes)
	    && memcmp(bytes, "abcXYf", sizeof(bytes)) == 0 && fclose(persisted) == 0);
	run(vm, "TruncateChecks.check(TruncateChecks.file.offset == 5)\n"
	    "TruncateChecks.file.offset = 3\n");
	positions = st_wren_test_truncate_positions;
	seeks = st_wren_test_truncate_seeks;
	calls = st_wren_test_truncate_calls;
	run(vm, "TruncateChecks.check(TruncateChecks.file.truncate(6) == null)\n");
	CHECK(st_wren_test_truncate_calls == calls + 1
	    && st_wren_test_truncate_positions == positions + 1 && st_wren_test_truncate_seeks == seeks + 1);
	positions = st_wren_test_truncate_positions;
	seeks = st_wren_test_truncate_seeks;
	calls = st_wren_test_truncate_calls;
	run(vm, "TruncateChecks.check(TruncateChecks.file.truncate(0) == null)\n");
	CHECK(st_wren_test_truncate_calls == calls + 1
	    && st_wren_test_truncate_positions == positions + 1 && st_wren_test_truncate_seeks == seeks + 1);
	run(vm, "TruncateChecks.check(TruncateChecks.file.truncate(6) == null)\n"
	    "TruncateChecks.check(TruncateChecks.file.offset == 3 && TruncateChecks.file.size == 6)\n"
	    "TruncateChecks.check(TruncateChecks.file.readBytes(6, 0) == \"\\x00\" * 6)\n");
}

int
main(void)
{
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config config = {.columns = 80, .rows = 25, .wrap = true};
	struct st_app_paths paths = {0};
	st_console_t *console = NULL;
	st_wren_host_t *host = NULL;
	st_hyperlink_registry_t *hyperlinks = NULL;
	CHECK(fs_create_directories(ST_WREN_TRUNCATE_TEST_DIRECTORY) == 0);
	paths.cache_directory = ST_WREN_TRUNCATE_TEST_DIRECTORY;
	paths.system_cache_directory = ST_WREN_TRUNCATE_TEST_DIRECTORY;
	paths.scripts_directory = ST_WREN_TRUNCATE_TEST_DIRECTORY;
	paths.themes_directory = ST_WREN_TRUNCATE_TEST_DIRECTORY;
	paths.download_directory = ST_WREN_TRUNCATE_TEST_DIRECTORY;
	CHECK(st_console_create(&console, &display, &config) == 0);
	CHECK(st_hyperlink_registry_create(&hyperlinks) == 0);
	const struct st_wren_host_config host_config = {
		.console = console, .paths = &paths, .safe_mode = true,
		.application_title = "File truncate tests", .hyperlinks = hyperlinks,
	};
	CHECK(st_wren_host_create(&host, &host_config) == 0);
	free(host->selected.download_directory);
	host->selected.download_directory = strdup(ST_WREN_TRUNCATE_TEST_DIRECTORY);
	CHECK(host->selected.download_directory != NULL);
	host->selected_valid = true;
	struct st_profile profile;
	st_profile_init(&profile);
	free(profile.download_directory);
	profile.download_directory = strdup(ST_WREN_TRUNCATE_TEST_DIRECTORY);
	CHECK(profile.download_directory != NULL);
	const struct st_wren_connected_config connected_config = {
		.host = host, .console = console, .profile = &profile,
	};
	st_wren_connected_t *connected = NULL;
	CHECK(st_wren_connected_create(&connected, &connected_config) == 0);
	char *source = read_source();
	for (unsigned owner = 0; owner < 2; ++owner) {
		WrenVM *vm = host->vm;
		if (owner == 0) {
			run(vm, source);
			run(vm, "TruncateChecks.setup()\n");
		} else {
			CHECK(st_wren_connected_interpret(connected, "file_truncate_test", source) == 0);
			st_wren_test_truncate_vm = NULL;
			CHECK(st_wren_connected_interpret(connected, "file_truncate_test", "TruncateChecks.setup()\n") == 0);
			vm = st_wren_test_truncate_vm;
			CHECK(vm != NULL && vm != host->vm);
		}
		run(vm, "TruncateChecks.successes()\n");
		boundaries(vm);
		grant(vm, ST_WREN_PICKER_DISPOSITION_READ);
		denied(vm);
		run(vm, "TruncateChecks.grantOpen()\n");
		denied(vm);
		run(vm, "TruncateChecks.grantClose()\n");
		grant(vm, ST_WREN_PICKER_DISPOSITION_APPEND);
		run(vm, "TruncateChecks.grantOpen()\n");
		denied(vm);
		run(vm, "TruncateChecks.grantClose()\n");
		struct fs_info info;
		CHECK(fs_get_info(&info, ST_WREN_TRUNCATE_TEST_DIRECTORY "/consent.bin") == 0
		    && info.size == 6);
		for (unsigned create = 0; create < 2; ++create) {
			grant(vm, create ? ST_WREN_PICKER_DISPOSITION_CREATE : ST_WREN_PICKER_DISPOSITION_OVERWRITE);
			denied(vm);
			run(vm, "TruncateChecks.grantOpen()\nTruncateChecks.consent()\n");
		}
		grant(vm, ST_WREN_PICKER_DISPOSITION_NONE);
		run(vm, "TruncateChecks.grantOpen()\n");
		wrenEnsureSlots(vm, 1);
		wrenGetVariable(vm, "file_truncate_test", "TruncateChecks", 0);
		WrenHandle *getter = wrenMakeCallHandle(vm, "granted");
		CHECK(wrenCall(vm, getter) == WREN_RESULT_SUCCESS);
		st_wren_test_truncate_invalidate(vm);
		wrenSetSlotNull(vm, 0);
		wrenReleaseHandle(vm, getter);
		run(vm, "TruncateChecks.openStale()\n");
		denied(vm);
		run(vm, "TruncateChecks.collected()\n");
		unsigned closes = st_wren_test_truncate_closes;
		run(vm, "System.gc()\n");
		CHECK(st_wren_test_truncate_closes == closes + 1);
		/* Keep the main stream open so both VM destruction paths must close it. */
		run(vm, owner == 0 ? "TruncateChecks.keepOpen(\"teardown-menu.bin\")\n"
		    : "TruncateChecks.keepOpen(\"teardown-connected.bin\")\n");
	}
	free(source);
	unsigned closes = st_wren_test_truncate_closes;
	st_wren_connected_destroy(connected);
	CHECK(st_wren_test_truncate_closes == closes + 1);
	st_profile_dispose(&profile);
	st_wren_host_destroy(host);
	CHECK(st_wren_test_truncate_closes == closes + 2);
	st_hyperlink_registry_destroy(hyperlinks);
	st_console_destroy(console);
	return EXIT_SUCCESS;
}

/* RIP block-mode validation and incremental received-scene isolation. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_cterm.h"
#include "cterm.h"
#include "cterm_rip_asset.h"
#include "cterm_rip_engine.h"
#include "filesystem.h"
#include "io_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef ST_RIP_TRANSFER_TEST_POSIX
#include <unistd.h>
#endif

#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); return 1; } } while (0)
struct capture { unsigned requests, cancellations; bool upload; unsigned protocol, type; char name[128]; };
static int request(void *context, const struct st_cterm_rip_transfer_request *r)
{
	struct capture *c = context;
	++c->requests; c->upload = r->upload; c->protocol = r->protocol; c->type = r->file_type;
	snprintf(c->name, sizeof(c->name), "%s", r->filename);
	return 0;
}
static void output(void *context, enum st_cterm_output_origin origin, const uint8_t *bytes, size_t size)
{
	struct capture *c = context;
	if (origin == ST_CTERM_OUTPUT_RESPONSE)
		for (size_t i = 0; i < size; ++i) if (bytes[i] == 24) ++c->cancellations;
}
static int feed(st_cterm_t *terminal, const char *wire)
{
	/* Commands may arrive one byte at a time, including the terminator. */
	for (; *wire; ++wire) {
		size_t consumed;
		int status = st_cterm_write(terminal, wire, 1, &consumed);
		if (status != 0 || consumed != 1) return 1;
	}
	return 0;
}
int main(void)
{
	st_console_t *console = NULL;
	st_font_catalog_t *fonts = NULL;
	st_cterm_t *terminal = NULL;
	struct capture capture = {0};
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config screen = {.columns = 80, .rows = 43, .cell_width = 8,
	    .cell_height = 8, .pixel_width = 640, .pixel_height = 350};
	CHECK(st_console_create(&console, &display, &screen) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	struct st_cterm_config config = {.emulation = ST_CTERM_ANSI_BBS,
	    .encoding = ST_CTERM_ENCODING_CODEPAGE, .codepage = ST_CODEPAGE_CP437,
	    .graphics_mode = ST_CTERM_GRAPHICS_RIP_3, .rip_directory = RIP_TRANSFER_TEST_DIRECTORY,
	    .fonts = fonts, .console = console, .output = output, .output_context = &capture,
	    .rip_transfer = request, .rip_transfer_context = &capture};
	CHECK(st_cterm_create(&terminal, &config) == 0);
	for (unsigned protocol = 0; protocol < 8; ++protocol) {
		char wire[100];
		if (protocol == 4) continue; /* No Kermit engine. */
		snprintf(wire, sizeof(wire), "!|9\x1b" "0%u010000ASSET.RIP<>\r", protocol);
		unsigned before = capture.requests;
		CHECK(feed(terminal, wire) == 0 && capture.requests == before + 1);
		CHECK(!capture.upload && capture.protocol == protocol && capture.type == 1);
		CHECK(strcmp(capture.name, "ASSET.RIP") == 0);
	}
	for (unsigned type = 0; type <= 5; ++type) {
		char wire[100];
		snprintf(wire, sizeof(wire), "!|9\x1b" "070%u0000<>\r", type);
		unsigned before = capture.requests;
		CHECK(feed(terminal, wire) == 0 && capture.requests == before + 1);
		CHECK(capture.type == type && capture.name[0] == '\0');
	}
	CHECK(feed(terminal, "!|9\x1b" "17ZZ0000ASSET.RIP<>\r") == 0);
	CHECK(capture.upload); /* File type ignored for uploads. */
	unsigned before = capture.requests, cancellations = capture.cancellations;
	const char *invalid[] = {
	    "!|9\x1b" "07010000<>|#\r", /* CR is mandatory. */
	    "!|9\x1b" "07010000\r", "!|9\x1b" "0701000<>\r", "!|9\x1b" "27010000<>\r",
	    "!|9\x1b" "08010000<>\r", "!|9\x1b" "04010000FILE.RIP<>\r", "!|9\x1b" "07060000<>\r",
	    "!|9\x1b" "00050000FILE.RIP<>\r", "!|9\x1b" "01010000<>\r", "!|9\x1b" "17010000<>\r",
	    "!|9\x1b" "07010000../ESCAPE.RIP<>\r", "!|9\x1b" "07010000NUL.RIP<>\r",
	    "!|9\x1b" "07010000FILE.RIP.<>\r", "!|9\x1b" "07010000SUB/FILE.RIP<>\r",
	};
	for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) CHECK(feed(terminal, invalid[i]) == 0);
	CHECK(capture.requests == before && capture.cancellations == cancellations + 10 * sizeof(invalid) / sizeof(invalid[0]));
	/* A file's parser survives arbitrary transfer-block splits, and cannot
	 * recursively seize the network. A failed file never flushes its tail. */
	CHECK(st_cterm_rip_receive_begin(terminal) == 0);
	CHECK(st_cterm_rip_receive_begin(terminal) == IO_ERROR_BUSY);
	const char *nested = "!|9\x1b" "07010000<>\r\n";
	CHECK(st_cterm_rip_receive_data(terminal, (const uint8_t *)nested, strlen(nested)) == 0);
	CHECK(capture.requests == before);
	size_t commands = terminal->rip_engine->commands_seen;
	CHECK(st_cterm_rip_receive_data(terminal, (const uint8_t *)"!|c", 3) == 0);
	CHECK(st_cterm_rip_receive_data(terminal, (const uint8_t *)"04\r", 3) == 0);
	CHECK(terminal->rip_engine->commands_seen == commands + 1);
	CHECK(st_cterm_rip_receive_data(terminal, (const uint8_t *)"!|c02", 5) == 0);
	CHECK(st_cterm_rip_receive_end(terminal, false) == 0);
	CHECK(terminal->rip_engine->commands_seen == commands + 1);
	CHECK(feed(terminal, "!|9\x1b" "07000000<>\r") == 0 && capture.requests == before + 1);
	CHECK(st_cterm_rip_receive_begin(terminal) == 0);
	CHECK(st_cterm_rip_receive_data(terminal, (const uint8_t *)"!|c03", 5) == 0);
	CHECK(st_cterm_rip_receive_end(terminal, true) == 0);
	CHECK(terminal->rip_engine->commands_seen == commands + 3);
	/* The same path rules apply to transfer I/O and later icon/scene reads. */
	CHECK(fs_create_directories(RIP_TRANSFER_TEST_DIRECTORY) == 0);
	char *path = NULL, *resolved = NULL;
	CHECK(cterm_rip_asset_transfer_path(&path, RIP_TRANSFER_TEST_DIRECTORY, "Mixed.RIP", false) == 0);
	CHECK(fs_replace_file(path, "!|c04\r", 6) == 0);
	CHECK(cterm_rip_asset_transfer_path(&resolved, RIP_TRANSFER_TEST_DIRECTORY, "mixed.rip", true) == 0);
	/* A case-insensitive filesystem can preserve the caller's spelling.
	 * Verify that lookup reaches the existing asset, not path spelling. */
	FILE *stream = NULL;
	char contents[7] = {0};
	CHECK(fs_open(&stream, resolved, "rb") == 0);
	CHECK(fread(contents, 1, sizeof(contents), stream) == 6 && fclose(stream) == 0);
	CHECK(strcmp(contents, "!|c04\r") == 0); free(resolved);
	CHECK(cterm_rip_asset_transfer_path(&resolved, RIP_TRANSFER_TEST_DIRECTORY, "missing.rip", true) == IO_ERROR_NOT_FOUND);
	CHECK(cterm_rip_asset_transfer_path(&resolved, RIP_TRANSFER_TEST_DIRECTORY, "../escape.rip", false) != 0);
	CHECK(cterm_rip_asset_transfer_path(&resolved, RIP_TRANSFER_TEST_DIRECTORY, "syncterm-cache-name.txt", false) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(cterm_rip_asset_transfer_path(&resolved, RIP_TRANSFER_TEST_DIRECTORY, "SYNCTERM-CACHE-NAME.TXT", true) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(cterm_rip_asset_transfer_path(&resolved, path, "asset.rip", false) != 0);
#ifdef ST_RIP_TRANSFER_TEST_POSIX
	char link[1024];
	snprintf(link, sizeof(link), "%s/LINK.RIP", RIP_TRANSFER_TEST_DIRECTORY);
	CHECK(symlink(path, link) == 0);
	CHECK(cterm_rip_asset_transfer_path(&resolved, RIP_TRANSFER_TEST_DIRECTORY, "link.rip", false) != 0);
	CHECK(cterm_rip_asset_transfer_path(&resolved, RIP_TRANSFER_TEST_DIRECTORY, "LINK.RIP", true) != 0);
	CHECK(fs_remove_file(link) == 0);
	snprintf(link, sizeof(link), "%s-link", RIP_TRANSFER_TEST_DIRECTORY);
	CHECK(symlink(RIP_TRANSFER_TEST_DIRECTORY, link) == 0);
	CHECK(cterm_rip_asset_transfer_path(&resolved, link, "Mixed.RIP", false) != 0);
	CHECK(fs_remove_file(link) == 0);
#endif
	CHECK(fs_remove_file(path) == 0); free(path);
	st_cterm_destroy(&terminal);
	st_font_catalog_destroy(fonts); st_console_destroy(console);
	return 0;
}

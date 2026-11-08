/* Wren/native codepage parity and independent contracts in all three VMs. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */

#include "wren_host_internal.h"
#include "filesystem.h"
#include "st_app_paths.h"
#include "st_amiga_font.h"
#include "st_builtin_fonts.h"
#include "st_cterm.h"
#include "st_font_catalog.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
		exit(EXIT_FAILURE); \
	} \
} while (false)

static char *
read_source(void)
{
	FILE *stream = fopen(ST_WREN_CODEPAGE_TEST_SOURCE, "rb");
	CHECK(stream != NULL && fseek(stream, 0, SEEK_END) == 0);
	long length = ftell(stream);
	CHECK(length > 0 && length < 65536);
	char *source = malloc((size_t)length + 1);
	CHECK(source != NULL && fseek(stream, 0, SEEK_SET) == 0);
	CHECK(fread(source, 1, (size_t)length, stream) == (size_t)length);
	source[length] = '\0';
	CHECK(fclose(stream) == 0);
	return source;
}

static void
interpret(WrenVM *vm, st_wren_connected_t *connected, const char *source)
{
	CHECK(connected != NULL
	    ? st_wren_connected_interpret(connected, "codepage_test", source) == 0
	    : wrenInterpret(vm, "codepage_test", source) == WREN_RESULT_SUCCESS);
}

static void
exercise(WrenVM *vm, st_wren_connected_t *connected, const char *source)
{
	char *probe = malloc(65536);
	CHECK(probe != NULL);
	interpret(vm, connected, source);
	/* Cross-check the binding boundary against C; the Wren vectors above
	 * and frozen native tests independently establish table semantics. */
	for (int cp = 0; cp < ST_CODEPAGE_COUNT; ++cp) {
		int size = snprintf(probe, 65536,
		    "CodepageChecks.check(Codepage.brokenVertical(%d) == %s)\n",
		    cp, st_codepage_broken_vertical((enum st_codepage)cp) ? "true" : "false");
		CHECK(size > 0 && size < 65536);
		size_t used = (size_t)size;
		for (unsigned graphic = 0; graphic < 2; ++graphic) {
			for (unsigned byte = 0; byte <= UINT8_MAX; ++byte) {
				uint32_t scalar = st_codepage_to_unicode((enum st_codepage)cp,
				    (uint8_t)byte, graphic != 0);
				uint8_t encoded;
				char result[16] = "null";
				if (st_codepage_from_unicode(&encoded, (enum st_codepage)cp, scalar))
					(void)snprintf(result, sizeof(result), "%u", (unsigned)encoded);
				size = snprintf(probe + used, 65536 - used,
				    "CodepageChecks.native(%d, %u, %s, %lu, %s)\n", cp,
				    byte, graphic ? "true" : "false", (unsigned long)scalar, result);
				CHECK(size > 0 && (size_t)size < 65536 - used);
				used += (size_t)size;
			}
		}
		interpret(vm, connected, probe);
	}
	for (size_t selector = 0; selector < ST_BUILTIN_FONT_COUNT; ++selector) {
		int size = snprintf(probe, 65536,
		    "CodepageChecks.check(Font.builtinBrokenVertical(%zu) == %s)\n",
		    selector, st_builtin_font_broken_vertical(selector) ? "true" : "false");
		CHECK(size > 0 && size < 65536);
		interpret(vm, connected, probe);
	}
	free(probe);
}

int
st_wren_codepage_test_main(void)
{
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry = {
		.columns = 80, .rows = 25, .cell_width = 8, .cell_height = 16, .wrap = true,
	};
	struct st_app_paths paths = {0};
	st_console_t *console = NULL;
	st_wren_host_t *host = NULL;
	st_hyperlink_registry_t *hyperlinks = NULL;
	CHECK(fs_create_directories(ST_WREN_CODEPAGE_TEST_DIRECTORY) == 0);
	paths.cache_directory = ST_WREN_CODEPAGE_TEST_DIRECTORY;
	paths.system_cache_directory = ST_WREN_CODEPAGE_TEST_DIRECTORY;
	paths.scripts_directory = ST_WREN_CODEPAGE_TEST_DIRECTORY;
	paths.themes_directory = ST_WREN_CODEPAGE_TEST_DIRECTORY;
	paths.download_directory = ST_WREN_CODEPAGE_TEST_DIRECTORY;
	CHECK(st_console_create(&console, &display, &geometry) == 0);
	CHECK(st_hyperlink_registry_create(&hyperlinks) == 0);
	const struct st_wren_host_config host_config = {
		.console = console, .paths = &paths, .safe_mode = true,
		.application_title = "Codepage tests", .hyperlinks = hyperlinks,
	};
	CHECK(st_wren_host_create(&host, &host_config) == 0);
	char *source = read_source();
	for (unsigned owner = 0; owner < 2; ++owner) {
		struct st_wren_context *context = owner == 0 ? &host->menu : &host->picker;
		bool available = context->fonts.catalog_valid[2];
		/* Omit a true-flag built-in from this VM's active registrations. */
		context->fonts.catalog_valid[2] = false;
		exercise(context->vm, NULL, source);
		interpret(context->vm, NULL,
		    "CodepageChecks.check(!Font.available(2))\n"
		    "CodepageChecks.check(Font.builtinBrokenVertical(2))\n");
		context->fonts.catalog_valid[2] = available;
	}
	struct st_profile profile;
	st_profile_init(&profile);
	for (unsigned utf8 = 0; utf8 < 2; ++utf8) {
		st_font_catalog_t *fonts = NULL;
		st_cterm_t *terminal = NULL;
		st_wren_connected_t *connected = NULL;
		CHECK(st_font_catalog_create(&fonts, console) == 0);
		/* A replacement raster has its own mapping, not the bundled flag. */
		CHECK(st_font_catalog_load_amiga(fonts, 2,
		    st_amiga_font_builtin("pearl.font", 8), ST_CODEPAGE_CP1251) == 0);
		const struct st_cterm_config terminal_config = {
			.console = console, .fonts = fonts, .emulation = ST_CTERM_ANSI_BBS,
			.encoding = utf8 ? ST_CTERM_ENCODING_UTF8 : ST_CTERM_ENCODING_CODEPAGE,
			.codepage = utf8 ? ST_CODEPAGE_CP1251 : ST_CODEPAGE_CP437, .quiet = true,
		};
		CHECK(st_cterm_create(&terminal, &terminal_config) == 0);
		const struct st_wren_connected_config connected_config = {
			.host = host, .console = console, .profile = &profile, .cterm = terminal,
		};
		CHECK(st_wren_connected_create(&connected, &connected_config) == 0);
		exercise(NULL, connected, source);
		interpret(NULL, connected,
		    "CodepageChecks.check(Font.codepageFor(2) == Codepage.cp1251)\n"
		    "CodepageChecks.check(Font.builtinBrokenVertical(2))\n");
		/* The download protocol owns only dynamic selectors. A downloaded
		 * font must not make one valid for the built-in metadata query. */
		CHECK(st_cterm_set_encoding(terminal, ST_CTERM_ENCODING_UTF8, ST_CODEPAGE_CP437) == 0);
		static const char download[] = "\x1b[=250;0{";
		static const uint8_t blank_font[4096] = {0};
		size_t consumed;
		CHECK(st_cterm_write(terminal, download, sizeof(download) - 1, &consumed) == 0);
		CHECK(consumed == sizeof(download) - 1);
		CHECK(st_cterm_write(terminal, blank_font, sizeof(blank_font), &consumed) == 0);
		CHECK(consumed == sizeof(blank_font));
		interpret(NULL, connected,
		    "CodepageChecks.check(Font.codepageFor(250) == Codepage.cp437)\n"
		    "CodepageChecks.rejects(Fn.new { Font.builtinBrokenVertical(250) })\n"
		    "CodepageChecks.vectors()\n");
		st_wren_connected_destroy(connected);
		st_cterm_destroy(&terminal);
		st_font_catalog_destroy(fonts);
	}
	st_profile_dispose(&profile);
	free(source);
	st_wren_host_destroy(host);
	st_hyperlink_registry_destroy(hyperlinks);
	st_console_destroy(console);
	return EXIT_SUCCESS;
}

/* Cell style contracts in all VMs; native fixtures and literal raster expectations. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "wren_host_internal.h"
#include "filesystem.h"
#include "st_app_paths.h"
#include "st_cterm.h"
#include "st_font_catalog.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
		exit(EXIT_FAILURE); \
	} \
} while (false)

static void
interpret(WrenVM *vm, st_wren_connected_t *connected, const char *source)
{
	CHECK(connected != NULL
	    ? st_wren_connected_interpret(connected, "cell_styles", source) == 0
	    : wrenInterpret(vm, "cell_styles", source) == WREN_RESULT_SUCCESS);
}

static void
exercise(WrenVM *vm, st_wren_connected_t *connected, st_console_t *console,
	const char *source, uint16_t hyperlink)
{
	interpret(vm, connected, source);
	const uint32_t style_mask = ST_CONSOLE_CELL_BRIGHT | ST_CONSOLE_CELL_BLINK
	    | ST_CONSOLE_CELL_UNDERLINE | ST_CONSOLE_CELL_BOLD
	    | ST_CONSOLE_CELL_REVERSE | ST_CONSOLE_CELL_CONCEAL;
	const uint32_t metadata = ST_CONSOLE_CELL_SAA5050 | ST_CONSOLE_CELL_DOUBLE_HEIGHT
	    | ST_CONSOLE_CELL_REVEAL | ST_CONSOLE_CELL_SEPARATED
	    | ST_CONSOLE_CELL_SAA5050_CONTROL(0x5d);
	struct st_console_cell input = {.glyph = 'A', .font = 0, .glyph_font = 0,
		.foreground = ST_CONSOLE_COLOR_RGB_VALUE(17, 34, 51),
		.background = ST_CONSOLE_COLOR_RGB_VALUE(68, 85, 102),
		.flags = metadata | style_mask, .hyperlink = hyperlink};
	const struct st_console_rect origin = {0, 0, 1, 1}, destination = {2, 0, 1, 1};
	const uint32_t metadata_bits[] = {ST_CONSOLE_CELL_SAA5050,
		ST_CONSOLE_CELL_DOUBLE_HEIGHT, ST_CONSOLE_CELL_REVEAL, ST_CONSOLE_CELL_SEPARATED};
	for (unsigned bit = 0; bit < 4; ++bit) {
		struct st_console_cell probe = input;
		probe.flags = metadata_bits[bit];
		CHECK(st_console_write_cells(console, &origin, &probe, 1) == 0);
		char script[256];
		CHECK(snprintf(script, sizeof(script),
		    "CellStyleChecks.metadataBits(%s, %s, %s, %s, null)\n",
		    bit == 0 ? "true" : "false", bit == 1 ? "true" : "false",
		    bit == 2 ? "true" : "false", bit == 3 ? "true" : "false") < (int)sizeof(script));
		interpret(vm, connected, script);
	}
	for (unsigned control = 0; control < 128; ++control) {
		struct st_console_cell probe = input;
		probe.flags = ST_CONSOLE_CELL_SAA5050_CONTROL(control);
		CHECK(st_console_write_cells(console, &origin, &probe, 1) == 0);
		char script[256], number[16];
		CHECK(snprintf(number, sizeof(number), "%u", control) < (int)sizeof(number));
		CHECK(snprintf(script, sizeof(script),
		    "CellStyleChecks.metadataBits(false, false, false, false, %s)\n",
		    control == 0 ? "null" : number) < (int)sizeof(script));
		interpret(vm, connected, script);
	}
	CHECK(st_console_write_cells(console, &origin, &input, 1) == 0);
	struct st_console_cell plain = input;
	plain.flags = ST_CONSOLE_CELL_LEGACY_FOREGROUND | ST_CONSOLE_CELL_LEGACY_BACKGROUND;
	const struct st_console_rect neighbour = {1, 0, 1, 1};
	CHECK(st_console_write_cells(console, &neighbour, &plain, 1) == 0);
	interpret(vm, connected, "CellStyleChecks.metadata()\n");
	struct st_console_cell result;
	CHECK(st_console_read_cells(console, &destination, &result, 1) == 0);
	CHECK((result.flags & ST_CONSOLE_CELL_PUBLIC_FLAGS)
	    == (metadata | ST_CONSOLE_CELL_BOLD | ST_CONSOLE_CELL_UNDERLINE));
	CHECK(result.glyph == input.glyph && result.glyph_font == input.glyph_font
	    && result.font == input.font && result.foreground == input.foreground
	    && result.background == input.background && result.hyperlink == input.hyperlink);
	/* A group update must retain legacy colour-origin flags as well. */
	interpret(vm, connected,
	    "var legacy = Screen.readRect(2, 1, 2, 1)\n"
	    "legacy[0].styleFlags = CellStyle.reverse\n"
	    "Screen.putRect(legacy, 4, 1)\n");
	const struct st_console_rect legacy_destination = {3, 0, 1, 1};
	CHECK(st_console_read_cells(console, &legacy_destination, &result, 1) == 0);
	CHECK((result.flags & ST_CONSOLE_CELL_PUBLIC_FLAGS)
	    == (plain.flags | ST_CONSOLE_CELL_REVERSE));

	struct st_pixel_buffer *pixels = NULL, *captured = NULL;
	CHECK(st_pixel_buffer_create(&pixels, 16, 16, true) == 0);
	for (size_t i = 0; i < 256; ++i) {
		pixels->primary[i] = ST_PIXEL_COLOR_RGB_VALUE(i, 17, 33);
		pixels->alternate[i] = ST_PIXEL_COLOR_RGB_VALUE(9, i, 65);
	}
	const struct st_pixel_blit blit = {.width = 16, .height = 16,
		.destination_y = 32, .scale_x = 1, .scale_y = 1};
	CHECK(st_console_blit_pixels(console, pixels, NULL, &blit) == 0);
	interpret(vm, connected, "CellStyleChecks.graphics()\n");
	const struct st_console_rect raster = {0, 48, 16, 16};
	CHECK(st_console_get_pixels(&captured, console, &raster, false) == 0);
	CHECK(captured->alternate != NULL);
	CHECK(memcmp(captured->primary, pixels->primary, 256 * sizeof(uint32_t)) == 0);
	CHECK(memcmp(captured->alternate, pixels->alternate, 256 * sizeof(uint32_t)) == 0);
	st_pixel_buffer_destroy(captured);
	st_pixel_buffer_destroy(pixels);

	struct st_console_cell glyphs[4];
	for (size_t i = 0; i < 4; ++i) {
		glyphs[i] = input;
		glyphs[i].flags = 0;
		glyphs[i].hyperlink = 0;
	}
	const struct st_console_rect glyph_area = {0, 5, 4, 1};
	CHECK(st_console_write_cells(console, &glyph_area, glyphs, 4) == 0);
	interpret(vm, connected, "CellStyleChecks.bitmap()\n");
	const struct st_console_rect bitmap = {0, 80, 32, 16};
	captured = NULL;
	CHECK(st_console_get_pixels(&captured, console, &bitmap, false) == 0);
	for (unsigned y = 0; y < 16; ++y) {
		for (unsigned x = 0; x < 32; ++x) {
			unsigned column = x / 8, bit = x % 8;
			/* Fixture A is one vertical stroke (0x20). Bold adds 0x10;
			 * underline fills its final row; reverse swaps RGB; conceal hides. */
			bool foreground = column == 0 ? bit == 2 || bit == 3
			    : column == 1 ? bit == 2 || y == 15
			    : column == 2 ? bit != 2 : false;
			uint32_t expected = foreground ? input.foreground : input.background;
			if (captured->primary[y * 32 + x] != expected)
				fprintf(stderr, "pixel %u,%u: %08lx expected %08lx\n", x, y,
				    (unsigned long)captured->primary[y * 32 + x], (unsigned long)expected);
			CHECK(captured->primary[y * 32 + x] == expected);
		}
	}
	st_pixel_buffer_destroy(captured);
}

int
st_wren_cell_styles_test_main(void)
{
	static const uint8_t bitmap[256 * 16] = {
		['A' * 16] = 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
		0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
	};
	static const uint8_t wide[32] = {0};
	static const struct st_console_font_mapping mapping[] = {
		{32, 32}, {63, 63}, {65, 65}, {66, 66}, {0x4e00, 256},
	};
	const struct st_console_font font = {.bitmap = bitmap, .bitmap_size = sizeof(bitmap),
		.width = 8, .height = 16, .glyph_count = 256, .row_stride = 1, .glyph_stride = 16,
		.wide_bitmap = wide, .wide_bitmap_size = sizeof(wide), .wide_glyph_count = 1,
		.wide_row_stride = 2, .wide_glyph_stride = 32, .mapping = mapping,
		.mapping_count = sizeof(mapping) / sizeof(mapping[0]), .fallback_glyph = 63};
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry = {.columns = 80, .rows = 25,
		.cell_width = 8, .cell_height = 16, .wrap = true};
	st_console_t *console = NULL;
	CHECK(st_console_create(&console, &display, &geometry) == 0);
	uint16_t identifier;
	CHECK(st_console_register_static_font(console, &font, &identifier) == 0 && identifier == 0);
	CHECK(st_console_set_fallback_font(console, identifier) == 0);
	struct st_app_paths paths = {0};
	CHECK(fs_create_directories(ST_WREN_CELL_STYLES_TEST_DIRECTORY) == 0);
	paths.cache_directory = ST_WREN_CELL_STYLES_TEST_DIRECTORY;
	paths.system_cache_directory = ST_WREN_CELL_STYLES_TEST_DIRECTORY;
	paths.scripts_directory = ST_WREN_CELL_STYLES_TEST_DIRECTORY;
	paths.themes_directory = ST_WREN_CELL_STYLES_TEST_DIRECTORY;
	paths.download_directory = ST_WREN_CELL_STYLES_TEST_DIRECTORY;
	st_wren_host_t *host = NULL;
	st_hyperlink_registry_t *hyperlinks = NULL;
	CHECK(st_hyperlink_registry_create(&hyperlinks) == 0);
	const struct st_wren_host_config host_config = {.console = console, .paths = &paths,
		.safe_mode = true, .application_title = "Cell style tests", .hyperlinks = hyperlinks};
	CHECK(st_wren_host_create(&host, &host_config) == 0);
	FILE *file = fopen(ST_WREN_CELL_STYLES_TEST_SOURCE, "rb");
	CHECK(file != NULL && fseek(file, 0, SEEK_END) == 0);
	long size = ftell(file);
	CHECK(size > 0 && size < 65536 && fseek(file, 0, SEEK_SET) == 0);
	uint16_t hyperlink;
	static const char uri[] = "https://example.invalid/styles";
	CHECK(st_hyperlink_registry_add(hyperlinks, uri, sizeof(uri) - 1, NULL, 0, &hyperlink) == 0);
	char *source = malloc((size_t)size + 1);
	CHECK(source != NULL && fread(source, 1, (size_t)size, file) == (size_t)size);
	source[size] = '\0';
	CHECK(fclose(file) == 0);
	exercise(host->menu.vm, NULL, console, source, hyperlink);
	exercise(host->picker.vm, NULL, console, source, hyperlink);
	struct st_profile profile;
	st_profile_init(&profile);
	for (unsigned utf8 = 0; utf8 < 2; ++utf8) {
		st_font_catalog_t *fonts = NULL;
		st_cterm_t *terminal = NULL;
		st_wren_connected_t *connected = NULL;
		CHECK(st_font_catalog_create(&fonts, console) == 0);
		const struct st_cterm_config terminal_config = {.console = console, .fonts = fonts,
			.emulation = ST_CTERM_ANSI_BBS, .encoding = utf8 ? ST_CTERM_ENCODING_UTF8
			    : ST_CTERM_ENCODING_CODEPAGE, .codepage = ST_CODEPAGE_CP437, .quiet = true};
		CHECK(st_cterm_create(&terminal, &terminal_config) == 0);
		const struct st_wren_connected_config config = {.host = host,
			.console = console, .profile = &profile, .cterm = terminal};
		CHECK(st_wren_connected_create(&connected, &config) == 0);
		exercise(NULL, connected, console, source, hyperlink);
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

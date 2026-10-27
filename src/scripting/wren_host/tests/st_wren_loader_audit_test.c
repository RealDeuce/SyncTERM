/* Isolated regressions for documented event loading and picker recovery. */
/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "wren_host_internal.h"
#include "filesystem.h"
#include "st_app_paths.h"
#include "st_builtin_fonts.h"
#include "st_console.h"
#include "st_presentation_font.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", \
		    __FILE__, __LINE__, #expression); \
		exit(EXIT_FAILURE); \
	} \
} while (false)

static const char *catalog_files[] = {
	"Alpha.wren", "beta.wren", "syncterm.wren", "syncterm_menu.wren",
	"syncterm_picker.wren", "picker_bootstrap.wren", "bad-name.wren",
	"ignored.WREN", ".wren",
};

static void
write_script(const char *path, const char *source)
{
	FILE *stream = fopen(path, "wb");

	CHECK(stream != NULL);
	CHECK(fputs(source, stream) >= 0);
	CHECK(fclose(stream) == 0);
}

int
main(int argc, char **argv)
{
	char menu[1024], picker[1024], script[1100];
	struct st_app_paths paths = {0};
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config config = {
		.columns = 80, .rows = 25, .wrap = true,
	};
	st_console_t *console = NULL;
	st_wren_host_t *host = NULL;
	st_hyperlink_registry_t *hyperlinks = NULL;
	struct st_console_font builtin_font;
	struct st_console_font_mapping *builtin_mapping = NULL;
	struct st_presentation_font presentation_font = {0};
	uint16_t audit_wide_font = 0;
	bool have_audit_wide_font = false;
	bool passed = false;
	int status;

	/* argv[1] must be a fresh, caller-owned temporary directory. */
	CHECK(argc == 3);
	CHECK(strcmp(argv[2], "script-catalog") == 0
	    || strcmp(argv[2], "settings-stale") == 0
	    || strcmp(argv[2], "settings-save-failure") == 0
	    || strcmp(argv[2], "settings-reload") == 0
	    || strcmp(argv[2], "screen-mode") == 0
	    || strcmp(argv[2], "core-helpers") == 0
	    || strcmp(argv[2], "cell-colours") == 0
	    || strcmp(argv[2], "draw-suite") == 0
	    || strcmp(argv[2], "input-suite") == 0
	    || strcmp(argv[2], "logview-suite") == 0
	    || strcmp(argv[2], "popup-suite") == 0
	    || strcmp(argv[2], "help-suite") == 0
	    || strcmp(argv[2], "progress-suite") == 0
	    || strcmp(argv[2], "menu-suite") == 0
	    || strcmp(argv[2], "wren-console-suite") == 0);
	CHECK(strlen(argv[1]) < 900);
	(void)snprintf(menu, sizeof(menu), "%s/auto/menu", argv[1]);
	(void)snprintf(picker, sizeof(picker), "%s/auto/picker", argv[1]);
	CHECK(fs_create_directories(menu) == 0);
	CHECK(fs_create_directories(picker) == 0);
	if (strcmp(argv[2], "script-catalog") == 0) {
		for (size_t i = 0; i < sizeof(catalog_files) / sizeof(catalog_files[0]); ++i) {
			(void)snprintf(script, sizeof(script), "%s/%s", argv[1], catalog_files[i]);
			(void)fs_remove_file(script);
		}
		(void)snprintf(script, sizeof(script), "%s/directory.wren", argv[1]);
		(void)fs_remove_directory(script);
	}
	paths.scripts_directory = argv[1];
	paths.themes_directory = argv[1];
	paths.cache_directory = argv[1];
	paths.system_cache_directory = argv[1];
	paths.download_directory = argv[1];
	CHECK(st_console_create(&console, &display, &config) == 0);
	CHECK(st_hyperlink_registry_create(&hyperlinks) == 0);
	CHECK(st_builtin_font_get(0, 8, 16, &builtin_font));
	builtin_mapping = malloc((builtin_font.mapping_count + 1)
	    * sizeof(builtin_mapping[0]));
	CHECK(builtin_mapping != NULL);
	{
		size_t insertion = 0;

		while (insertion < builtin_font.mapping_count
		    && builtin_font.mapping[insertion].codepoint
		    < UINT32_C(0x4e00))
			++insertion;
		memcpy(builtin_mapping, builtin_font.mapping,
		    insertion * sizeof(builtin_mapping[0]));
		builtin_mapping[insertion]
		    = (struct st_console_font_mapping){UINT32_C(0x4e00),
		    builtin_font.glyph_count};
		memcpy(builtin_mapping + insertion + 1,
		    builtin_font.mapping + insertion,
		    (builtin_font.mapping_count - insertion)
		    * sizeof(builtin_mapping[0]));
	}
	builtin_font.mapping = builtin_mapping;
	++builtin_font.mapping_count;
	{
		static const uint8_t wide_bitmap[32] = {0};

		builtin_font.wide_bitmap = wide_bitmap;
		builtin_font.wide_bitmap_size = sizeof(wide_bitmap);
		builtin_font.wide_glyph_count = 1;
		builtin_font.wide_row_stride = 2;
		builtin_font.wide_glyph_stride = 32;
	}
	CHECK(st_presentation_font_create(&presentation_font, &builtin_font) == 0);
	free(builtin_mapping);
	builtin_mapping = NULL;
	CHECK(st_console_register_static_font(console, &presentation_font.font,
	    &audit_wide_font) == 0);
	CHECK(audit_wide_font == 0);
	if (strcmp(argv[2], "popup-suite") == 0
	    || strcmp(argv[2], "help-suite") == 0
	    || strcmp(argv[2], "progress-suite") == 0
	    || strcmp(argv[2], "menu-suite") == 0) {
		static const uint8_t narrow_bitmap[16] = {0};
		static const uint8_t wide_bitmap[32] = {0};
		static const struct st_console_font_mapping wide_mapping[] = {
			{'?', 0}, {'A', 1}, {UINT32_C(0x4e00), 1},
		};
		const struct st_console_font wide_font = {
			.bitmap = narrow_bitmap,
			.bitmap_size = sizeof(narrow_bitmap),
			.width = 8,
			.height = 16,
			.glyph_count = 1,
			.row_stride = 1,
			.glyph_stride = 16,
			.wide_bitmap = wide_bitmap,
			.wide_bitmap_size = sizeof(wide_bitmap),
			.wide_glyph_count = 1,
			.wide_row_stride = 2,
			.wide_glyph_stride = 32,
			.mapping = wide_mapping,
			.mapping_count = sizeof(wide_mapping)
			    / sizeof(wide_mapping[0]),
		};

		CHECK(st_console_register_static_font(console, &wide_font,
		    &audit_wide_font) == 0);
		CHECK(audit_wide_font == 1);
		have_audit_wide_font = true;
	}
	const struct st_wren_host_config host_config = {
		.console = console, .paths = &paths,
		.application_title = "Loader audit",
		.safe_mode = true,
		.hyperlinks = hyperlinks,
	};
	status = st_wren_host_create(&host, &host_config);
	if (status == 0 && have_audit_wide_font) {
		struct st_wren_context *context =
		    &((struct st_wren_host *)host)->menu;

		context->fonts.catalog[UINT8_MAX] = audit_wide_font;
		context->fonts.catalog_valid[UINT8_MAX] = true;
		context->fonts.selector_by_identifier[audit_wide_font] = UINT8_MAX;
	}
	if (strcmp(argv[2], "script-catalog") == 0) {
		char maximum_name[256];
		char excessive_name[257];

		CHECK(status == 0);
		memset(maximum_name, 'a', sizeof(maximum_name) - 1);
		maximum_name[sizeof(maximum_name) - 1] = '\0';
		memset(excessive_name, 'a', sizeof(excessive_name) - 1);
		excessive_name[sizeof(excessive_name) - 1] = '\0';
		CHECK(st_wren_bbs_script_name_valid(maximum_name));
		CHECK(!st_wren_bbs_script_name_valid(excessive_name));
		CHECK(!st_wren_bbs_script_name_valid(""));
		for (size_t i = 0;
		    i < sizeof(catalog_files) / sizeof(catalog_files[0]); ++i) {
			(void)snprintf(script, sizeof(script), "%s/%s", argv[1],
			    catalog_files[i]);
			write_script(script, "// catalog fixture\n");
		}
		(void)snprintf(script, sizeof(script), "%s/directory.wren", argv[1]);
		CHECK(fs_create_directories(script) == 0);
		passed = wrenInterpret(((struct st_wren_host *)host)->vm,
		    "script_catalog_test",
		    "import \"syncterm_menu\" for Menu\n"
		    "var first = Menu.scriptModules\n"
		    "if (first.count != 3 || first[0] != \"Alpha\" || "
		    "first[1] != \"beta\" || first[2] != \"syncterm\") "
		    "Fiber.abort(\"wrong script catalog\")\n"
		    "first.add(\"changed\")\n"
		    "var second = Menu.scriptModules\n"
		    "if (second.count != 3 || second.indexOf(\"changed\") >= 0) "
		    "Fiber.abort(\"script catalog was not fresh\")\n")
		    == WREN_RESULT_SUCCESS;
		fprintf(stderr, "root script catalog filtering and ownership: %s\n",
		    passed ? "PASS" : "FAIL");
	}
	else if (strcmp(argv[2], "screen-mode") == 0) {
		char source[512];
		uint32_t columns, rows;
		uint32_t pixel_width, pixel_height;
		uint32_t aspect_width, aspect_height;
		uint32_t palette[16];
		struct st_console_custom_cursor cursor;
		struct st_console_style style;
		enum st_console_blink_mode blink;
		CHECK(status == 0);
		(void)snprintf(source, sizeof(source),
		    "import \"syncterm_menu\" for Menu\n"
		    "if (!Menu.setScreenMode(%d)) Fiber.abort(\"mode rejected\")\n",
		    ST_SCREEN_MODE_PRESTEL);
		CHECK(wrenInterpret(host->vm, "mode_geometry", source)
		    == WREN_RESULT_SUCCESS);
		CHECK(st_console_size(console, &columns, &rows) == 0);
		CHECK(st_console_cell_pixel_size(console, &pixel_width,
		    &pixel_height) == 0);
		bool geometry = columns == 40 && rows == 25
		    && pixel_width == 12 && pixel_height == 20;
		CHECK(st_console_pixel_size(console, &pixel_width, &pixel_height) == 0);
		geometry = geometry && pixel_width == 480 && pixel_height == 500;
		CHECK(st_console_get_aspect_ratio(console, &aspect_width,
		    &aspect_height) == 0);
		geometry = geometry && aspect_width == 3077 && aspect_height == 2400;
		CHECK(st_console_get_blink_mode(console, &blink) == 0);
		CHECK(st_console_get_default_cursor(console, &cursor) == 0);
		CHECK(st_console_get_palette(console, 0, palette, 16) == 0);
		CHECK(st_console_get_style(console, &style) == 0);
		geometry = geometry && blink == ST_CONSOLE_BLINK_PRESTEL
		    && cursor.range == 20 && palette[4] == UINT32_C(0x0000ff)
		    && style.foreground == 7 && style.background == 0;
		fprintf(stderr, "Prestel menu mode: %ux%u, expected 40x25\n",
		    (unsigned)columns, (unsigned)rows);
		(void)snprintf(source, sizeof(source),
		    "import \"syncterm_menu\" for Menu\n"
		    "if (!Menu.setScreenMode(%d)) Fiber.abort(\"mode rejected\")\n"
		    "if (Menu.currentScreenMode != %d) Fiber.abort(\"mode identity lost\")\n",
		    ST_SCREEN_MODE_LCD_80X25, ST_SCREEN_MODE_LCD_80X25);
		bool identity = wrenInterpret(host->vm, "mode_identity", source)
		    == WREN_RESULT_SUCCESS;
		passed = geometry && identity;
	}
	else if (strcmp(argv[2], "core-helpers") == 0) {
		static const char *cases[] = {
		    "import \"syncterm\" for WON\n"
		    "if (WON.serialize([1, true, null, \"é\"]) != \"[1,true,null,\\\"é\\\"]\") Fiber.abort(\"serialization control failed\")\n",
		    "import \"syncterm\" for WON\n"
		    "if (WON.deserialize(\"[1,true,null]\")[0] != 1) Fiber.abort(\"parse control failed\")\n",
		    "import \"syncterm\" for Hook\n"
		    "if (Hook.dispatch_(Fn.new { Fiber.abort(42) }) != null) Fiber.abort(\"hook abort did not pass through\")\n",
		    "var map = {\"key\": 9}\n"
		    "for (entry in map) {\n"
		    "  if (entry.key != \"key\" || entry.value != 9) Fiber.abort(\"Map entry\")\n"
		    "}\n"
		    "if (map.keys.toList[0] != \"key\") Fiber.abort(\"Map keys\")\n",
		    "var list = [9]\n"
		    "if (list.remove(9) != 9 || list.remove(9) != null) Fiber.abort(\"List.remove result\")\n",
		    "var inclusive = (5..0).toList\n"
		    "var exclusive = (5...0).toList\n"
		    "if (inclusive.count != 6 || inclusive[0] != 5 || inclusive[5] != 0) Fiber.abort(\"inclusive reverse range\")\n"
		    "if (exclusive.count != 5 || exclusive[0] != 5 || exclusive[4] != 1) Fiber.abort(\"exclusive reverse range\")\n",
		    "if (Object.supertype != null) Fiber.abort(\"Object supertype\")\n",
		    "class UnsupportedProbe { foreign static call() }\n"
		    "var unsupported = Fiber.new { UnsupportedProbe.call() }\n"
		    "unsupported.try()\n"
		    "if (!(unsupported.error is String) || !unsupported.error.contains(\"not available\")) Fiber.abort(\"unsupported method fallback\")\n",
		};
		CHECK(status == 0);
		passed = true;
		for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
			char module[64];
			(void)snprintf(module, sizeof(module), "core_helper_%zu", i);
			bool ok = wrenInterpret(host->vm, module, cases[i])
			    == WREN_RESULT_SUCCESS;
			fprintf(stderr, "core helper %zu: %s\n", i, ok ? "PASS" : "FAIL");
			passed = passed && ok;
		}
	}
	else if (strcmp(argv[2], "cell-colours") == 0) {
		CHECK(status == 0);
		passed = wrenInterpret(host->vm, "cell_colours_audit",
		    "import \"syncterm\" for Cell\n"
		    "var cell = Cell.new()\n"
		    "cell.legacyAttr = 112\n"
		    "cell.fgRgb = 0\n"
		    "cell.bgRgb = 0xaaaaaa\n"
		    "if (cell.legacyAttr != 112) Fiber.abort(\"RGB setters lost legacy attribute: %(cell.legacyAttr)\")\n")
		    == WREN_RESULT_SUCCESS;
		fprintf(stderr, "RGB colors preserve legacy attribute: %s\n",
		    passed ? "PASS" : "FAIL");
	}
	else if (strcmp(argv[2], "settings-save-failure") == 0) {
		CHECK(status == 0);
		/* No settings store is loaded: persistence must fail without I/O. */
		passed = wrenInterpret(host->vm, "settings_save_failure_audit",
		    "import \"syncterm_menu\" for Menu\n"
		    "var snapshot = Menu.settings\n"
		    "var before = snapshot.confirmClose\n"
		    "snapshot.confirmClose = !before\n"
		    "if (snapshot.save()) Fiber.abort(\"expected persistence failure\")\n"
		    "if (!snapshot.dirty) Fiber.abort(\"failed save cleaned snapshot\")\n"
		    "if (Menu.settings.confirmClose != before) Fiber.abort(\"failed save applied snapshot\")\n")
		    == WREN_RESULT_SUCCESS;
		fprintf(stderr, "failed settings save preserves running values: %s\n",
		    passed ? "PASS" : "FAIL");
	}
	else if (strcmp(argv[2], "settings-reload") == 0) {
		CHECK(status == 0);
		passed = wrenInterpret(host->vm, "settings_reload_audit",
		    "import \"syncterm_menu\" for Menu\n"
		    "var applied = Menu.settings\n"
		    "applied.confirmClose = !applied.confirmClose\n"
		    "if (!applied.apply()) Fiber.abort(\"apply failed\")\n"
		    "var snapshot = Menu.settings\n"
		    "var before = snapshot.confirmClose\n"
		    "snapshot.confirmClose = !before\n"
		    "snapshot.reload()\n"
		    "if (snapshot.confirmClose != before || snapshot.dirty) Fiber.abort(\"reload did not restore running values\")\n")
		    == WREN_RESULT_SUCCESS;
		fprintf(stderr, "settings reload reads running values without a disk store: %s\n",
		    passed ? "PASS" : "FAIL");
	}
	else if (strcmp(argv[2], "settings-stale") == 0) {
		CHECK(status == 0);
		passed = wrenInterpret(host->vm, "settings_stale_audit",
		    "import \"syncterm_menu\" for Menu\n"
		    "var old = Menu.settings\n"
		    "var current = Menu.settings\n"
		    "current.confirmClose = !current.confirmClose\n"
		    "if (!current.apply()) Fiber.abort(\"settings apply failed\")\n"
		    "var stale = Fiber.new { old.confirmClose }.try()\n"
		    "if (!(stale is String)) Fiber.abort(\"old settings handle remained usable\")\n")
		    == WREN_RESULT_SUCCESS;
		fprintf(stderr, "settings apply invalidates other snapshots: %s\n",
		    passed ? "PASS" : "FAIL");
	}
	else {
		static const struct {
			const char *name, *module, *class_name;
		} suites[] = {
			{"draw-suite", "ui_draw_test", "UiDrawTest"},
			{"input-suite", "ui_input_test", "UiInputTest"},
			{"logview-suite", "ui_logview_test", "UiLogviewTest"},
			{"popup-suite", "ui_popup_test", "UiPopupTest"},
			{"help-suite", "ui_help_test", "UiHelpTest"},
			{"progress-suite", "ui_progress_test", "UiProgressTest"},
			{"menu-suite", "menu_ui_test", "MenuUiTest"},
			{"wren-console-suite", "wren_console_test", "WrenConsoleTest"},
		};
		static char source[128 * 1024];
		CHECK(status == 0);
		for (size_t i = 0; i < sizeof(suites) / sizeof(suites[0]); ++i) {
			if (strcmp(argv[2], suites[i].name) != 0) continue;
			char path[128], runner[256];
			CHECK(snprintf(path, sizeof(path), "scripts/%s.wren", suites[i].module) < (int)sizeof(path));
			FILE *stream = fopen(path, "rb");
			CHECK(stream != NULL);
			size_t count = fread(source, 1, sizeof(source) - 1, stream);
			CHECK(!ferror(stream) && feof(stream));
			CHECK(fclose(stream) == 0);
			source[count] = '\0';
			CHECK(wrenInterpret(host->vm, suites[i].module, source) == WREN_RESULT_SUCCESS);
			CHECK(snprintf(runner, sizeof(runner),
			    "import \"%s\" for %s\nvar result = %s.runSyntheticFontAudit()\n",
			    suites[i].module, suites[i].class_name, suites[i].class_name) < (int)sizeof(runner));
			CHECK(wrenInterpret(host->vm, "synthetic_font_audit", runner) == WREN_RESULT_SUCCESS);
			wrenEnsureSlots(host->vm, 2);
			wrenGetVariable(host->vm, "synthetic_font_audit", "result", 0);
			CHECK(wrenGetSlotType(host->vm, 0) == WREN_TYPE_LIST);
			wrenGetListElement(host->vm, 0, 0, 1);
			CHECK(wrenGetSlotDouble(host->vm, 1) > 0);
			wrenGetListElement(host->vm, 0, 1, 1);
			passed = wrenGetSlotDouble(host->vm, 1) == 0;
			for (size_t entry = 0; entry < host->menu.log.count; ++entry) {
				uint64_t seq = host->menu.log.total - host->menu.log.count + entry;
				const char *message = host->menu.log.entries[seq % ST_WREN_LOG_CAPACITY].text;
				if (message != NULL && strstr(message, "FAIL") != NULL)
					fprintf(stderr, "%s\n", message);
			}
		}
	}

	st_wren_host_destroy(host);
	st_console_destroy(console);
	st_presentation_font_destroy(&presentation_font);
	st_hyperlink_registry_destroy(hyperlinks);
	return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}

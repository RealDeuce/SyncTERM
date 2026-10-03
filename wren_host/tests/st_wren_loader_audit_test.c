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

static void
write_embedded_override(const char *path, const char *name,
	const char *addition)
{
	FILE *stream = fopen(path, "wb");
	const struct st_embedded_script *found = NULL;

	CHECK(stream != NULL);
	for (const struct st_embedded_script *script = st_wren_embedded_scripts;
	    script->name != NULL; ++script) {
		if (script->event != NULL && strcmp(script->name, name) == 0) {
			found = script;
			break;
		}
	}
	CHECK(found != NULL);
	CHECK(fputs(found->source, stream) >= 0);
	CHECK(fputc('\n', stream) != EOF);
	CHECK(fputs(addition, stream) >= 0);
	CHECK(fclose(stream) == 0);
}

static size_t
log_count(const struct st_wren_log *log, const char *text)
{
	size_t count = 0;

	for (uint64_t sequence = log->total - log->count;
	    sequence < log->total; ++sequence) {
		const struct st_wren_log_entry *entry =
		    &log->entries[sequence % ST_WREN_LOG_CAPACITY];

		if (entry->text != NULL && strcmp(entry->text, text) == 0)
			++count;
	}
	return count;
}

static uint64_t
log_position(const struct st_wren_log *log, const char *text)
{
	for (uint64_t sequence = log->total - log->count;
	    sequence < log->total; ++sequence) {
		const struct st_wren_log_entry *entry =
		    &log->entries[sequence % ST_WREN_LOG_CAPACITY];

		if (entry->text != NULL && strcmp(entry->text, text) == 0)
			return sequence;
	}
	return UINT64_MAX;
}

static void
post_text(st_console_t *console, uint32_t codepoint)
{
	const struct st_console_event event = {
		.type = ST_CONSOLE_EVENT_TEXT,
		.data.text.codepoint = codepoint,
	};

	CHECK(st_console_post_event(console, &event) == 0);
}

static void
post_key(st_console_t *console, uint16_t code)
{
	const struct st_console_event event = {
		.type = ST_CONSOLE_EVENT_KEY_CODE,
		.data.key.code = code,
	};

	CHECK(st_console_post_event(console, &event) == 0);
}

static void
post_wren_console_events(st_console_t *console)
{
	static const uint32_t first[] = {
		'/', 'e', 'd', 'i', 't', ' ', 'A', UINT32_C(0x00e9),
		UINT32_C(0x4e00), 'Z',
	};
	static const uint32_t second[] = {
		'/', 'e', 'd', 'i', 't', ' ', 'k', 'e', 'e', 'p', ' ',
		UINT32_C(0x00e9), 'w', 'o', 'r', 'd',
	};

	for (size_t i = 0; i < sizeof(first) / sizeof(first[0]); ++i)
		post_text(console, first[i]);
	post_key(console, ST_CONSOLE_KEY_LEFT);
	post_key(console, '\b');
	post_key(console, ST_CONSOLE_KEY_LEFT);
	post_key(console, ST_CONSOLE_KEY_RIGHT);
	post_key(console, ST_CONSOLE_KEY_LEFT);
	post_key(console, ST_CONSOLE_KEY_DELETE);
	post_text(console, UINT32_C(0x03a9));
	post_key(console, '\r');
	for (size_t i = 0; i < sizeof(second) / sizeof(second[0]); ++i)
		post_text(console, second[i]);
	post_key(console, UINT16_C(0x0017));
	post_text(console, 'd');
	post_text(console, 'o');
	post_text(console, 'n');
	post_text(console, 'e');
	post_key(console, '\r');
	post_key(console, 27);
}

int
main(int argc, char **argv)
{
	char menu[1024], picker[1024], script[1100];
	char personal[1024], cached[1024];
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
	bool recovery;
	bool passed = false;
	int status;

	/* argv[1] must be a fresh, caller-owned temporary directory. */
	CHECK(argc == 3);
	CHECK(strcmp(argv[2], "autoload") == 0
	    || strcmp(argv[2], "recovery") == 0
	    || strcmp(argv[2], "menu-error") == 0
	    || strcmp(argv[2], "script-catalog") == 0
	    || strcmp(argv[2], "physical-input") == 0
	    || strcmp(argv[2], "settings-stale") == 0
	    || strcmp(argv[2], "settings-save-failure") == 0
	    || strcmp(argv[2], "settings-reload") == 0
	    || strcmp(argv[2], "draw-suite") == 0
	    || strcmp(argv[2], "widget-suite") == 0
	    || strcmp(argv[2], "widget-tail") == 0
	    || strcmp(argv[2], "list-suite") == 0
	    || strcmp(argv[2], "style-suite") == 0
	    || strcmp(argv[2], "button-suite") == 0
	    || strcmp(argv[2], "checkbox-suite") == 0
	    || strcmp(argv[2], "radio-suite") == 0
	    || strcmp(argv[2], "spinbox-suite") == 0
	    || strcmp(argv[2], "progress-suite") == 0
	    || strcmp(argv[2], "popup-suite") == 0
	    || strcmp(argv[2], "input-suite") == 0
	    || strcmp(argv[2], "form-suite") == 0
	    || strcmp(argv[2], "menubar-suite") == 0
	    || strcmp(argv[2], "date-suite") == 0
	    || strcmp(argv[2], "help-suite") == 0
	    || strcmp(argv[2], "color-suite") == 0
	    || strcmp(argv[2], "palette-suite") == 0
	    || strcmp(argv[2], "menu-suite") == 0
	    || strcmp(argv[2], "main-menu-address") == 0
	    || strcmp(argv[2], "logview-suite") == 0
	    || strcmp(argv[2], "wren-console-suite") == 0
	    || strcmp(argv[2], "auxiliary-scripts") == 0
	    || strcmp(argv[2], "core-helpers") == 0
	    || strcmp(argv[2], "screen-mode") == 0
	    || strcmp(argv[2], "bbs-setters") == 0
	    || strcmp(argv[2], "statusbar-suite") == 0
	    || strcmp(argv[2], "cell-colours") == 0);
	recovery = strcmp(argv[2], "recovery") == 0;
	CHECK(strlen(argv[1]) < 900);
	(void)snprintf(menu, sizeof(menu), "%s/auto/menu", argv[1]);
	(void)snprintf(picker, sizeof(picker), "%s/auto/picker", argv[1]);
	CHECK(fs_create_directories(menu) == 0);
	CHECK(fs_create_directories(picker) == 0);
	if (recovery) {
		(void)snprintf(script, sizeof(script), "%s/file_picker.wren", picker);
		write_script(script, "class Broken {\n");
	}
	else if (strcmp(argv[2], "menu-error") == 0) {
		(void)snprintf(script, sizeof(script), "%s/broken_addon.wren", menu);
		write_script(script, "class Broken {\n");
	}
	else if (strcmp(argv[2], "autoload") == 0) {
		(void)snprintf(script, sizeof(script), "%s/main_menu.wren", argv[1]);
		write_script(script, "class Broken {\n");
		(void)snprintf(script, sizeof(script), "%s/file_picker.wren", argv[1]);
		write_script(script, "class Broken {\n");
		(void)snprintf(script, sizeof(script), "%s/a_importer.wren", menu);
		write_script(script, "import \"z_imported\"\n"
		    "System.print(\"menu-importer\")\n");
		(void)snprintf(script, sizeof(script), "%s/z_imported.wren", menu);
		write_script(script, "System.print(\"menu-imported\")\n");
		(void)snprintf(script, sizeof(script), "%s/main_menu.wren", menu);
		write_embedded_override(script, "main_menu",
		    "System.print(\"menu-override\")\n");
		(void)snprintf(script, sizeof(script), "%s/a_importer.wren", picker);
		write_script(script, "import \"z_imported\"\n"
		    "System.print(\"picker-importer\")\n");
		(void)snprintf(script, sizeof(script), "%s/z_imported.wren", picker);
		write_script(script, "System.print(\"picker-imported\")\n");
		(void)snprintf(script, sizeof(script), "%s/file_picker.wren", picker);
		write_embedded_override(script, "file_picker",
		    "System.print(\"picker-override\")\n");
	}
	else if (strcmp(argv[2], "script-catalog") == 0) {
		for (size_t i = 0;
		    i < sizeof(catalog_files) / sizeof(catalog_files[0]); ++i) {
			(void)snprintf(script, sizeof(script), "%s/%s", argv[1],
			    catalog_files[i]);
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
	if (strcmp(argv[2], "bbs-setters") == 0) {
		(void)snprintf(personal, sizeof(personal), "%s/personal.ini", argv[1]);
		(void)snprintf(cached, sizeof(cached), "%s/cached.ini", argv[1]);
		write_script(personal, "[Audit]\nAddress=example.invalid\n"
		    "Palette=1,2\nWrenScripts=original\n");
		paths.personal_list = personal;
		paths.cached_system_list = cached;
	}
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
		.application_title = "Loader audit", .safe_mode = true,
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
	if (recovery) {
		const struct st_console_event escape = {
			.type = ST_CONSOLE_EVENT_KEY_CODE,
			.data.key.code = 27,
		};
		struct st_wren_picker_call call = {
			.mode = ST_WREN_PICKER_FILE,
			.title = "Recovery test",
		};

		CHECK(status == 0);
		CHECK(((struct st_wren_host *)host)->picker_class == NULL);
		CHECK(st_console_post_event(console, &escape) == 0);
		passed = st_wren_picker_host_run((struct st_wren_host *)host, &call)
		    && call.completed && call.path_count == 0;
		st_wren_picker_call_dispose(&call);
		fprintf(stderr, "invalid picker implementation reaches recovery: %s\n",
		    passed ? "PASS" : "FAIL");
	}
	else if (strcmp(argv[2], "menu-error") == 0) {
		passed = status == IO_ERROR_INVALID_FORMAT && host == NULL;
		fprintf(stderr, "invalid menu add-on rejects menu startup: %s\n",
		    passed ? "PASS" : "FAIL");
	}
	else if (strcmp(argv[2], "script-catalog") == 0) {
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
	else if (strcmp(argv[2], "bbs-setters") == 0) {
		static const char *cases[] = {
		    "import \"syncterm_menu\" for Menu\n"
		    "if (Menu.load(null) != 0) Fiber.abort(\"fixture load failed\")\n"
		    "var entry = Menu.entries[0]\n"
		    "var bad = Fiber.new { entry.palette = [99, \"bad\"] }\n"
		    "bad.try()\n"
		    "if (bad.error == null) Fiber.abort(\"invalid palette accepted\")\n"
		    "if (entry.palette.count != 2 || entry.palette[0] != 1 || "
		    "entry.palette[1] != 2 || entry.dirty) "
		    "Fiber.abort(\"rejected palette changed entry\")\n",
		    "import \"syncterm_menu\" for Menu\n"
		    "var entry = Menu.entries[0]\n"
		    "var bad = Fiber.new { entry.wrenScripts = [\"x\", false] }\n"
		    "bad.try()\n"
		    "if (bad.error == null) Fiber.abort(\"invalid scripts accepted\")\n"
		    "if (entry.wrenScripts.count != 1 || "
		    "entry.wrenScripts[0] != \"original\" || entry.dirty) "
		    "Fiber.abort(\"rejected scripts changed entry\")\n",
		    "import \"syncterm_menu\" for Menu\n"
		    "var entry = Menu.entries[0]\n"
		    "var bad = Fiber.new { entry.palette = "
		    "[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16] }\n"
		    "bad.try()\n"
		    "if (bad.error == null || entry.palette.count != 2 || "
		    "entry.palette[0] != 1 || entry.palette[1] != 2 || entry.dirty) "
		    "Fiber.abort(\"oversized palette changed entry\")\n",
		    "import \"syncterm_menu\" for Menu\n"
		    "var entry = Menu.entries[0]\n"
		    "var huge = \"a\"\n"
		    "while (huge.bytes.count <= Menu.wrenScriptsMaxLength) huge = huge + \"a\"\n"
		    "var bad = Fiber.new { entry.wrenScripts = [huge] }\n"
		    "bad.try()\n"
		    "if (bad.error == null || entry.wrenScripts.count != 1 || "
		    "entry.wrenScripts[0] != \"original\" || entry.dirty) "
		    "Fiber.abort(\"oversized scripts changed entry\")\n",
		};
		CHECK(status == 0);
		passed = true;
		for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
			char module[64];
			(void)snprintf(module, sizeof(module), "bbs_setter_%zu", i);
			bool ok = wrenInterpret(host->vm, module, cases[i])
			    == WREN_RESULT_SUCCESS;
			fprintf(stderr, "BBS setter %zu: %s\n", i, ok ? "PASS" : "FAIL");
			passed = passed && ok;
		}
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
	else if (strcmp(argv[2], "main-menu-address") == 0) {
		static char source[128 * 1024];
		FILE *stream = fopen("scripts/main_menu_test.wren", "rb");
		size_t count;

		CHECK(status == 0);
		CHECK(stream != NULL);
		count = fread(source, 1, sizeof(source) - 1, stream);
		CHECK(!ferror(stream) && feof(stream));
		CHECK(fclose(stream) == 0);
		source[count] = '\0';
		CHECK(wrenInterpret(host->vm, "main_menu_test", source)
		    == WREN_RESULT_SUCCESS);
		CHECK(wrenInterpret(host->vm, "main_menu_address_suite",
		    "import \"main_menu_test\" for MenuTest\n"
		    "var result = MenuTest.run()\n") == WREN_RESULT_SUCCESS);
		wrenEnsureSlots(host->vm, 2);
		wrenGetVariable(host->vm, "main_menu_address_suite", "result", 0);
		CHECK(wrenGetSlotType(host->vm, 0) == WREN_TYPE_LIST);
		wrenGetListElement(host->vm, 0, 0, 1);
		fprintf(stderr, "main-menu address: %.0f passed; ",
		    wrenGetSlotDouble(host->vm, 1));
		wrenGetListElement(host->vm, 0, 1, 1);
		fprintf(stderr, "%.0f failed\n", wrenGetSlotDouble(host->vm, 1));
		passed = wrenGetSlotDouble(host->vm, 1) == 0;
	}
	else if (strcmp(argv[2], "wren-console-suite") == 0) {
		static char source[128 * 1024];
		FILE *stream = fopen("scripts/wren_console_test.wren", "rb");
		size_t count;

		CHECK(status == 0);
		CHECK(stream != NULL);
		count = fread(source, 1, sizeof(source) - 1, stream);
		CHECK(!ferror(stream) && feof(stream));
		CHECK(fclose(stream) == 0);
		source[count] = '\0';
		CHECK(wrenInterpret(host->vm, "wren_console_test", source)
		    == WREN_RESULT_SUCCESS);
		post_wren_console_events(console);
		CHECK(wrenInterpret(host->vm, "wren_console_suite",
		    "import \"wren_console_test\" for WrenConsoleTest\n"
		    "var result = WrenConsoleTest.run()\n")
		    == WREN_RESULT_SUCCESS);
		wrenEnsureSlots(host->vm, 2);
		wrenGetVariable(host->vm, "wren_console_suite", "result", 0);
		CHECK(wrenGetSlotType(host->vm, 0) == WREN_TYPE_LIST);
		wrenGetListElement(host->vm, 0, 0, 1);
		fprintf(stderr, "wren console: %.0f passed; ",
		    wrenGetSlotDouble(host->vm, 1));
		wrenGetListElement(host->vm, 0, 1, 1);
		fprintf(stderr, "%.0f failed\n", wrenGetSlotDouble(host->vm, 1));
		passed = wrenGetSlotDouble(host->vm, 1) == 0;
		for (size_t i = 0; i < host->menu.log.count; ++i) {
			uint64_t sequence = host->menu.log.total - host->menu.log.count + i;
			const char *message = host->menu.log.entries[
			    sequence % ST_WREN_LOG_CAPACITY].text;

			if (message != NULL && strstr(message, "FAIL") != NULL)
				fprintf(stderr, "%s\n", message);
		}
	}
	else if (strcmp(argv[2], "auxiliary-scripts") == 0) {
		static const char *modules[] = {
			"menu_host_ui_test", "pixel_vm_test",
			"menu_theme_editor_test",
			"menu_bbs_editor_test",
			"main_menu_test",
		};
		static char source[128 * 1024];

		CHECK(status == 0);
		passed = true;
		for (size_t i = 0; i < sizeof(modules) / sizeof(modules[0]); ++i) {
			char path[128];
			char invocation[256];
			(void)snprintf(path, sizeof(path), "scripts/%s.wren", modules[i]);
			FILE *stream = fopen(path, "rb");

			CHECK(stream != NULL);
			size_t count = fread(source, 1, sizeof(source) - 1, stream);
			CHECK(!ferror(stream) && feof(stream));
			CHECK(fclose(stream) == 0);
			source[count] = '\0';
			CHECK(wrenInterpret(host->vm, modules[i], source) == WREN_RESULT_SUCCESS);
			(void)snprintf(invocation, sizeof(invocation),
			    "import \"%s\" for MenuTest\nvar result = MenuTest.%s()\n",
			    modules[i], strcmp(modules[i], "menu_bbs_editor_test") == 0
			    ? "runHelpers" : "run");
			if (wrenInterpret(host->vm, path, invocation) != WREN_RESULT_SUCCESS) {
				fprintf(stderr, "%s: FAIL (runtime abort)\n", modules[i]);
				passed = false;
				continue;
			}
			wrenEnsureSlots(host->vm, 2);
			wrenGetVariable(host->vm, path, "result", 0);
			wrenGetListElement(host->vm, 0, 0, 1);
			fprintf(stderr, "%s: %.0f passed; ", modules[i], wrenGetSlotDouble(host->vm, 1));
			wrenGetListElement(host->vm, 0, 1, 1);
			fprintf(stderr, "%.0f failed\n", wrenGetSlotDouble(host->vm, 1));
			if (wrenGetSlotDouble(host->vm, 1) != 0)
				passed = false;
		}
		FILE *demo = fopen("scripts/ui_demo.wren", "rb");
		CHECK(demo != NULL);
		size_t demo_count = fread(source, 1, sizeof(source) - 1, demo);
		CHECK(!ferror(demo) && feof(demo));
		CHECK(fclose(demo) == 0);
		source[demo_count] = '\0';
		bool demo_loaded = wrenInterpret(host->vm, "ui_demo", source)
		    == WREN_RESULT_SUCCESS;
		fprintf(stderr, "shipped ui_demo import: %s\n", demo_loaded ? "PASS" : "FAIL");
		passed = passed && demo_loaded;
		for (size_t i = 0; i < host->menu.log.count; ++i) {
			uint64_t sequence = host->menu.log.total - host->menu.log.count + i;
			const char *message = host->menu.log.entries[
			    sequence % ST_WREN_LOG_CAPACITY].text;

			if (message != NULL && strstr(message, "FAIL") != NULL)
				fprintf(stderr, "%s\n", message);
		}
	}
	else if (strcmp(argv[2], "widget-suite") == 0
	    || strcmp(argv[2], "widget-tail") == 0
	    || strcmp(argv[2], "draw-suite") == 0
	    || strcmp(argv[2], "list-suite") == 0
	    || strcmp(argv[2], "style-suite") == 0
	    || strcmp(argv[2], "button-suite") == 0
	    || strcmp(argv[2], "checkbox-suite") == 0
	    || strcmp(argv[2], "radio-suite") == 0
	    || strcmp(argv[2], "spinbox-suite") == 0
	    || strcmp(argv[2], "progress-suite") == 0
	    || strcmp(argv[2], "popup-suite") == 0
	    || strcmp(argv[2], "input-suite") == 0
	    || strcmp(argv[2], "form-suite") == 0
	    || strcmp(argv[2], "menubar-suite") == 0
	    || strcmp(argv[2], "date-suite") == 0
	    || strcmp(argv[2], "help-suite") == 0
	    || strcmp(argv[2], "color-suite") == 0
	    || strcmp(argv[2], "palette-suite") == 0
	    || strcmp(argv[2], "menu-suite") == 0
	    || strcmp(argv[2], "logview-suite") == 0
	    || strcmp(argv[2], "statusbar-suite") == 0) {
		static char source[128 * 1024];
		bool list_suite = strcmp(argv[2], "list-suite") == 0;
		bool draw_suite = strcmp(argv[2], "draw-suite") == 0;
		bool style_suite = strcmp(argv[2], "style-suite") == 0;
		bool button_suite = strcmp(argv[2], "button-suite") == 0;
		bool checkbox_suite = strcmp(argv[2], "checkbox-suite") == 0;
		bool radio_suite = strcmp(argv[2], "radio-suite") == 0;
		bool spinbox_suite = strcmp(argv[2], "spinbox-suite") == 0;
		bool progress_suite = strcmp(argv[2], "progress-suite") == 0;
		bool popup_suite = strcmp(argv[2], "popup-suite") == 0;
		bool input_suite = strcmp(argv[2], "input-suite") == 0;
		bool form_suite = strcmp(argv[2], "form-suite") == 0;
		bool menubar_suite = strcmp(argv[2], "menubar-suite") == 0;
		bool date_suite = strcmp(argv[2], "date-suite") == 0;
		bool help_suite = strcmp(argv[2], "help-suite") == 0;
		bool color_suite = strcmp(argv[2], "color-suite") == 0;
		bool palette_suite = strcmp(argv[2], "palette-suite") == 0;
		bool menu_suite = strcmp(argv[2], "menu-suite") == 0;
		bool logview_suite = strcmp(argv[2], "logview-suite") == 0;
		bool statusbar_suite = strcmp(argv[2], "statusbar-suite") == 0;
		FILE *stream = fopen(draw_suite ? "scripts/ui_draw_test.wren"
		    : radio_suite ? "scripts/ui_radio_test.wren"
		    : logview_suite ? "scripts/ui_logview_test.wren"
		    : menu_suite ? "scripts/menu_ui_test.wren"
		    : palette_suite ? "scripts/menu_palette_picker_test.wren"
		    : color_suite ? "scripts/ui_color_picker_test.wren"
		    : help_suite ? "scripts/ui_help_test.wren"
		    : date_suite ? "scripts/ui_date_picker_test.wren"
		    : menubar_suite ? "scripts/ui_menubar_test.wren"
		    : form_suite ? "scripts/ui_form_test.wren"
		    : input_suite ? "scripts/ui_input_test.wren"
		    : popup_suite ? "scripts/ui_popup_test.wren"
		    : progress_suite ? "scripts/ui_progress_test.wren"
		    : statusbar_suite ? "scripts/ui_statusbar_test.wren"
		    : spinbox_suite ? "scripts/ui_spinbox_test.wren"
		    : button_suite ? "scripts/ui_button_test.wren"
		    : checkbox_suite ? "scripts/ui_checkbox_test.wren"
		    : style_suite ? "scripts/ui_style_test.wren"
		    : list_suite ? "scripts/ui_list_test.wren"
		    : "scripts/ui_widget_test.wren", "rb");
		size_t count;

		CHECK(status == 0);
		CHECK(stream != NULL);
		count = fread(source, 1, sizeof(source) - 1, stream);
		CHECK(!ferror(stream) && feof(stream));
		CHECK(fclose(stream) == 0);
		source[count] = '\0';
		CHECK(wrenInterpret(host->vm, draw_suite ? "ui_draw_test"
		    : radio_suite ? "ui_radio_test"
		    : logview_suite ? "ui_logview_test"
		    : menu_suite ? "menu_ui_test"
		    : palette_suite ? "menu_palette_picker_test"
		    : color_suite ? "ui_color_picker_test"
		    : help_suite ? "ui_help_test"
		    : date_suite ? "ui_date_picker_test"
		    : menubar_suite ? "ui_menubar_test"
		    : form_suite ? "ui_form_test"
		    : input_suite ? "ui_input_test"
		    : popup_suite ? "ui_popup_test"
		    : progress_suite ? "ui_progress_test"
		    : statusbar_suite ? "ui_statusbar_test"
		    : spinbox_suite ? "ui_spinbox_test"
		    : button_suite ? "ui_button_test"
		    : checkbox_suite ? "ui_checkbox_test"
		    : style_suite ? "ui_style_test"
		    : list_suite ? "ui_list_test"
		    : "ui_widget_test", source)
		    == WREN_RESULT_SUCCESS);
		CHECK(wrenInterpret(host->vm, "widget_suite_audit",
		    logview_suite ?
		    "import \"ui_logview_test\" for UiLogviewTest\n"
		    "var result = UiLogviewTest.run()\n" :
		    menu_suite ?
		    "import \"menu_ui_test\" for MenuUiTest\n"
		    "var result = MenuUiTest.run()\n" :
		    palette_suite ?
		    "import \"menu_palette_picker_test\" for MenuPalettePickerTest\n"
		    "var result = MenuPalettePickerTest.runAudit()\n" :
		    color_suite ?
		    "import \"ui_color_picker_test\" for UiColorPickerTest\n"
		    "var result = UiColorPickerTest.run()\n" :
		    draw_suite ?
		    "import \"ui_draw_test\" for UiDrawTest\n"
		    "var result = UiDrawTest.run()\n" :
		    help_suite ?
		    "import \"ui_help_test\" for UiHelpTest\n"
		    "var result = UiHelpTest.run()\n" :
		    date_suite ?
		    "import \"ui_date_picker_test\" for UiDatePickerTest\n"
		    "var result = UiDatePickerTest.run()\n" :
		    menubar_suite ?
		    "import \"ui_menubar_test\" for UiMenubarTest\n"
		    "var result = UiMenubarTest.run()\n" :
		    form_suite ?
		    "import \"ui_form_test\" for UiFormTest\n"
		    "var result = UiFormTest.run()\n" :
		    input_suite ?
		    "import \"ui_input_test\" for UiInputTest\n"
		    "var result = UiInputTest.run()\n" :
		    popup_suite ?
		    "import \"ui_popup_test\" for UiPopupTest\n"
		    "var result = UiPopupTest.run()\n" :
		    progress_suite ?
		    "import \"ui_progress_test\" for UiProgressTest\n"
		    "var result = UiProgressTest.run()\n" :
		    statusbar_suite ?
		    "import \"ui_statusbar_test\" for UiStatusbarTest\n"
		    "var result = UiStatusbarTest.run()\n" :
		    radio_suite ?
		    "import \"ui_radio_test\" for UiRadioTest\n"
		    "var result = UiRadioTest.run()\n" :
		    spinbox_suite ?
		    "import \"ui_spinbox_test\" for UiSpinboxTest\n"
		    "var result = UiSpinboxTest.run()\n" :
		    button_suite ?
		    "import \"ui_button_test\" for UiButtonTest\n"
		    "var result = UiButtonTest.run()\n" :
		    checkbox_suite ?
		    "import \"ui_checkbox_test\" for UiCheckboxTest\n"
		    "var result = UiCheckboxTest.run()\n" :
		    style_suite ?
		    "import \"ui_style_test\" for UiStyleTest\n"
		    "var result = UiStyleTest.run()\n" :
		    list_suite ?
		    "import \"ui_list_test\" for UiListTest\n"
		    "var result = UiListTest.run()\n" :
		    strcmp(argv[2], "widget-tail") == 0 ?
		    "import \"ui_widget_test\" for UiWidgetTest\n"
		    "var result = UiWidgetTest.runAfterCursorAudit()\n" :
		    "import \"ui_widget_test\" for UiWidgetTest\n"
		    "var result = UiWidgetTest.run()\n") == WREN_RESULT_SUCCESS);
		wrenEnsureSlots(host->vm, 2);
		wrenGetVariable(host->vm, "widget_suite_audit", "result", 0);
		CHECK(wrenGetSlotType(host->vm, 0) == WREN_TYPE_LIST);
		wrenGetListElement(host->vm, 0, 0, 1);
		fprintf(stderr, "%s: %.0f passed; ", argv[2], wrenGetSlotDouble(host->vm, 1));
		wrenGetListElement(host->vm, 0, 1, 1);
		fprintf(stderr, "%.0f failed\n", wrenGetSlotDouble(host->vm, 1));
		passed = wrenGetSlotDouble(host->vm, 1) == 0;
		for (size_t i = 0; i < host->menu.log.count; ++i) {
			uint64_t sequence = host->menu.log.total - host->menu.log.count + i;
			const char *message = host->menu.log.entries[
			    sequence % ST_WREN_LOG_CAPACITY].text;

			if (message != NULL && strstr(message, "FAIL") != NULL)
				fprintf(stderr, "%s\n", message);
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
	else if (strcmp(argv[2], "physical-input") == 0) {
		CHECK(status == 0);
		passed = true;
		for (size_t i = 0; i < 2; ++i) {
			const struct st_console_event event = {
				.type = ST_CONSOLE_EVENT_PHYSICAL_KEY,
				.data.physical_key = {.evdev = 30, .pressed = i == 0},
			};
			char source[512], module[64];
			CHECK(st_console_post_event(console, &event) == 0);
			(void)snprintf(module, sizeof(module), "physical_input_%zu", i);
			(void)snprintf(source, sizeof(source),
			    "import \"syncterm\" for Input, PhysicalKeyEvent\n"
			    "var ev = Input.poll()\n"
			    "if (!(ev is PhysicalKeyEvent)) Fiber.abort(\"physical edge missing\")\n"
			    "if (ev.evdev != 30 || ev.pressed != %s) Fiber.abort(\"physical edge changed\")\n",
			    i == 0 ? "true" : "false");
			bool ok = wrenInterpret(host->vm, module, source)
			    == WREN_RESULT_SUCCESS;
			fprintf(stderr, "physical %s: %s\n", i == 0 ? "press" : "release",
			    ok ? "PASS" : "FAIL");
			passed = passed && ok;
		}
	}
	else if (strcmp(argv[2], "autoload") == 0) {
		struct st_wren_host *internal = host;
		static const char *menu_messages[] = {
			"menu-imported", "menu-importer", "menu-override",
		};
		static const char *picker_messages[] = {
			"picker-imported", "picker-importer", "picker-override",
		};

		CHECK(status == 0);
		passed = wrenHasModule(internal->vm, "a_importer")
		    && wrenHasModule(internal->vm, "z_imported")
		    && wrenHasModule(internal->picker_vm, "a_importer")
		    && wrenHasModule(internal->picker_vm, "z_imported");
		for (size_t i = 0; i < sizeof(menu_messages) / sizeof(menu_messages[0]);
		    ++i)
			passed = passed
			    && log_count(&internal->menu.log, menu_messages[i]) == 1;
		for (size_t i = 0;
		    i < sizeof(picker_messages) / sizeof(picker_messages[0]); ++i)
			passed = passed
			    && log_count(&internal->picker.log, picker_messages[i]) == 1;
		passed = passed
		    && log_position(&internal->menu.log, "menu-imported")
		    < log_position(&internal->menu.log, "menu-importer")
		    && log_position(&internal->menu.log, "menu-importer")
		    < log_position(&internal->menu.log, "menu-override")
		    && log_position(&internal->picker.log, "picker-imported")
		    < log_position(&internal->picker.log, "picker-importer")
		    && log_position(&internal->picker.log, "picker-importer")
		    < log_position(&internal->picker.log, "picker-override");
		fprintf(stderr, "event discovery, override and exactly-once loading: %s\n",
		    passed ? "PASS" : "FAIL");
	}
	st_wren_host_destroy(host);
	st_console_destroy(console);
	st_presentation_font_destroy(&presentation_font);
	st_hyperlink_registry_destroy(hyperlinks);
	return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}

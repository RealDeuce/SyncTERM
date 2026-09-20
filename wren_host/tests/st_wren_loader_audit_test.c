/* Isolated regressions for documented event loading and picker recovery. */
/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "wren_host_internal.h"
#include "filesystem.h"
#include "st_app_paths.h"
#include "st_console.h"

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
	bool recovery;
	bool passed = false;
	int status;

	/* argv[1] must be a fresh, caller-owned temporary directory. */
	CHECK(argc == 3);
	CHECK(strcmp(argv[2], "autoload") == 0
	    || strcmp(argv[2], "recovery") == 0
	    || strcmp(argv[2], "core-contracts") == 0
	    || strcmp(argv[2], "physical-input") == 0
	    || strcmp(argv[2], "cell") == 0
	    || strcmp(argv[2], "reload") == 0
	    || strcmp(argv[2], "settings-stale") == 0
	    || strcmp(argv[2], "settings-save-failure") == 0
	    || strcmp(argv[2], "settings-reload") == 0
	    || strcmp(argv[2], "painter-truncate") == 0
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
	    || strcmp(argv[2], "color-audit") == 0
	    || strcmp(argv[2], "palette-suite") == 0
	    || strcmp(argv[2], "menu-suite") == 0
	    || strcmp(argv[2], "logview-suite") == 0
	    || strcmp(argv[2], "auxiliary-scripts") == 0
	    || strcmp(argv[2], "core-helpers") == 0
	    || strcmp(argv[2], "screen-mode") == 0
	    || strcmp(argv[2], "bbs-setters") == 0
	    || strcmp(argv[2], "statusbar-suite") == 0
	    || strcmp(argv[2], "inherited-theme") == 0
	    || strcmp(argv[2], "cell-colours") == 0
	    || strcmp(argv[2], "safe-save") == 0);
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
	else if (strcmp(argv[2], "autoload") == 0) {
		(void)snprintf(script, sizeof(script), "%s/audit_menu_marker.wren", menu);
		write_script(script, "class AuditMenuMarker {}\n");
		(void)snprintf(script, sizeof(script), "%s/audit_picker_marker.wren", picker);
		write_script(script, "class AuditPickerMarker {}\n");
	}
	paths.scripts_directory = argv[1];
	paths.themes_directory = argv[1];
	paths.cache_directory = argv[1];
	paths.system_cache_directory = argv[1];
	paths.download_directory = argv[1];
	if (strcmp(argv[2], "reload") == 0 || strcmp(argv[2], "safe-save") == 0
	    || strcmp(argv[2], "bbs-setters") == 0) {
		(void)snprintf(personal, sizeof(personal), "%s/personal.ini", argv[1]);
		(void)snprintf(cached, sizeof(cached), "%s/cached.ini", argv[1]);
		write_script(personal, "[Audit]\nAddress=example.invalid\n");
		paths.personal_list = personal;
		paths.cached_system_list = cached;
	}
	CHECK(st_console_create(&console, &display, &config) == 0);
	if (strcmp(argv[2], "painter-truncate") == 0
	    || strcmp(argv[2], "input-suite") == 0) {
		static const uint8_t narrow[16] = {0};
		static const uint8_t wide[32] = {0};
		static const struct st_console_font_mapping mapping[] = {
			{'?', 0}, {UINT32_C(0x4e00), 1},
		};
		const struct st_console_font font = {
			.bitmap = narrow, .bitmap_size = sizeof(narrow),
			.width = 8, .height = 16, .glyph_count = 1,
			.row_stride = 1, .glyph_stride = 16,
			.wide_bitmap = wide, .wide_bitmap_size = sizeof(wide),
			.wide_glyph_count = 1, .wide_row_stride = 2,
			.wide_glyph_stride = 32,
			.mapping = mapping, .mapping_count = 2,
		};
		uint16_t identifier;

		CHECK(st_console_register_static_font(console, &font, &identifier) == 0);
		CHECK(identifier == 0);
	}
	const struct st_wren_host_config host_config = {
		.console = console, .paths = &paths,
		.application_title = "Loader audit", .safe_mode = true,
	};
	status = st_wren_host_create(&host, &host_config);
	if (recovery) {
		passed = status == 0;
		fprintf(stderr, "host survives invalid picker implementation: %s (status %d)\n",
		    passed ? "PASS" : "FAIL", status);
	}
	else if (strcmp(argv[2], "safe-save") == 0) {
		char contents[4096];
		FILE *stream;
		size_t count;

		CHECK(status == 0);
		CHECK(wrenInterpret(host->vm, "safe_save_audit",
		    "import \"syncterm_menu\" for Menu\n"
		    "import \"syncterm\" for Host\n"
		    "if (!Host.safeMode || Menu.load(null) != 0) Fiber.abort(\"invalid safe-mode setup\")\n"
		    "var entry = Menu.entries[0]\n"
		    "entry.comment = \"safe-mode audit edit\"\n"
		    "entry.save()\n") == WREN_RESULT_SUCCESS);
		stream = fopen(personal, "rb");
		CHECK(stream != NULL);
		count = fread(contents, 1, sizeof(contents) - 1, stream);
		CHECK(!ferror(stream));
		CHECK(fclose(stream) == 0);
		contents[count] = '\0';
		passed = strcmp(contents, "[Audit]\nAddress=example.invalid\n") == 0;
		fprintf(stderr, "safe-mode save leaves file unchanged: %s\n", passed ? "PASS" : "FAIL");
	}
	else if (strcmp(argv[2], "reload") == 0) {
		CHECK(status == 0);
		CHECK(wrenInterpret(host->vm, "initial_load_audit",
		    "import \"syncterm_menu\" for Menu\n"
		    "if (Menu.load(null) != 0 || Menu.entries.count != 1) Fiber.abort(\"initial load failed\")\n")
		    == WREN_RESULT_SUCCESS);
		/* A directory cannot be read as a personal list document. */
		paths.personal_list = argv[1];
		passed = wrenInterpret(host->vm, "failed_reload_audit",
		    "import \"syncterm_menu\" for Menu\n"
		    "if (Menu.load(null) == 0) Fiber.abort(\"expected reload failure\")\n"
		    "if (Menu.entries.count != 1) Fiber.abort(\"failed reload lost old entries\")\n")
		    == WREN_RESULT_SUCCESS;
		fprintf(stderr, "failed reload preserves entries: %s\n", passed ? "PASS" : "FAIL");
	}
	else if (strcmp(argv[2], "bbs-setters") == 0) {
		static const char *cases[] = {
		    "import \"syncterm_menu\" for Menu\n"
		    "if (Menu.load(null) != 0) Fiber.abort(\"fixture load failed\")\n"
		    "var entry = Menu.entries[0]\n"
		    "entry.palette = [1, 2]\n"
		    "var bad = Fiber.new { entry.palette = [99, \"bad\"] }\n"
		    "bad.try()\n"
		    "if (bad.error == null) Fiber.abort(\"invalid palette accepted\")\n"
		    "if (entry.palette[0] != 1) Fiber.abort(\"rejected palette changed old value\")\n",
		    "import \"syncterm_menu\" for Menu\n"
		    "var entry = Menu.entries[0]\n"
		    "entry.wrenScripts = [\"original\"]\n"
		    "var bad = Fiber.new { entry.wrenScripts = [\"x\", false] }\n"
		    "bad.try()\n"
		    "if (bad.error == null) Fiber.abort(\"invalid scripts accepted\")\n"
		    "if (entry.wrenScripts[0] != \"original\") Fiber.abort(\"rejected scripts changed old value\")\n",
		    "import \"syncterm_menu\" for Menu\n"
		    "var entry = Menu.entries[0]\n"
		    "if (Menu.load(null) != 0) Fiber.abort(\"reload failed\")\n"
		    "var stale = Fiber.new { entry.addr }\n"
		    "stale.try()\n"
		    "if (stale.error == null) Fiber.abort(\"old BBS handle remained usable after reload\")\n",
		    "import \"syncterm_menu\" for Menu\n"
		    "var entry = Menu.quickConnect(\"raw://example.invalid:23\")\n"
		    "if (entry == null) Fiber.abort(\"fixture URL rejected\")\n"
		    "if (entry.rename(\"Transient\")) Fiber.abort(\"transient rename accepted\")\n",
		    ("import \"syncterm_menu\" for Menu\n"
		    "if (Menu.create(\"Audit\") != null) Fiber.abort(\"duplicate entry accepted\")\n"),
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
		CHECK(status == 0);
		(void)snprintf(source, sizeof(source),
		    "import \"syncterm_menu\" for Menu\n"
		    "if (!Menu.setScreenMode(%d)) Fiber.abort(\"mode rejected\")\n",
		    ST_SCREEN_MODE_PRESTEL);
		CHECK(wrenInterpret(host->vm, "mode_geometry", source)
		    == WREN_RESULT_SUCCESS);
		CHECK(st_console_size(console, &columns, &rows) == 0);
		bool geometry = columns == 40 && rows == 25;
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
		    "for (key in map) {\n"
		    "  if (key != \"key\") Fiber.abort(\"manual says Map iteration yields keys\")\n"
		    "}\n",
		    "var list = [9]\n"
		    "if (list.remove(9) != true) Fiber.abort(\"manual says List.remove returns true\")\n",
		    "if ((5..0).toList.count != 5) Fiber.abort(\"manual reverse-range example omits zero\")\n",
		    "if (Object.supertype != Object) Fiber.abort(\"manual says Object is its own supertype\")\n",
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
	else if (strcmp(argv[2], "auxiliary-scripts") == 0) {
		static const char *modules[] = {
			"menu_host_ui_test", "pixel_vm_test", "wren_console_test",
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
	    || strcmp(argv[2], "color-audit") == 0
	    || strcmp(argv[2], "palette-suite") == 0
	    || strcmp(argv[2], "menu-suite") == 0
	    || strcmp(argv[2], "logview-suite") == 0
	    || strcmp(argv[2], "statusbar-suite") == 0) {
		static char source[128 * 1024];
		bool list_suite = strcmp(argv[2], "list-suite") == 0;
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
		bool color_suite = strcmp(argv[2], "color-suite") == 0
		    || strcmp(argv[2], "color-audit") == 0;
		bool palette_suite = strcmp(argv[2], "palette-suite") == 0;
		bool menu_suite = strcmp(argv[2], "menu-suite") == 0;
		bool logview_suite = strcmp(argv[2], "logview-suite") == 0;
		bool statusbar_suite = strcmp(argv[2], "statusbar-suite") == 0;
		FILE *stream = fopen(radio_suite ? "scripts/ui_radio_test.wren"
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
		CHECK(wrenInterpret(host->vm, radio_suite ? "ui_radio_test"
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
		    strcmp(argv[2], "color-audit") == 0 ?
		    "import \"ui_color_picker_test\" for UiColorPickerTest\n"
		    "var result = UiColorPickerTest.runAudit()\n" :
		    color_suite ?
		    "import \"ui_color_picker_test\" for UiColorPickerTest\n"
		    "var result = UiColorPickerTest.run()\n" :
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

			if (message != NULL && (strstr(message, "FAIL") != NULL
			    || strstr(message, "wrap audit:") != NULL))
				fprintf(stderr, "%s\n", message);
		}
	}
	else if (strcmp(argv[2], "inherited-theme") == 0) {
		CHECK(status == 0);
		passed = wrenInterpret(host->vm, "inherited_theme_audit",
		    "import \"ui_widget\" for Widget, Container, Rect\n"
		    "import \"ui_style\" for Style, Theme\n"
		    "import \"ui_draw\" for Painter\n"
		    "class Painted is Widget {\n"
		    "  construct new() { super() }\n"
		    "  onPaint_() { Painter.text(surface, 0, 0, \"X\", style(\"default\")) }\n"
		    "}\n"
		    "var parent = Container.new()\n"
		    "parent.bounds = Rect.new(1, 1, 1, 1)\n"
		    "var leaf = Painted.new()\n"
		    "leaf.bounds = parent.bounds\n"
		    "parent.add(leaf)\n"
		    "parent.theme = Theme.new({\"default\": Style.new(0, 7, 0x112233, 0)}, {})\n"
		    "if (parent.draw()[0].fgRgb != 0x112233) Fiber.abort(\"initial theme did not paint\")\n"
		    "parent.theme = Theme.new({\"default\": Style.new(0, 7, 0x445566, 0)}, {})\n"
		    "if (parent.draw()[0].fgRgb != 0x445566) Fiber.abort(\"child retained old inherited theme\")\n")
		    == WREN_RESULT_SUCCESS;
		fprintf(stderr, "parent theme change repaints inheriting children: %s\n",
		    passed ? "PASS" : "FAIL");
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
	else if (strcmp(argv[2], "painter-truncate") == 0) {
		CHECK(status == 0);
		passed = wrenInterpret(host->vm, "painter_truncate_audit",
		    "import \"syncterm\" for Surface, Screen\n"
		    "import \"ui_draw\" for Painter\n"
		    "import \"ui_style\" for Style\n"
		    "var surface = Surface.new(3, 1)\n"
		    "surface[0].ch = \"Z\"\n"
		    "surface[0].legacyAttr = 7\n"
		    "var before = surface[0].fgRgb\n"
		    "if (Screen.textWidth(\"\\u4e00\", 0) != 2) Fiber.abort(\"expected wide test glyph\")\n"
		    "Painter.text(surface, 0, 0, \"\\u4e00\", Style.new(0, 31, 0xffffff, 0x0000aa), 1)\n"
		    "if (surface[0].ch != \"Z\" || surface[0].legacyAttr != 7 || surface[0].fgRgb != before) Fiber.abort(\"clipped glyph changed destination style\")\n")
		    == WREN_RESULT_SUCCESS;
		fprintf(stderr, "Painter truncation preserves undrawn cells: %s\n",
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
		    "if (Menu.settings.confirmClose != before) Fiber.abort(\"failed save applied snapshot\")\n")
		    == WREN_RESULT_SUCCESS;
		fprintf(stderr, "failed settings save preserves running values: %s\n",
		    passed ? "PASS" : "FAIL");
	}
	else if (strcmp(argv[2], "settings-reload") == 0) {
		CHECK(status == 0);
		passed = wrenInterpret(host->vm, "settings_reload_audit",
		    "import \"syncterm_menu\" for Menu\n"
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
	else if (strcmp(argv[2], "cell") == 0) {
		CHECK(status == 0);
		passed = wrenInterpret(host->vm, "cell_equality_audit",
		    "import \"syncterm\" for Cell\n"
		    "var c = Cell.new()\n"
		    "if (c.eqContent(null) != false) Fiber.abort(\"non-Cell comparison must be false\")\n")
		    == WREN_RESULT_SUCCESS;
		fprintf(stderr, "non-Cell equality returns false: %s\n", passed ? "PASS" : "FAIL");
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
	else if (strcmp(argv[2], "core-contracts") == 0) {
		static const char *cases[] = {
		    "import \"syncterm\" for Format\n"
		    "if (Format.bytes(-1) != Format.bytes(0)) Fiber.abort(\"negative bytes not clamped\")\n",
		    "import \"syncterm\" for Format\n"
		    "if (Format.duration(-1) != Format.duration(0)) Fiber.abort(\"negative duration not clamped\")\n",
		};
		CHECK(status == 0);
		passed = true;
		for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
			char module[64];
			(void)snprintf(module, sizeof(module), "core_contract_%zu", i);
			bool ok = wrenInterpret(host->vm, module, cases[i])
			    == WREN_RESULT_SUCCESS;
			fprintf(stderr, "core contract %zu: %s\n", i, ok ? "PASS" : "FAIL");
			passed = passed && ok;
		}
	}
	else {
		CHECK(status == 0);
		bool menu_loaded = wrenHasModule(host->vm, "audit_menu_marker");
		bool picker_loaded = wrenHasModule(host->picker_vm, "audit_picker_marker");

		passed = menu_loaded && picker_loaded;
		fprintf(stderr, "additional menu script: %s; additional picker script: %s\n",
		    menu_loaded ? "PASS" : "FAIL", picker_loaded ? "PASS" : "FAIL");
	}
	st_wren_host_destroy(host);
	st_console_destroy(console);
	return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}

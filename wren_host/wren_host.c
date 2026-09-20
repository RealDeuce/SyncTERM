/* Persistent Wren host and embedded-module loader. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "wren_host_internal.h"

#include "filesystem.h"
#include "io_error.h"
#include "path.h"
#include "st_app_paths.h"
#include "st_bbs_list.h"
#include "st_builtin_fonts.h"
#include "st_console.h"
#include "st_settings.h"
#include "st_theme.h"
#include "string_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ST_WREN_MAX_MODULE_SIZE (4U * 1024U * 1024U)
#define ST_WREN_FIRST_CUSTOM_FONT ST_BUILTIN_FONT_COUNT

_Static_assert(ST_WREN_CUSTOM_FONT_MAX == ST_SETTINGS_CUSTOM_FONT_MAX,
	"custom font limits must agree");
_Static_assert(ST_BUILTIN_FONT_COUNT == 46U,
	"Wren font selectors reserve 0 through 45 for built-ins");

static int
load_builtin_fonts(struct st_wren_context *context)
{
	struct st_console_font font;
	struct st_console_style style;
	uint32_t width;
	uint32_t height;
	uint16_t identifier;
	int status;

	if (st_console_cell_pixel_size(context->console, &width, &height) != 0)
		return IO_ERROR_INVALID_ARGUMENT;
	for (size_t selector = 0; selector < st_builtin_font_count(); ++selector) {
		context->fonts.catalog_valid[selector] = false;
		if (!st_builtin_font_get(selector, width, height, &font))
			continue;
		status = st_console_register_static_font(context->console, &font,
		    &identifier);
		if (status != 0)
			return status;
		context->fonts.catalog[selector] = identifier;
		context->fonts.catalog_valid[selector] = true;
		context->fonts.selector_by_identifier[identifier]
		    = (uint16_t)selector;
	}
	if (!context->fonts.catalog_valid[0]) {
		context->fonts.catalog[0] = 0;
		context->fonts.catalog_valid[0] = true;
		context->fonts.selector_by_identifier[0] = 0;
		return 0;
	}
	status = st_console_get_style(context->console, &style);
	if (status != 0)
		return status;
	style.font = context->fonts.catalog[0];
	return st_console_set_style(context->console, &style);
}

static bool
custom_font_dimensions(uint32_t width, uint32_t height,
	enum st_settings_font_path *slot, size_t *size)
{
	if (width == 8 && height == 8)
		*slot = ST_SETTINGS_FONT_8X8;
	else if (width == 8 && height == 14)
		*slot = ST_SETTINGS_FONT_8X14;
	else if (width == 8 && height == 16)
		*slot = ST_SETTINGS_FONT_8X16;
	else if (width == 12 && height == 20)
		*slot = ST_SETTINGS_FONT_12X20;
	else
		return false;
	*size = ((size_t)width + 7) / 8 * height * 256;
	return true;
}

static uint8_t *
read_custom_font(const char *path, size_t size)
{
	uint8_t *bitmap;
	FILE *stream;

	if (path == NULL || fs_open(&stream, path, "rb") < 0)
		return NULL;
	if (fs_stream_size(stream) != (int64_t)size) {
		(void)fclose(stream);
		return NULL;
	}
	bitmap = malloc(size);
	if (bitmap != NULL && fread(bitmap, 1, size, stream) != size) {
		free(bitmap);
		bitmap = NULL;
	}
	(void)fclose(stream);
	return bitmap;
}

bool
st_wren_font_identifier(struct st_wren_context *context, uint16_t selector,
	uint16_t *identifier)
{
	if (selector > UINT8_MAX || !context->fonts.catalog_valid[selector])
		return false;
	*identifier = context->fonts.catalog[selector];
	return true;
}

uint16_t
st_wren_font_selector(struct st_wren_context *context, uint16_t identifier)
{
	return context->fonts.selector_by_identifier[identifier];
}

static int
reload_custom_fonts_current(struct st_wren_context *context)
{
	uint8_t *fresh[ST_WREN_CUSTOM_FONT_MAX] = {0};
	uint16_t identifiers[ST_WREN_CUSTOM_FONT_MAX] = {0};
	bool available[ST_WREN_CUSTOM_FONT_MAX] = {0};
	uint32_t width;
	uint32_t height;
	enum st_settings_font_path slot;
	size_t size;
	size_t count;
	int status;

	if (context == NULL || st_console_cell_pixel_size(context->console,
	    &width, &height) < 0)
		return IO_ERROR_INVALID_ARGUMENT;
	if (!custom_font_dimensions(width, height, &slot, &size))
		return 0;
	count = st_settings_font_count();
	for (size_t i = 0; i < count; ++i) {
		const struct st_settings_font *configured = st_settings_font_at(i);
		const char *path = configured->path[slot];

		if (path == NULL)
			continue;
		fresh[i] = read_custom_font(path, size);
		if (fresh[i] == NULL) {
			/* Unreadable and wrong-sized configured files stay unavailable. */
			continue;
		}
	}
	for (size_t i = 0; i < count; ++i) {
		status = 0;
		if (i < count && fresh[i] != NULL) {
			const struct st_console_font font = {
				.bitmap = fresh[i],
				.bitmap_size = size,
				.width = width,
				.height = height,
				.glyph_count = 256,
				.row_stride = ((size_t)width + 7) / 8,
				.glyph_stride = (((size_t)width + 7) / 8) * height,
			};

			status = st_console_register_font_copy(context->console,
			    &font, &identifiers[i]);
			if (status == 0)
				available[i] = true;
		}
		if (status < 0) {
			for (size_t free_index = 0; free_index < count; ++free_index)
				free(fresh[free_index]);
			return status;
		}
	}
	for (size_t i = 0; i < ST_WREN_CUSTOM_FONT_MAX; ++i) {
		uint16_t selector = (uint16_t)(ST_WREN_FIRST_CUSTOM_FONT + i);

		context->fonts.catalog_valid[selector] = i < count && available[i];
		if (i < count && available[i]) {
			context->fonts.catalog[selector] = identifiers[i];
			context->fonts.selector_by_identifier[identifiers[i]] = selector;
		}
	}
	for (size_t i = 0; i < count; ++i)
		free(fresh[i]);
	context->fonts.loaded_count = count;
	return 0;
}

int
st_wren_reload_custom_fonts(struct st_wren_host *host)
{
	int status;

	if (host == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = reload_custom_fonts_current(&host->menu);
	if (status == 0 && host->picker.console != host->menu.console)
		status = reload_custom_fonts_current(&host->picker);
	else if (status == 0) {
		uint16_t slots[5];

		/* The one-console host shares registrations, not VM font selectors. */
		memcpy(slots, host->picker.fonts.slots, sizeof(slots));
		host->picker.fonts = host->menu.fonts;
		memcpy(host->picker.fonts.slots, slots, sizeof(slots));
	}
	return status;
}

static bool
module_name_valid(const char *name)
{
	if (name == NULL || *name == '\0')
		return false;
	for (const unsigned char *p = (const unsigned char *)name;
	    *p != '\0'; ++p) {
		if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')
		    || (*p >= '0' && *p <= '9') || *p == '_'))
			return false;
	}
	return true;
}

static char *
read_source(const char *path)
{
	FILE *stream;
	char *source;
	long length;
	size_t read_length;

	stream = fopen(path, "rb");
	if (stream == NULL)
		return NULL;
	if (fseek(stream, 0, SEEK_END) != 0
	    || (length = ftell(stream)) < 0
	    || (unsigned long)length > ST_WREN_MAX_MODULE_SIZE
	    || fseek(stream, 0, SEEK_SET) != 0) {
		fclose(stream);
		return NULL;
	}
	source = malloc((size_t)length + 1);
	if (source == NULL) {
		fclose(stream);
		return NULL;
	}
	read_length = fread(source, 1, (size_t)length, stream);
	if (read_length != (size_t)length || ferror(stream)) {
		free(source);
		fclose(stream);
		return NULL;
	}
	source[read_length] = '\0';
	fclose(stream);
	return source;
}

static char *
read_named_source(const char *directory, const char *event, const char *name)
{
	char *filename = NULL;
	char *event_directory = NULL;
	char *path = NULL;
	char *source = NULL;
	size_t name_length = strlen(name);

	filename = malloc(name_length + sizeof(".wren"));
	if (filename == NULL)
		return NULL;
	memcpy(filename, name, name_length);
	memcpy(filename + name_length, ".wren", sizeof(".wren"));
	if (event == NULL) {
		if (path_join_alloc(&path, directory, filename) == 0)
			source = read_source(path);
	}
	else if (path_join_alloc(&event_directory, directory, "auto") == 0) {
		char *next = NULL;

		if (path_join_alloc(&next, event_directory, event) == 0
		    && path_join_alloc(&path, next, filename) == 0)
			source = read_source(path);
		free(next);
	}
	free(path);
	free(event_directory);
	free(filename);
	return source;
}

static void
source_complete(WrenVM *vm, const char *name, WrenLoadModuleResult result)
{
	(void)vm;
	(void)name;
	free((void *)result.source);
}

static WrenLoadModuleResult
load_module(WrenVM *vm, const char *name)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	WrenLoadModuleResult result = {NULL, NULL, NULL};
	bool picker = vm == host->picker_vm;
	char *source;

	if (!module_name_valid(name)) {
		result.source = "Fiber.abort(\"invalid module name\")";
		return result;
	}
	if (!picker && (strcmp(name, "syncterm_picker") == 0
	    || strcmp(name, "picker_bootstrap") == 0)) {
		result.source =
		    "Fiber.abort(\"picker modules are isolated from the menu VM\")";
		return result;
	}
	if (picker && strcmp(name, "syncterm_menu") == 0) {
		result.source =
		    "Fiber.abort(\"menu facilities are unavailable in the picker VM\")";
		return result;
	}
	if (picker && strcmp(name, "picker_bootstrap") == 0) {
		for (const struct st_embedded_script *script =
		    st_wren_embedded_scripts; script->name != NULL; ++script) {
			if (strcmp(script->name, name) == 0
			    && script->event == NULL) {
				result.source = script->source;
				return result;
			}
		}
		return result;
	}
	source = read_named_source(host->config.paths->scripts_directory, NULL,
	    name);
	if (source == NULL)
		source = read_named_source(host->config.paths->scripts_directory,
		    picker ? "picker" : "menu", name);
	if (source != NULL) {
		result.source = source;
		result.onComplete = source_complete;
		return result;
	}
	for (const struct st_embedded_script *script = st_wren_embedded_scripts;
	    script->name != NULL; ++script) {
		if (strcmp(script->name, name) == 0
		    && (script->event == NULL
		    || strcmp(script->event, picker ? "picker" : "menu") == 0)) {
			result.source = script->source;
			return result;
		}
	}
	return result;
}

static WrenForeignMethodFn
bind_method(WrenVM *vm, const char *module, const char *class_name,
	bool is_static, const char *signature)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	WrenForeignMethodFn method = NULL;

	method = st_wren_picker_bind_method(module, class_name, is_static,
	    signature);
	if (method == NULL && strcmp(module, "syncterm") == 0)
		method = st_wren_pixel_bind_method(class_name, is_static, signature);
	if (method == NULL && strcmp(module, "syncterm") == 0)
		method = st_wren_repl_bind_method(class_name, is_static, signature);
	if (method == NULL && strcmp(module, "syncterm") == 0)
		method = st_wren_core_bind_method(class_name, is_static,
		    signature);
	else if (method == NULL && vm == host->vm
	    && strcmp(module, "syncterm_menu") == 0)
		method = st_wren_menu_bind_method(class_name, is_static, signature);
	return method == NULL ? st_wren_unsupported : method;
}

static WrenForeignClassMethods
bind_class(WrenVM *vm, const char *module, const char *class_name)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	WrenForeignClassMethods methods = {NULL, NULL};

	methods = st_wren_picker_bind_class(module, class_name);
	if (methods.allocate != NULL)
		return methods;
	if (strcmp(module, "syncterm") == 0) {
		methods = st_wren_pixel_bind_class(class_name);
		if (methods.allocate != NULL)
			return methods;
		methods = st_wren_core_bind_class(class_name);
	}
	else if (vm == host->vm && strcmp(module, "syncterm_menu") == 0)
		methods = st_wren_menu_bind_class(class_name);
	return methods;
}

static void
write_output(WrenVM *vm, const char *text)
{
	st_wren_log_append(vm, 0, text);
}

static void
report_error(WrenVM *vm, WrenErrorType type, const char *module, int line,
	const char *message)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	const char *kind = type == WREN_ERROR_COMPILE ? "compile" :
	    type == WREN_ERROR_RUNTIME ? "runtime" : "stack";

	st_wren_log_error(vm, type, module, line, message);
	if (st_wren_context_from_vm(vm)->capturing)
		return;

	if (type != WREN_ERROR_STACK_TRACE)
		(void)snprintf(host->last_error, sizeof(host->last_error),
		    "%s %s:%d: %s", kind, module == NULL ? "?" : module, line,
		    message == NULL ? "" : message);
	fprintf(stderr, "[%s] %s %s:%d: %s\n",
	    vm == host->picker_vm ? "wren-picker" : "wren-menu", kind,
	    module == NULL ? "?" : module, line, message == NULL ? "" : message);
}

static int
interpret_event_scripts(WrenVM *vm, const char *event)
{
	for (const struct st_embedded_script *script = st_wren_embedded_scripts;
	    script->name != NULL; ++script) {
		WrenLoadModuleResult loaded;
		WrenInterpretResult result;

		if (script->event == NULL || strcmp(script->event, event) != 0)
			continue;
		loaded = load_module(vm, script->name);
		if (loaded.source == NULL)
			return IO_ERROR_NOT_FOUND;
		result = wrenInterpret(vm, script->name, loaded.source);
		if (loaded.onComplete != NULL)
			loaded.onComplete(vm, script->name, loaded);
		if (result != WREN_RESULT_SUCCESS)
			return IO_ERROR_INVALID_FORMAT;
	}
	return 0;
}

static const struct st_embedded_script *
embedded_script(const char *name)
{
	for (const struct st_embedded_script *script = st_wren_embedded_scripts;
	    script->name != NULL; ++script) {
		if (script->event == NULL && strcmp(script->name, name) == 0)
			return script;
	}
	return NULL;
}

static WrenHandle *
class_handle(WrenVM *vm, const char *module, const char *class_name)
{
	if (!wrenHasModule(vm, module)
	    || !wrenHasVariable(vm, module, class_name))
		return NULL;
	wrenEnsureSlots(vm, 1);
	wrenGetVariable(vm, module, class_name, 0);
	return wrenGetSlotHandle(vm, 0);
}

static bool
menu_status(struct st_wren_host *host, const char *title,
	const char *const *lines, size_t count)
{
	if (host->menu_ui_class == NULL || host->menu_status == NULL)
		return false;
	wrenEnsureSlots(host->vm, 4);
	wrenSetSlotHandle(host->vm, 0, host->menu_ui_class);
	wrenSetSlotString(host->vm, 1, title);
	wrenSetSlotNewList(host->vm, 2);
	for (size_t i = 0; i < count; ++i) {
		wrenSetSlotString(host->vm, 3, lines[i]);
		wrenInsertInList(host->vm, 2, -1, 3);
	}
	return wrenCall(host->vm, host->menu_status) == WREN_RESULT_SUCCESS;
}

static void
menu_status_clear(struct st_wren_host *host)
{
	if (host->menu_ui_class == NULL || host->menu_status_clear == NULL)
		return;
	wrenEnsureSlots(host->vm, 1);
	wrenSetSlotHandle(host->vm, 0, host->menu_ui_class);
	(void)wrenCall(host->vm, host->menu_status_clear);
}

static const char *
web_phase_name(enum st_webget_phase phase)
{
	switch (phase) {
		case ST_WEBGET_RESOLVING: return "Resolving";
		case ST_WEBGET_CONNECTING: return "Connecting";
		case ST_WEBGET_TLS: return "TLS setup";
		case ST_WEBGET_REQUESTING: return "Requesting";
		case ST_WEBGET_HEADERS: return "Reading headers";
		case ST_WEBGET_BODY: return "Reading response";
		case ST_WEBGET_VALIDATING: return "Validating";
		case ST_WEBGET_INSTALLING: return "Installing";
	}
	return "Updating";
}

static const char *
web_error_name(int status)
{
	switch (status) {
		case IO_ERROR_INVALID_ARGUMENT: return "Invalid URL";
		case IO_ERROR_NAME_RESOLUTION: return "Name lookup failed";
		case IO_ERROR_CONNECTION_REFUSED: return "Connection refused";
		case IO_ERROR_CONNECTION_RESET: return "Connection reset";
		case IO_ERROR_NETWORK_UNREACHABLE: return "Network unreachable";
		case IO_ERROR_HOST_UNREACHABLE: return "Host unreachable";
		case IO_ERROR_TIMED_OUT: return "Timed out";
		case IO_ERROR_MESSAGE_TOO_LARGE: return "List is too large";
		case IO_ERROR_INVALID_FORMAT: return "Invalid list";
		case IO_ERROR_NOT_SUPPORTED: return "Not supported";
		case IO_ERROR_OUT_OF_MEMORY: return "Out of memory";
		default: return "Update failed";
	}
}

static void
format_byte_count(char *result, size_t size, uint64_t value)
{
	static const char *const units[] = {
		"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB",
	};
	uint64_t whole = value;
	uint64_t remainder = 0;
	size_t unit = 0;

	while (whole >= 1024 && unit + 1 < sizeof(units) / sizeof(units[0])) {
		remainder = whole % 1024;
		whole /= 1024;
		unit++;
	}
	if (unit == 0 || whole >= 10) {
		(void)snprintf(result, size, "%llu %s",
		    (unsigned long long)whole, units[unit]);
	}
	else {
		uint64_t tenth = (remainder * 10 + 512) / 1024;

		if (tenth == 10) {
			whole++;
			tenth = 0;
		}
		(void)snprintf(result, size, "%llu.%llu %s",
		    (unsigned long long)whole, (unsigned long long)tenth,
		    units[unit]);
	}
}

static unsigned
web_percentage(uint64_t received, uint64_t total)
{
	uint64_t whole;
	uint64_t remainder;

	if (total == 0)
		return 0;
	if (received >= total)
		return 100;
	whole = total / 100;
	remainder = total % 100;
	for (unsigned percent = 100; percent > 0; --percent) {
		uint64_t threshold = whole * percent
		    + (remainder * percent + 99) / 100;

		if (received >= threshold)
			return percent;
	}
	return 0;
}

struct web_progress_ui {
	struct st_wren_host *host;
	st_console_snapshot_t *snapshot;
	char (*text)[192];
	const char **lines;
	bool visible;
	bool disabled;
};

enum {
	WEB_PROGRESS_ROW_WIDTH = 67,
};

static void
format_web_progress(char *result, size_t size,
	const struct st_bbs_web_refresh_item *item)
{
	char content[192];
	const char *name = item->name == NULL ? "" : item->name;
	const char *phase;

	if (item->state == ST_BBS_WEB_REFRESH_PENDING) {
		(void)snprintf(content, sizeof(content), "%-20.20s: Waiting",
		    name);
	}
	else if (item->state == ST_BBS_WEB_REFRESH_COMPLETE) {
		(void)snprintf(content, sizeof(content), "%-20.20s: Done", name);
	}
	else if (item->state == ST_BBS_WEB_REFRESH_FAILED) {
		(void)snprintf(content, sizeof(content), "%-20.20s: %.44s", name,
		    web_error_name(item->status));
	}
	else if (item->received != 0 || item->total != 0) {
		char received[24];

		phase = web_phase_name(item->phase);
		format_byte_count(received, sizeof(received), item->received);
		if (item->total_known && item->total != 0) {
			char total[24];

			format_byte_count(total, sizeof(total), item->total);
			(void)snprintf(content, sizeof(content),
			    "%-20.20s: %-20.20s %9s/%-9s %3u%%", name,
			    phase, received, total,
			    web_percentage(item->received, item->total));
		}
		else {
			(void)snprintf(content, sizeof(content),
			    "%-20.20s: %-20.20s %9s", name, phase, received);
		}
	}
	else {
		phase = web_phase_name(item->phase);
		(void)snprintf(content, sizeof(content), "%-20.20s: %-20.20s",
		    name, phase);
	}
	(void)snprintf(result, size, "%-*.*s", WEB_PROGRESS_ROW_WIDTH,
	    WEB_PROGRESS_ROW_WIDTH, content);
}

static void
show_web_progress(const struct st_bbs_web_refresh_item *items, size_t count,
	void *context)
{
	struct web_progress_ui *ui = context;

	if (ui->disabled)
		return;
	if (ui->text == NULL) {
		ui->text = calloc(count, sizeof(ui->text[0]));
		ui->lines = calloc(count, sizeof(ui->lines[0]));
		if (ui->text == NULL || ui->lines == NULL
		    || st_console_snapshot_create(&ui->snapshot,
		    ui->host->config.console) != 0) {
			free(ui->lines);
			free(ui->text);
			ui->lines = NULL;
			ui->text = NULL;
			ui->disabled = true;
			return;
		}
	}
	for (size_t i = 0; i < count; ++i) {
		format_web_progress(ui->text[i], sizeof(ui->text[i]), &items[i]);
		ui->lines[i] = ui->text[i];
	}
	if (!menu_status(ui->host, "Web Cache Update", ui->lines, count)) {
		menu_status_clear(ui->host);
		(void)st_console_snapshot_restore(ui->host->config.console,
		    ui->snapshot);
		st_console_snapshot_destroy(ui->snapshot);
		ui->snapshot = NULL;
		ui->visible = false;
		ui->disabled = true;
		return;
	}
	ui->visible = true;
}

int
st_wren_host_refresh_web_lists(st_wren_host_t *public_host)
{
	struct st_wren_host *host = public_host;
	struct web_progress_ui ui = { .host = host };
	int status;

	if (host == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_bbs_web_cache_refresh(host->config.paths, show_web_progress,
	    &ui);
	if (ui.visible)
		menu_status_clear(host);
	if (ui.snapshot != NULL) {
		(void)st_console_snapshot_restore(host->config.console, ui.snapshot);
		st_console_snapshot_destroy(ui.snapshot);
	}
	free(ui.lines);
	free(ui.text);
	return status;
}

int
st_wren_picker_host_init(struct st_wren_host *host)
{
	const struct st_embedded_script *bootstrap;
	const struct st_embedded_script *request;
	WrenConfiguration config;
	int status;

	bootstrap = embedded_script("picker_bootstrap");
	request = embedded_script("syncterm_picker");
	if (bootstrap == NULL || request == NULL)
		return IO_ERROR_NOT_FOUND;
	wrenInitConfiguration(&config);
	config.userData = &host->picker;
	config.writeFn = write_output;
	config.errorFn = report_error;
	config.loadModuleFn = load_module;
	config.bindForeignMethodFn = bind_method;
	config.bindForeignClassFn = bind_class;
	host->picker_vm = wrenNewVM(&config);
	if (host->picker_vm == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	if (wrenInterpret(host->picker_vm, bootstrap->name, bootstrap->source)
	    != WREN_RESULT_SUCCESS
	    || wrenInterpret(host->picker_vm, request->name, request->source)
	    != WREN_RESULT_SUCCESS)
		return IO_ERROR_INVALID_FORMAT;
	status = interpret_event_scripts(host->picker_vm, "picker");
	if (status != 0)
		return status;
	host->picker_bootstrap = class_handle(host->picker_vm,
	    "picker_bootstrap", "PickerBootstrap");
	host->picker_class = class_handle(host->picker_vm, "file_picker",
	    "FilePicker");
	host->picker_run = wrenMakeCallHandle(host->picker_vm, "run(_,_)" );
	host->picker_recover = wrenMakeCallHandle(host->picker_vm,
	    "recover(_,_)" );
	if (host->picker_bootstrap == NULL || host->picker_class == NULL
	    || host->picker_run == NULL || host->picker_recover == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	return 0;
}

void
st_wren_picker_host_dispose(struct st_wren_host *host)
{
	if (host == NULL || host->picker_vm == NULL)
		return;
	if (host->picker_recover != NULL)
		wrenReleaseHandle(host->picker_vm, host->picker_recover);
	if (host->picker_run != NULL)
		wrenReleaseHandle(host->picker_vm, host->picker_run);
	if (host->picker_class != NULL)
		wrenReleaseHandle(host->picker_vm, host->picker_class);
	if (host->picker_bootstrap != NULL)
		wrenReleaseHandle(host->picker_vm, host->picker_bootstrap);
	st_wren_log_dispose(host->picker_vm);
	wrenFreeVM(host->picker_vm);
	host->picker_vm = NULL;
	host->picker_recover = NULL;
	host->picker_run = NULL;
	host->picker_class = NULL;
	host->picker_bootstrap = NULL;
}

struct st_wren_host *
st_wren_host_from_vm(WrenVM *vm)
{
	return st_wren_context_from_vm(vm)->host;
}

struct st_wren_context *
st_wren_context_from_vm(WrenVM *vm)
{
	return wrenGetUserData(vm);
}

void
st_wren_abort(WrenVM *vm, const char *message)
{
	wrenSetSlotString(vm, 0, message);
	wrenAbortFiber(vm, 0);
}

void
st_wren_unsupported(WrenVM *vm)
{
	st_wren_abort(vm, "this capability is not available in the menu host");
}

int
st_wren_host_create(st_wren_host_t **result,
	const struct st_wren_host_config *config)
{
	struct st_wren_host *host;
	WrenConfiguration wren_config;
	int status;

	if (result == NULL || config == NULL || config->console == NULL
	    || ((config->display == NULL) != (config->picker_console == NULL))
	    || config->paths == NULL || config->paths->scripts_directory == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	host = calloc(1, sizeof(*host));
	if (host == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	host->config = *config;
	host->menu.host = host;
	host->menu.console = config->console;
	host->picker.host = host;
	host->picker.console = config->picker_console != NULL
	    ? config->picker_console : config->console;
	host->font_generation = 1;
	st_profile_init(&host->selected);
	status = load_builtin_fonts(&host->menu);
	if (status == 0)
		status = reload_custom_fonts_current(&host->menu);
	if (status == 0 && host->picker.console != host->menu.console) {
		status = load_builtin_fonts(&host->picker);
		if (status == 0)
			status = reload_custom_fonts_current(&host->picker);
	}
	else if (status == 0)
		host->picker.fonts = host->menu.fonts;
	if (status != 0) {
		st_wren_host_destroy(host);
		return status;
	}
	status = st_theme_manager_create(&host->themes, config->paths,
	    &st_settings, host->last_error, sizeof(host->last_error));
	if (status != 0) {
		st_wren_host_destroy(host);
		return status;
	}
	wrenInitConfiguration(&wren_config);
	wren_config.userData = &host->menu;
	wren_config.writeFn = write_output;
	wren_config.errorFn = report_error;
	wren_config.loadModuleFn = load_module;
	wren_config.bindForeignMethodFn = bind_method;
	wren_config.bindForeignClassFn = bind_class;
	host->vm = wrenNewVM(&wren_config);
	if (host->vm == NULL) {
		st_wren_host_destroy(host);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	status = interpret_event_scripts(host->vm, "menu");
	if (status != 0 || !wrenHasModule(host->vm, "main_menu")
	    || !wrenHasVariable(host->vm, "main_menu", "MainMenu")) {
		st_wren_host_destroy(host);
		return status == 0 ? IO_ERROR_INVALID_FORMAT : status;
	}
	wrenEnsureSlots(host->vm, 1);
	wrenGetVariable(host->vm, "main_menu", "MainMenu", 0);
	host->main_menu = wrenGetSlotHandle(host->vm, 0);
	host->run_menu = wrenMakeCallHandle(host->vm, "run(_,_)" );
	if (host->main_menu == NULL || host->run_menu == NULL) {
		st_wren_host_destroy(host);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	host->menu_ui_class = class_handle(host->vm, "menu_host_ui",
	    "MenuHostUI");
	if (host->menu_ui_class != NULL) {
		host->menu_status = wrenMakeCallHandle(host->vm, "status(_,_)" );
		host->menu_status_clear = wrenMakeCallHandle(host->vm,
		    "statusClear()" );
		if (host->menu_status == NULL || host->menu_status_clear == NULL) {
			st_wren_host_destroy(host);
			return IO_ERROR_OUT_OF_MEMORY;
		}
	}
	status = st_wren_picker_host_init(host);
	if (status != 0) {
		st_wren_host_destroy(host);
		return status;
	}
	*result = host;
	return 0;
}

void
st_wren_host_destroy(st_wren_host_t *host)
{
	if (host == NULL)
		return;
	st_wren_picker_host_dispose(host);
	if (host->vm != NULL && host->menu_status_clear != NULL)
		wrenReleaseHandle(host->vm, host->menu_status_clear);
	if (host->vm != NULL && host->menu_status != NULL)
		wrenReleaseHandle(host->vm, host->menu_status);
	if (host->vm != NULL && host->menu_ui_class != NULL)
		wrenReleaseHandle(host->vm, host->menu_ui_class);
	if (host->vm != NULL && host->run_menu != NULL)
		wrenReleaseHandle(host->vm, host->run_menu);
	if (host->vm != NULL && host->main_menu != NULL)
		wrenReleaseHandle(host->vm, host->main_menu);
	if (host->vm != NULL) {
		st_wren_log_dispose(host->vm);
		wrenFreeVM(host->vm);
	}
	st_bbs_list_close(host->bbs_list);
	st_theme_manager_free(&host->themes);
	st_profile_dispose(&host->selected);
	free(host);
}

int
st_wren_host_run_menu(st_wren_host_t *host, struct st_profile *selected,
	bool *has_selection)
{
	WrenInterpretResult result;

	if (host == NULL || selected == NULL || has_selection == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	host->selected_valid = false;
	wrenEnsureSlots(host->vm, 3);
	wrenSetSlotHandle(host->vm, 0, host->main_menu);
	wrenSetSlotNull(host->vm, 1);
	wrenSetSlotBool(host->vm, 2, false);
	result = wrenCall(host->vm, host->run_menu);
	if (result != WREN_RESULT_SUCCESS)
		return IO_ERROR_INVALID_FORMAT;
	host->selected_valid = wrenGetSlotType(host->vm, 0) != WREN_TYPE_NULL
	    && st_wren_menu_copy_profile(host->vm, 0, &host->selected);
	*has_selection = host->selected_valid;
	if (host->selected_valid && !st_profile_copy(selected, &host->selected))
		return IO_ERROR_OUT_OF_MEMORY;
	return 0;
}

bool
st_wren_host_exit_requested(const st_wren_host_t *host)
{
	return host != NULL && host->exit_requested;
}

const char *
st_wren_host_last_error(const st_wren_host_t *host)
{
	return host == NULL || host->last_error[0] == '\0' ? NULL :
	    host->last_error;
}

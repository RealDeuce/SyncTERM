/* Internal declarations shared by generated modules and Wren bindings. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_WREN_HOST_INTERNAL_H_
#define ST_WREN_HOST_INTERNAL_H_

#include "st_wren_host.h"
#include "st_profile.h"
#include "wren.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct st_app_paths;
struct st_bbs_list;
struct st_profile;
struct st_theme_manager;
struct st_theme;

#define ST_WREN_PICKER_MASK_LOCKED       (1 << 0)
#define ST_WREN_PICKER_MASK_CASE         (1 << 1)
#define ST_WREN_PICKER_CASE_SORT         (1 << 2)
#define ST_WREN_PICKER_SELECT_DIRECTORY  (1 << 3)
#define ST_WREN_PICKER_PATH_MUST_EXIST   (1 << 4)
#define ST_WREN_PICKER_FILE_MUST_EXIST   (1 << 5)
#define ST_WREN_PICKER_SHOW_HIDDEN       (1 << 6)
#define ST_WREN_PICKER_ALLOW_ENTRY       (1 << 8)
#define ST_WREN_PICKER_CONFIRM_OVERWRITE (1 << 9)
#define ST_WREN_PICKER_CONFIRM_CREATE    (1 << 10)
#define ST_WREN_PICKER_ALLOW_APPEND      (1 << 11)
#define ST_WREN_CUSTOM_FONT_MAX 210

enum st_wren_picker_mode {
	ST_WREN_PICKER_FILE,
	ST_WREN_PICKER_DIRECTORY,
	ST_WREN_PICKER_MULTIPLE,
	ST_WREN_PICKER_SAVE,
};

enum st_wren_picker_disposition {
	ST_WREN_PICKER_DISPOSITION_NONE,
	ST_WREN_PICKER_DISPOSITION_READ,
	ST_WREN_PICKER_DISPOSITION_CREATE,
	ST_WREN_PICKER_DISPOSITION_OVERWRITE,
	ST_WREN_PICKER_DISPOSITION_APPEND,
};

struct st_wren_picker_call {
	uint64_t generation;
	bool active;
	bool completed;
	bool quit_application;
	enum st_wren_picker_mode mode;
	const char *title;
	const char *initial_path;
	const char *mask;
	int options;
	char **paths;
	size_t path_count;
	enum st_wren_picker_disposition disposition;
};

struct st_wren_font_state {
	size_t loaded_count;
	uint16_t catalog[UINT8_MAX + 1U];
	bool catalog_valid[UINT8_MAX + 1U];
	uint16_t selector_by_identifier[UINT16_MAX + 1U];
	uint16_t slots[5];
};

#define ST_WREN_LOG_CAPACITY 1024
struct st_wren_log_entry {
	char *text;
	double timestamp;
	int source;
	WrenHandle *cached;
};

struct st_wren_log {
	struct st_wren_log_entry entries[ST_WREN_LOG_CAPACITY];
	uint64_t total;
	uint64_t errors;
	size_t count;
};

/* VM lifetime state, shared by that VM's UI and REPL. */
struct st_wren_context {
	struct st_wren_host *host;
	st_console_t *console;
	struct st_wren_font_state fonts;
	uint32_t mouse_events;
	uint32_t mouse_buttons;
	struct st_wren_log log;
	struct st_wren_log capture;
	bool capturing;
	uint64_t seen_total;
	uint64_t seen_errors;
};

struct st_wren_picker_entry {
	char *name;
	bool directory;
	uint64_t size;
	int64_t modified;
};

struct st_wren_picker_entries {
	struct st_wren_picker_entry *items;
	size_t count;
	size_t capacity;
};

struct st_embedded_script {
	const char *name;
	const char *source;
	const char *event;
};

extern const struct st_embedded_script st_wren_embedded_scripts[];

struct st_wren_host {
	WrenVM *vm;
	WrenVM *picker_vm;
	struct st_wren_host_config config;
	struct st_bbs_list *bbs_list;
	WrenHandle *main_menu;
	WrenHandle *run_menu;
	WrenHandle *menu_ui_class;
	WrenHandle *menu_status;
	WrenHandle *menu_status_clear;
	WrenHandle *picker_bootstrap;
	WrenHandle *picker_class;
	WrenHandle *picker_run;
	WrenHandle *picker_recover;
	struct st_wren_picker_call *picker_call;
	uint64_t picker_generation;
	bool exit_requested;
	bool selected_valid;
	bool web_lists_dirty;
	bool fonts_dirty;
	uint64_t font_generation;
	struct st_wren_context menu;
	struct st_wren_context picker;
	struct st_profile selected;
	struct st_theme_manager *themes;
	char last_error[512];
};

struct st_wren_host *st_wren_host_from_vm(WrenVM *vm);
struct st_wren_context *st_wren_context_from_vm(WrenVM *vm);
void st_wren_abort(WrenVM *vm, const char *message);
void st_wren_log_append(WrenVM *vm, int source, const char *text);
void st_wren_log_error(WrenVM *vm, WrenErrorType type, const char *module,
	int line, const char *message);
void st_wren_log_dispose(WrenVM *vm);
WrenForeignMethodFn st_wren_repl_bind_method(const char *class_name,
	bool is_static, const char *signature);

WrenForeignMethodFn st_wren_core_bind_method(const char *class_name,
	bool is_static, const char *signature);
WrenForeignClassMethods st_wren_core_bind_class(const char *class_name);
WrenForeignMethodFn st_wren_menu_bind_method(const char *class_name,
	bool is_static, const char *signature);
WrenForeignClassMethods st_wren_menu_bind_class(const char *class_name);
bool st_wren_menu_copy_profile(WrenVM *vm, int slot,
	struct st_profile *result);

WrenForeignMethodFn st_wren_picker_bind_method(const char *module,
	const char *class_name, bool is_static, const char *signature);
WrenForeignClassMethods st_wren_picker_bind_class(const char *module,
	const char *class_name);
int st_wren_picker_host_init(struct st_wren_host *host);
void st_wren_picker_host_dispose(struct st_wren_host *host);
bool st_wren_picker_host_run(struct st_wren_host *host,
	struct st_wren_picker_call *call);
void st_wren_picker_call_dispose(struct st_wren_picker_call *call);
const char *st_wren_file_read_path(WrenVM *vm, int slot);

int st_wren_picker_enumerate(const char *directory,
	struct st_wren_picker_entries *entries);
void st_wren_picker_entries_dispose(struct st_wren_picker_entries *entries);

void st_wren_unsupported(WrenVM *vm);
void st_wren_push_theme_data(WrenVM *vm, const struct st_theme *theme);
int st_wren_reload_custom_fonts(struct st_wren_host *host);
bool st_wren_font_identifier(struct st_wren_context *context, uint16_t selector,
	uint16_t *identifier);
uint16_t st_wren_font_selector(struct st_wren_context *context,
	uint16_t identifier);

#endif

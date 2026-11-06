/* Internal declarations shared by generated modules and Wren bindings. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_WREN_HOST_INTERNAL_H_
#define ST_WREN_HOST_INTERNAL_H_

#include "st_wren_host.h"
#include "st_console.h"
#include "st_codepage.h"
#include "st_profile.h"
#include "st_screen_mode.h"
#include "st_settings.h"
#include "wren_directory.h"
#include "wren_bbs_model.h"
#include "wren.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Deliberately exclude 2^53: every integer in this range has a unique Number. */
#define ST_WREN_MAX_INTEGER UINT64_C(9007199254740991)
bool st_wren_u64_exact(uint64_t value);
bool st_wren_i64_exact(int64_t value);
/* Failed reads preserve the result; checked returns abort on overflow. */
bool st_wren_number_u64(WrenVM *vm, int slot, uint64_t *result);
bool st_wren_number_i64(WrenVM *vm, int slot, int64_t *result);
void st_wren_return_u64(WrenVM *vm, uint64_t value);
void st_wren_return_i64(WrenVM *vm, int64_t value);

struct st_app_paths;
struct st_bbs_list;
struct st_profile;
struct st_theme_manager;
struct st_theme;
struct st_emoji_locale;

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
	bool amiga_replacement, confirmed;
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
	bool runtime_loaded[UINT8_MAX + 1U];
	enum st_codepage codepages[UINT8_MAX + 1U];
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

enum st_wren_input_type {
	ST_WREN_INPUT_KEY,
	ST_WREN_INPUT_PHYSICAL_KEY,
	ST_WREN_INPUT_MOUSE,
};

struct st_wren_input_record {
	enum st_wren_input_type type;
	union {
		struct {
			struct st_console_event native;
		} key;
		struct {
			uint16_t evdev;
			uint32_t modifiers;
			bool pressed;
			bool repeat;
		} physical_key;
		struct {
			int event;
			uint32_t buttons;
			uint32_t modifiers;
			int32_t start_x;
			int32_t start_y;
			int32_t end_x;
			int32_t end_y;
			int32_t start_pixel_x;
			int32_t start_pixel_y;
			int32_t end_pixel_x;
			int32_t end_pixel_y;
			int32_t wheel_x;
			int32_t wheel_y;
		} mouse;
	} data;
};

enum st_wren_local_mouse_event {
	ST_WREN_MOUSE_MOVE = 0,
	ST_WREN_MOUSE_LEFT_PRESS = 1,
	ST_WREN_MOUSE_LEFT_RELEASE = 2,
	ST_WREN_MOUSE_LEFT_CLICK = 3,
	ST_WREN_MOUSE_LEFT_QUAD_CLICK = 6,
	ST_WREN_MOUSE_LEFT_DRAG_START = 7,
	ST_WREN_MOUSE_LEFT_DRAG_MOVE = 8,
	ST_WREN_MOUSE_LEFT_DRAG_END = 9,
};
#define ST_WREN_LINK_MOUSE_EVENTS ((UINT64_C(1) << ST_WREN_MOUSE_MOVE) \
	| (UINT64_C(1) << ST_WREN_MOUSE_LEFT_PRESS) \
	| (UINT64_C(1) << ST_WREN_MOUSE_LEFT_RELEASE) \
	| (UINT64_C(1) << ST_WREN_MOUSE_LEFT_CLICK) \
	| (UINT64_C(1) << ST_WREN_MOUSE_LEFT_DRAG_START) \
	| (UINT64_C(1) << ST_WREN_MOUSE_LEFT_DRAG_MOVE) \
	| (UINT64_C(1) << ST_WREN_MOUSE_LEFT_DRAG_END))

struct st_wren_mouse_gesture {
	int32_t start_x;
	int32_t start_y;
	int32_t start_pixel_x;
	int32_t start_pixel_y;
	int32_t end_x;
	int32_t end_y;
	uint32_t modifiers;
	bool pressed;
	bool dragging;
};

struct st_wren_operation;
struct st_wren_input_claim;
struct st_wren_wake;
struct surface_storage;
struct st_wren_transfer;
struct st_wren_send;
struct st_wren_cleanup;
struct st_wren_session_lease;

/* VM lifetime state, shared by that VM's UI and REPL. */
struct st_wren_context {
	struct st_wren_host *host;
	WrenVM *vm;
	st_console_t *console;
	struct st_wren_font_state fonts;
	uint64_t mouse_events;
	uint32_t mouse_buttons;
	struct st_wren_mouse_gesture mouse_gestures[5];
	struct st_wren_input_record *pending_input;
	size_t pending_input_head;
	size_t pending_input_count;
	size_t pending_input_capacity;
	struct st_wren_input_claim *claim_top;
	uint64_t next_claim_id;
	struct st_wren_operation *operations;
	struct st_wren_cleanup *cleanups;
	bool cleanup_closing;
	struct st_wren_wire *wire;
	struct st_wren_send *send_head, *send_tail;
	size_t send_count, send_storage;
	struct st_wren_wake *wake_head;
	struct st_wren_wake *wake_tail;
	WrenHandle *hook_class;
	WrenHandle *hook_dispatch;
	WrenHandle *fiber_is_done;
	WrenHandle *fiber_call;
	struct st_wren_log log;
	struct st_wren_log capture;
	bool capturing;
	uint64_t seen_total;
	uint64_t seen_errors;
	struct st_wren_connected *connected;
	const struct st_profile *profile;
	int transfer_log_level;
	void (*transfer_log)(void *context, int level, const char *message);
	void *transfer_log_context;
	int (*session_log)(void *context, char **text);
	void *session_log_context;
	st_cterm_t *cterm;
	struct st_connection *connection;
	struct st_wren_transfer *transfer;
	struct st_term_session *session;
	uint64_t connected_at_ms;
	const char *event;
	struct st_wren_session_lease *session_leases;
	WrenHandle *session_value;
	WrenHandle *profile_value;
	bool status_enabled;
	bool link_hover_valid;
	bool link_gesture;
	int32_t link_hover_x;
	int32_t link_hover_y;
	bool cterm_suspended;
	bool scrollback_requested;
	bool cterm_mouse_disabled;
	WrenHandle *status_callable;
	WrenHandle *status_details;
	WrenHandle *status_surface;
	WrenHandle *status_call;
	uint32_t status_surface_width;
};

struct st_embedded_script {
	const char *name;
	const char *source;
	const char *event;
};

extern const struct st_embedded_script st_wren_embedded_scripts[];

struct st_wren_host {
	WrenVM *vm;
	struct st_emoji_locale *emoji_locale;
	WrenVM *picker_vm;
	struct st_wren_host_config config;
	struct st_bbs_list *bbs_list;
	struct st_wren_bbs_model bbs_model;
	WrenHandle *main_menu;
	WrenHandle *run_menu;
	WrenHandle *menu_ui_class;
	WrenHandle *menu_prompt;
	WrenHandle *menu_status;
	WrenHandle *menu_status_clear;
	st_console_snapshot_t *status_snapshot;
	struct st_wren_tls_observation *tls_observations;
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
	bool hyperlink_root_registered;
	struct surface_storage *surface_storage;
	uint64_t font_generation;
	uint64_t amiga_font_generation;
	struct st_settings_font_draft font_draft;
	uint64_t settings_generation;
	enum st_screen_mode current_screen_mode;
	struct st_screen_mode_descriptor current_mode;
	struct st_screen_mode_descriptor menu_mode;
	struct st_screen_mode_descriptor history_mode;
	bool history_mode_valid;
	enum st_screen_mode menu_mode_identity;
	bool session_mode_active;
	bool offline_history_mode_active;
	struct st_wren_context menu;
	struct st_wren_context picker;
	struct st_profile selected;
	struct st_theme_manager *themes;
	char last_error[512];
};

struct st_wren_host *st_wren_host_from_vm(WrenVM *vm);
struct st_wren_context *st_wren_context_from_vm(WrenVM *vm);
int st_wren_host_run_connected_menu(struct st_wren_host *host);
/* Class checks do not allocate, grow slots, or inspect the foreign payload. */
bool st_wren_foreign_is(WrenVM *vm, int slot, const char *module_name,
	const char *class_name);
void *st_wren_foreign_require(WrenVM *vm, int slot, const char *module_name,
	const char *class_name);
void st_wren_abort(WrenVM *vm, const char *message);
bool st_wren_text_slot(WrenVM *vm, int slot, const char **text, int *length);
void st_wren_log_append(WrenVM *vm, int source, const char *text);
void st_wren_log_error(WrenVM *vm, WrenErrorType type, const char *module,
	int line, const char *message);
void st_wren_log_dispose(WrenVM *vm);
void st_wren_push_surface_take(WrenVM *vm, struct st_console_cell *cells,
	uint32_t width, uint32_t height);
/* Always consumes pixels, including on failure. source_columns describes
 * the captured strip before clipping/padding to the destination Surface. */
int st_wren_surface_take_row_pixels(WrenVM *vm, int slot, uint32_t row,
	uint32_t source_columns, struct st_pixel_buffer *pixels);
bool st_wren_surface_reset(WrenVM *vm, int slot,
	const struct st_console_cell *blank, uint32_t width, uint32_t height);
const struct st_console_cell *st_wren_surface_cells(WrenVM *vm, int slot,
	uint32_t width, uint32_t height);
bool st_wren_surface_view(WrenVM *vm, int slot,
	struct st_console_cell **cells, uint32_t *width, uint32_t *height);
int st_wren_mark_surface_hyperlinks(struct st_wren_host *host,
	st_hyperlink_mark_fn mark, void *mark_context);
WrenForeignMethodFn st_wren_repl_bind_method(const char *class_name,
	bool is_static, const char *signature);
WrenForeignMethodFn st_wren_status_bind_method(const char *class_name,
	bool is_static, const char *signature);
WrenForeignClassMethods st_wren_session_object_bind_class(const char *class_name);
WrenForeignMethodFn st_wren_session_object_bind_method(const char *class_name,
	bool is_static, const char *signature);
void st_wren_session_release_holds(WrenVM *vm);
void st_wren_session_object_dispose(WrenVM *vm);
WrenForeignMethodFn st_wren_session_bind_method(const char *class_name,
	bool is_static, const char *signature);
WrenForeignMethodFn st_wren_transfer_bind_method(const char *class_name,
	bool is_static, const char *signature);
WrenForeignClassMethods st_wren_transfer_bind_class(const char *class_name);
void st_wren_transfer_dispose(struct st_wren_context *context);
int st_wren_transfer_skypix_request(struct st_wren_context *context, unsigned mode,
	unsigned width, unsigned height, const uint8_t *name, size_t size);
int st_wren_transfer_rip_request(struct st_wren_context *context,
	const struct st_cterm_rip_transfer_request *request);
void st_wren_transfer_pump(struct st_wren_context *context);
int st_wren_send_pump(struct st_wren_context *context);
void st_wren_send_dispose(struct st_wren_context *context);
bool st_wren_transfer_detect_enabled(const struct st_wren_context *context);
int st_wren_transfer_filter(struct st_wren_context *context, uint8_t byte,
	bool *consumed, uint8_t *replacement, size_t capacity, size_t *size);
bool st_wren_download_directory_allowed(const char *path);
WrenForeignClassMethods st_wren_session_bind_class(const char *class_name);
int st_wren_status_render(WrenVM *vm, bool enabled);
void st_wren_status_dispose(WrenVM *vm);

void st_wren_cleanup_shutdown(WrenVM *vm);
WrenForeignMethodFn st_wren_cleanup_bind_method(const char *class_name,
	bool is_static, const char *signature);
WrenForeignClassMethods st_wren_cleanup_bind_class(const char *class_name);

/* Native work pins an Operation until resolve; result handles and waiters
 * are released by its finalizer, or explicitly before VM destruction. */
struct st_wren_operation *st_wren_operation_new(WrenVM *vm, int slot);
void st_wren_operation_on_cancel(struct st_wren_operation *operation,
	void (*cancel)(void *, struct st_wren_operation *), void *context);
/* Resolving consumes scratch slots above result_slot. */
int st_wren_operation_resolve(WrenVM *vm, struct st_wren_operation *operation,
	int result_slot);
enum st_wren_wake_action {
	ST_WREN_WAKE_DELIVER,
	ST_WREN_WAKE_DEFER,
	ST_WREN_WAKE_DISCARD,
};
bool st_wren_fibers_same_scope(WrenVM *vm,
    const WrenHandle *left, const WrenHandle *right);
WrenHandle *st_wren_cleanup_wake_target(WrenVM *vm, WrenHandle *fiber);
void st_wren_wake_forget_fiber(WrenVM *vm, const WrenHandle *fiber);
void st_wren_operation_forget_fiber(WrenVM *vm, const WrenHandle *fiber);
enum st_wren_wake_action st_wren_operation_wake_action(WrenVM *vm,
	WrenHandle *fiber, struct st_wren_operation *source);
int st_wren_operation_collect(WrenVM *vm);
int st_wren_operation_cancel_all(WrenVM *vm);
void st_wren_operation_dispose(WrenVM *vm);
WrenForeignMethodFn st_wren_async_bind_method(const char *class_name,
	bool is_static, const char *signature);
WrenForeignClassMethods st_wren_async_bind_class(const char *class_name);

WrenForeignMethodFn st_wren_core_bind_method(const char *class_name,
	bool is_static, const char *signature);
WrenForeignClassMethods st_wren_core_bind_class(const char *class_name);
/* Algorithm zero means invalid input and an already-aborted fiber. */
int st_wren_digest_algorithm(WrenVM *vm, int slot);
void st_wren_digest_push(WrenVM *vm, int algorithm, const void *bytes,
	size_t size, const char *operation);
WrenForeignMethodFn st_wren_digest_bind_method(const char *class_name,
	bool is_static, const char *signature);
WrenForeignClassMethods st_wren_digest_bind_class(const char *class_name);
WrenForeignMethodFn st_wren_fs_bind_method(const char *class_name,
	bool is_static, const char *signature);
WrenForeignMethodFn st_wren_hyperlink_bind_method(const char *class_name,
	bool is_static, const char *signature);
WrenForeignClassMethods st_wren_fs_bind_class(const char *class_name);
bool st_wren_file_push(WrenVM *vm, int slot, const char *path,
	enum st_wren_picker_disposition disposition, bool picker_read);
void st_wren_won_deserialize(WrenVM *vm);
void st_wren_won_error_allocate(WrenVM *vm);
void st_wren_won_error_finalize(void *data);
void st_wren_won_error_code(WrenVM *vm);
void st_wren_won_error_offset(WrenVM *vm);
void st_wren_won_error_message(WrenVM *vm);
void st_wren_won_error_toString(WrenVM *vm);
WrenForeignMethodFn st_wren_pixel_bind_method(const char *class_name,
	bool is_static, const char *signature);
WrenForeignClassMethods st_wren_pixel_bind_class(const char *class_name);
WrenForeignMethodFn st_wren_menu_bind_method(const char *class_name,
	bool is_static, const char *signature);
WrenForeignClassMethods st_wren_menu_bind_class(const char *class_name);
bool st_wren_menu_copy_profile(WrenVM *vm, int slot,
	struct st_profile *result);

WrenForeignMethodFn st_wren_picker_bind_method(const char *module,
	const char *class_name, bool is_static, const char *signature);
WrenForeignClassMethods st_wren_picker_bind_class(const char *module,
	const char *class_name);
int st_wren_host_prepare_history_mode(struct st_wren_host *host);
int st_wren_picker_host_init(struct st_wren_host *host);
void st_wren_picker_host_dispose(struct st_wren_host *host);
bool st_wren_picker_host_run(struct st_wren_host *host,
	struct st_wren_picker_call *call);
void st_wren_picker_call_dispose(struct st_wren_picker_call *call);
const char *st_wren_file_read_path(WrenVM *vm, int slot);
/* Owned validated upload path: readable File or confined session-root path. */
char *st_wren_file_upload_path(WrenVM *vm, int slot);
bool st_wren_file_claim_write(WrenVM *vm, int slot,
	const char *operation, FILE **stream);
void st_wren_file_write_error(WrenVM *vm, int native_error,
	const char *message);
const char *st_wren_directory_path(WrenVM *vm, int slot);
bool st_wren_connected_capture_paused(const struct st_wren_connected *connected);
int st_wren_connected_capture_start(struct st_wren_connected *connected,
	FILE *stream, bool raw);
void st_wren_connected_capture_stop(struct st_wren_connected *connected);
void st_wren_connected_capture_pause(struct st_wren_connected *connected);
void st_wren_connected_capture_resume(struct st_wren_connected *connected);

int st_wren_script_inventory(const char *directory, const char *event,
	struct st_wren_directory_entries *entries);
bool st_wren_bbs_script_name_valid(const char *name);

void st_wren_unsupported(WrenVM *vm);
void st_wren_push_theme_data(WrenVM *vm, const struct st_theme *theme);
int st_wren_reload_custom_fonts(struct st_wren_host *host);
int st_wren_context_load_fonts(struct st_wren_context *context);
int st_wren_host_set_screen_mode(struct st_wren_host *host,
	enum st_screen_mode mode);
void st_wren_host_external_screen_resize(struct st_wren_host *host,
	st_console_t *console, uint32_t columns, uint32_t rows);
enum st_wren_font_error {
	ST_WREN_FONT_UNAVAILABLE = 1,
	ST_WREN_FONT_INVALID_BITMAP,
	ST_WREN_FONT_UNSUPPORTED_CODEPAGE,
	ST_WREN_FONT_NO_SLOTS,
	ST_WREN_FONT_NOT_LOADED,
	ST_WREN_FONT_NATIVE_FAILURE,
};
/* message must have static storage duration; the Error retains it. */
bool st_wren_screen_status(WrenVM *vm, int status, const char *operation);
void st_wren_screen_result(WrenVM *vm, int status, const char *operation);
void st_wren_push_font_error(WrenVM *vm, enum st_wren_font_error code,
	int native_code, const char *message);
void st_wren_push_file_error(WrenVM *vm, int code, int native_error,
	const char *message);
bool st_wren_font_metadata(struct st_wren_context *context, uint16_t selector,
	uint16_t *identifier, enum st_codepage *codepage);
bool st_wren_font_identifier(struct st_wren_context *context, uint16_t selector,
	uint16_t *identifier);
uint16_t st_wren_font_selector(struct st_wren_context *context,
	uint16_t identifier);
char *st_wren_detect_url(st_console_t *console, const struct st_console_cell *cells,
	uint32_t width, uint32_t height, uint32_t x, uint32_t y);
bool st_wren_uri_openable(const char *uri);
char *st_wren_hover_uri(struct st_wren_context *context);
bool st_wren_link_mouse(struct st_wren_context *context,
	const struct st_wren_input_record *record);
int st_wren_mouse_drag_select(struct st_wren_context *context,
	bool rectangular);
int st_wren_input_context_init(WrenVM *vm);
void st_wren_input_context_dispose(WrenVM *vm);
int st_wren_input_dispatch(WrenVM *vm,
	const struct st_wren_input_record *event, bool *consumed);
int st_wren_input_dispatch_console_event(WrenVM *vm,
	const struct st_console_event *event, bool *consumed);
#define ST_WREN_NAMED_KEY_BASE UINT32_C(0x110000)
#define ST_WREN_QUIT_KEY UINT32_C(0x120000)
bool st_wren_input_key(const struct st_console_event *event,
	uint32_t *key, uint32_t *modifiers);
bool st_wren_input_native(WrenVM *vm, int slot, struct st_console_event *event);
int st_wren_connected_handle_key_record(struct st_wren_connected *connected,
	const struct st_wren_input_record *record, bool *consumed);
void st_wren_input_push_event(WrenVM *vm, int slot,
	const struct st_wren_input_record *record);
int st_wren_connected_handle_mouse_record(struct st_wren_connected *connected,
	const struct st_wren_input_record *record, bool *consumed);
int st_wren_connected_handle_physical_record(
	struct st_wren_connected *connected,
	const struct st_wren_input_record *record, bool *consumed);
int st_wren_wake_drain(WrenVM *vm);
int st_wren_wake_enqueue(WrenVM *vm, int fiber_slot, int value_slot,
	struct st_wren_operation *source);
WrenForeignMethodFn st_wren_sftp_bind_method(const char *class_name,
	bool is_static, const char *signature);
WrenForeignClassMethods st_wren_sftp_bind_class(const char *class_name);
WrenLoadModuleResult st_wren_load_module(WrenVM *vm, const char *name);
WrenForeignMethodFn st_wren_bind_method(WrenVM *vm, const char *module,
	const char *class_name, bool is_static, const char *signature);
WrenForeignClassMethods st_wren_bind_class(WrenVM *vm, const char *module,
	const char *class_name);
void st_wren_write_output(WrenVM *vm, const char *text);
void st_wren_report_error(WrenVM *vm, WrenErrorType type,
	const char *module, int line, const char *message);
int st_wren_interpret_event_scripts(WrenVM *vm, const char *event);
int st_wren_interpret_profile_scripts(WrenVM *vm,
	const struct st_profile *profile);

struct st_wren_context *st_wren_session_context(WrenVM *vm);
void st_wren_wire_dispose(struct st_wren_context *context);
int st_wren_operation_cancel(WrenVM *vm, struct st_wren_operation *operation);

bool st_wren_profile_push(WrenVM *vm, int slot, const struct st_profile *profile, bool read_only);
WrenForeignClassMethods st_wren_profile_bind_class(const char *class_name);
WrenForeignMethodFn st_wren_profile_bind_method(const char *class_name, bool is_static, const char *signature);
void st_wren_entry_cache_push(WrenVM *vm, const struct st_wren_bbs_record *record,
    uint64_t generation, uint64_t identity);

#endif

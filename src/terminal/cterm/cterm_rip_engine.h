/* Per-terminal RIP wire and graphics state. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef CTERM_RIP_ENGINE_H_
#define CTERM_RIP_ENGINE_H_

#include "cterm_rip_parser.h"
#include "cterm_rip_raster.h"

struct cterm_rip_mouse_field {
	struct cterm_rip_mouse_field *next;
	int x1;
	int y1;
	int x2;
	int y2;
	bool invert;
	bool reset_after;
	bool is_button;
	bool selected;
	struct cterm_rip_button_draw drawing;
	char *icon;
	char *label;
	char *command;
};

struct cterm_rip_user_var {
	struct cterm_rip_user_var *next;
	char name[13];
	char *value;
	int field_width;
	bool persistent;
};

struct cterm_rip_engine {
	struct cterm_rip_parser parser;
	struct cterm_rip_raster raster;
	struct cterm_rip_parser_sink host;
	struct {
		uint16_t font_identifier;
		uint32_t columns;
		uint32_t rows;
		uint32_t cell_width;
		uint32_t cell_height;
	} default_text;
	struct cterm_rip_mouse_field *mouse_fields;
	struct cterm_rip_mouse_field *saved_mouse_fields;
	struct cterm_rip_mouse_field *pressed_field;
	struct cterm_rip_mouse_field *tab_highlight;
	bool hotkeys_enabled;
	bool tab_enabled;
	char *graphics_click;
	char *text_click;
	struct {
		int x;
		int y;
		uint32_t buttons;
		bool text;
		bool active;
	} mouse_query;
	int last_mouse_x;
	int last_mouse_y;
	uint32_t last_mouse_buttons;
	struct cterm_rip_user_var *user_vars;
	size_t commands_seen;
	size_t scene_depth;
	struct cterm_rip_parser *received_scene_parser;
};

int cterm_rip_engine_init(struct cterm_rip_engine *engine,
    st_console_t *console, const struct cterm_rip_parser_sink *host);
void cterm_rip_engine_destroy(struct cterm_rip_engine *engine);
int cterm_rip_engine_feed(struct cterm_rip_engine *engine,
    const uint8_t *bytes, size_t size, size_t *consumed);
int cterm_rip_engine_flush(struct cterm_rip_engine *engine);
int cterm_rip_engine_receive_begin(struct cterm_rip_engine *engine);
int cterm_rip_engine_receive_data(struct cterm_rip_engine *engine,
    const uint8_t *bytes, size_t size);
int cterm_rip_engine_receive_end(struct cterm_rip_engine *engine, bool success);
void cterm_rip_engine_clear_mouse(struct cterm_rip_engine *engine);
void cterm_rip_engine_clear_tab(struct cterm_rip_engine *engine);
void cterm_rip_engine_next_tab(struct cterm_rip_engine *engine, bool forward);
int cterm_rip_engine_save_mouse(struct cterm_rip_engine *engine);
int cterm_rip_engine_restore_mouse(struct cterm_rip_engine *engine);
int cterm_rip_engine_add_mouse(struct cterm_rip_engine *engine,
    int x1, int y1, int x2, int y2, bool invert,
    bool reset_after, const char *command);
int cterm_rip_engine_add_button_mouse(struct cterm_rip_engine *engine,
    const struct cterm_rip_button_draw *drawing, bool selected,
    const char *command);

#endif

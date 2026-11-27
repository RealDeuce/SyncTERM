/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_DDOFX_SCRIPT_H_
#define ST_DDOFX_SCRIPT_H_

#include "st_ddofx.h"

struct st_ddofx_callback {
	char macro[101], reply[101];
	bool resource_text, stealth;
};

enum st_ddofx_callback_kind { DDO_EVENT = 1, DDO_KEY, DDO_MOUSE, DDO_FIELD
};

enum st_ddofx_group_kind {
	DDO_RADIO,
	DDO_CHECKBOX,
	DDO_TEXT_FIELD,
	DDO_LIST_BOX
};

enum { ST_DDOFX_FIELD_REPLY_MAX = 120 }; /* Two 25-byte affixes and up to 70 edited bytes. */
unsigned st_ddofx_text_field_count(const struct st_ddofx *);
bool st_ddofx_text_field_hotkey(struct st_ddofx *, unsigned, uint16_t);
bool st_ddofx_text_field_editing(const struct st_ddofx *);
bool st_ddofx_text_field_edit(struct st_ddofx *, uint16_t);
bool st_ddofx_next_list_item(struct st_ddofx *, unsigned id, unsigned *next);
bool st_ddofx_next_text_field(struct st_ddofx *, unsigned anchor, unsigned *next);
bool st_ddofx_text_field_value(const struct st_ddofx *, int32_t id, bool full, char out[ST_DDOFX_FIELD_REPLY_MAX+1]);
bool st_ddofx_field_callback(struct st_ddofx *, const struct st_ddofx_callback *, const char *reply);
unsigned st_ddofx_control_count(const struct st_ddofx *);
bool     st_ddofx_choice_mouse(struct st_ddofx *, enum st_ddofx_group_kind, unsigned slot, int x, int y);
bool     st_ddofx_choices_consume_key(struct st_ddofx *, unsigned slot, uint16_t word);
void     st_ddofx_execute_group(struct st_ddofx *, enum st_ddofx_group_kind, unsigned anchor, bool submitted_by_key);
bool     st_ddofx_next_choice(struct st_ddofx *, enum st_ddofx_group_kind, unsigned anchor, unsigned *next);

unsigned st_ddofx_button_count(const struct st_ddofx *);
bool     st_ddofx_button_key(struct st_ddofx *, unsigned, uint16_t);
bool     st_ddofx_button_pass(const struct st_ddofx *, unsigned);
bool     st_ddofx_button_callback(struct st_ddofx *, const struct st_ddofx_callback *, enum st_ddofx_callback_kind,
    unsigned);
void     st_ddofx_button_up(struct st_ddofx *, unsigned);
void st_ddofx_finish_field(struct st_ddofx *);
void st_ddofx_resume_mouse(struct st_ddofx *);
void     st_ddofx_callback_define(const struct st_ddofx *, struct st_ddofx_callback *, const char *, const char *);
bool     st_ddofx_callback_enabled(const struct st_ddofx *, const struct st_ddofx_callback *);
bool     st_ddofx_callback_start(struct st_ddofx *, const struct st_ddofx_callback *, enum st_ddofx_callback_kind);
void     st_ddofx_define_key(struct st_ddofx *, char *args);
void     st_ddofx_clear_keys(struct st_ddofx *);
int      st_ddofx_resume_key(struct st_ddofx *, const struct st_ddofx_sink *);
char    *st_ddofx_field(char **);
bool     st_ddofx_field_number(const struct st_ddofx *, char **, int32_t *);
void     st_ddofx_event_command(struct st_ddofx *, const char *, char *);
uint32_t st_ddofx_event_wait_ms(const struct st_ddofx *);
void     st_ddofx_event_poll(struct st_ddofx *);

void st_ddofx_query_reply(struct st_ddofx *, const struct st_ddofx_sink *, const char *, bool numeric);
bool st_ddofx_builtin(const struct st_ddofx *, const struct st_ddofx_sink *, const char *,
    char result[ST_DDOFX_BODY_MAX + 1], bool *numeric);
int  st_ddofx_parse(struct st_ddofx *, struct st_ddofx_framer *, const struct st_ddofx_sink *, const uint8_t *,
    size_t, size_t *);
bool st_ddofx_script_active(const struct st_ddofx *);
int  st_ddofx_script_step(struct st_ddofx *, const struct st_ddofx_sink *);
void st_ddofx_run_resource(struct st_ddofx *, const struct st_ddofx_sink *, const char *);
void st_ddofx_script_clear(struct st_ddofx *);
void st_ddofx_script_destroy(struct st_ddofx *);
void st_ddofx_script_command(struct st_ddofx *, const struct st_ddofx_sink *, char *);
bool st_ddofx_number(const struct st_ddofx *, const char *, int32_t *);
const char *st_ddofx_value(const struct st_ddofx *, const char *);
void st_ddofx_info(struct st_ddofx *, const struct st_ddofx_sink *, bool save, char *args);
size_t st_ddofx_control_value(const struct st_ddofx *, const char *, uint8_t *, size_t limit);
void st_ddofx_write_text_file(struct st_ddofx *, const struct st_ddofx_sink *, char *);
void st_ddofx_retrieve_text_file(struct st_ddofx *, const struct st_ddofx_sink *, char *);
void st_ddofx_retrieve_step(struct st_ddofx *, const struct st_ddofx_sink *);
void st_ddofx_info_reply(struct st_ddofx *, const struct st_ddofx_sink *, const uint8_t *, size_t, const char *error);
bool        st_ddofx_capture(struct st_ddofx *, const char *, bool numeric);

#endif

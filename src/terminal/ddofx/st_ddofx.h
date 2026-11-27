/* DDO F/X session wire interpreter. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_DDOFX_H_
#define ST_DDOFX_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "st_tracker.h"

#define ST_DDOFX_BODY_MAX 250

struct st_console_event;
struct st_controller_event;

struct st_ddofx_stick {
	uint32_t device;
	unsigned buttons, button_inputs;
	int x, y, hat_x, hat_y;
	unsigned dpad;
};

struct st_ddofx_framer {
	enum {
		ST_DDO_TEXT,
		ST_DDO_PREFIX,
		ST_DDO_BODY,
		ST_DDO_DISCARD
	} phase;

	char   body[ST_DDOFX_BODY_MAX + 1];
	size_t length;
};

struct st_ddofx {
	struct st_ddofx_graphics *graphics;
	struct st_ddofx_vga_font *vga_font;
	struct st_ddofx_lite_fade *lite_fade;
	bool                       mode_prefix, abort_script;
	uint32_t                 *saved_palette;
	FILE                     *page_files[3];
	struct {
		FILE *file;
		uint64_t remaining;
		uint8_t reply[ST_DDOFX_BODY_MAX];
		size_t reply_length;
	} retrieval;
	struct st_ddofx_framer   wire;
	struct st_ddofx_script  *script;
	struct st_ddofx_keys    *keys;
	struct st_ddofx_stick    sticks[2];
	struct st_ddofx_joystick *joystick;
	struct st_ddofx_events  *events;
	struct st_ddofx_samples *samples;
	uint8_t                 *queued;
	size_t                   queued_start, queued_end;
	uint64_t                 wait_until, fade_due;
	char                     music_name[ST_DDOFX_BODY_MAX + 1];
	char                     resource_prefix[12];
	struct st_tracker       *player;
	struct st_cmf           *cmf;
	unsigned                 volume;
	bool                     pointer_paused, controller_paused;
	bool                     enable_background_download, background_download, define_stealth;
	bool                     keys_locked, in_key, in_mouse;
	bool                     ignoring, background_paused, in_event;
};

struct st_ddofx_sink {
	void               *context;
	const char                   *cache_directory;
	int32_t                       (*available_memory)(void);
	void                          (*alert)(void *, const char *title, const char *message);
	void                         *alert_context;
	bool                mouse_disabled;
	st_audio_runtime_t *audio;
	const struct st_cterm_config *text_config;
	const struct cterminal *lite_terminal;
	FILE               *(*open_asset)(void *, const char *category, const char *client, const char *name);
	/* Owned BBSTHEME directory (NULL name) or checked loose file path. */
	char               *(*theme_path)(void *, const char *filename);
	bool                (*client_exists)(void *, const char *name);
	int                 (*text)(void *, const uint8_t *, size_t, size_t *);
	int                 (*resource_text)(void *, const uint8_t *, size_t, size_t *);
	int                 (*pass_key)(void *, const struct st_console_event *);
	void                (*reply)(void *, const uint8_t *, size_t);
};

int32_t  st_ddofx_speed_index(void);
int32_t  st_ddofx_available_memory(void);
int32_t  st_ddofx_available_drive(const char *directory);
void st_ddofx_controller(struct st_ddofx *, const struct st_controller_event *, bool consumed);
void st_ddofx_pointer_pause(struct st_ddofx *, bool);
void st_ddofx_mouse(struct st_ddofx *, const struct st_console_event *);
void st_ddofx_background_download(struct st_ddofx *, bool active);
void st_ddofx_mouse_cancel(struct st_ddofx *, uint32_t buttons);
bool st_ddofx_key(struct st_ddofx *, const struct st_console_event *);
uint32_t st_ddofx_wait_ms(const struct st_ddofx *);
bool     st_ddofx_busy(const struct st_ddofx *);
int      st_ddofx_poll(struct st_ddofx *, const struct st_ddofx_sink *);
void     st_ddofx_reset(struct st_ddofx *);
int      st_ddofx_feed(struct st_ddofx *, const struct st_ddofx_sink *, const uint8_t *, size_t, size_t *);

#endif

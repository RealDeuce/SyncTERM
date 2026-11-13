/* BIG application attributes versus installed BGI state (reference 27/54). */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_BIG_STATE_H_
#define ST_BIG_STATE_H_
#include "st_big_profile.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct st_big_state {
	struct {
		uint16_t hollow, colors[4], fill, write_mode;
		uint8_t marker, marker_size;
		uint8_t patterns[12][8];
	} application;
	struct {
		uint16_t state, driver, mode, max_mode;
		int error;
		uint32_t width, height;
		uint16_t viewport[4], clip, x, y, active_page, visual_page;
		uint16_t color, background, fill_style, fill_color;
		uint16_t ddo_x, ddo_y, device_x, device_y, line_distance;
		uint16_t bar_corners[4];
		uint8_t orientation;
		uint8_t device_color, ddo_line_width;
		uint8_t fill_low, device_fill_color, device_fill_style;
		uint8_t device_fill_user[8], fill_pattern[8], fill_pixels[64];
		uint16_t device_line_width;
		uint8_t palette_size, palette[16], fill_user[8];
		uint16_t line_style, line_user, line_mask, line_width;
		uint16_t font, direction, text_size, justify_x, justify_y, write_mode;
		/* The font0 shortcut does not dispose stroke resources/ratio owners. */
		uint16_t saved_ratio[4], effective_ratio[4];
	} bgi;
	/* Interpreter-frame values are initialized on entry, not by I/R. */
	uint16_t memory_page, active_page, visual_page;
	uint8_t local_palette[17];
	uint16_t saved_palette[16];
};

struct st_big_state_sink {
	void *context;
	/* These install/clear the native surface, publish indexed palette RGBs,
	 * and change physical pages. Views are borrowed; calls are serialized.
	 * Host failures stop at the reached prefix, with no state rollback. */
	int (*install)(void *, const struct st_big_mode *);
	int (*palette)(void *, size_t first, const uint8_t *, size_t count, bool bulk);
	int (*page)(void *, bool visual, uint16_t page);
};
extern const uint8_t st_big_fill_restore[12][8];
void st_big_state_init(struct st_big_state *);
void st_big_state_enter(struct st_big_state *);
/* Constructor/mode primitive: does not assign the application rez cache. */
int st_big_state_install(struct st_big_state *, unsigned resolution, const struct st_big_state_sink *);
/* Exact normal I/R caller operations. Cache points at the numeric reader's
 * cached resolution BYTE. Full WORD normalization and retained state apply. */
int st_big_state_initialize(struct st_big_state *, uint16_t selector,
	uint8_t *cache, const struct st_big_state_sink *);
int st_big_state_resolution(struct st_big_state *, uint16_t requested,
	uint8_t *cache, const struct st_big_state_sink *);
/* M's WORD normalization, installed mode first, application cache on return. */
int st_big_state_write_mode(struct st_big_state *, uint16_t requested);
/* C stores an application WORD after both reads; no installed BGI effect. */
int st_big_state_color(struct st_big_state *, uint16_t selector, uint16_t color);
/* H stores the full WORD, reloads it for the signed negative clamp, and
 * has no BGI operation or upper clamp. */
int st_big_state_hollow(struct st_big_state *, uint16_t requested);
/* Public setpalette, without S's caller gates: initialized/count/sign
 * rejection is silent, accepted low BYTE/background stores precede the
 * device callback. Raw cache values are not physical six-bit colours.
 * Checked cache bounds replace corrupt count-driven foreign accesses. */
int st_big_state_palette(struct st_big_state *, uint16_t index, uint16_t value,
	const struct st_big_state_sink *);
/* G resets interpreter page locals after its operation read in rez3.
 * G4/5/6 normalize/publish their local after the page read; physical page
 * state belongs to the sink, reached only by G5/6. */
void st_big_state_g_entry(struct st_big_state *, uint8_t resolution);
int st_big_state_g_page(struct st_big_state *, uint16_t operation, uint16_t requested,
	uint8_t resolution, const struct st_big_state_sink *);
#endif

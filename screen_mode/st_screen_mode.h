/* Authoritative SyncTERM screen-mode descriptions. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_SCREEN_MODE_H_
#define ST_SCREEN_MODE_H_

#include "st_console.h"
#include "st_profile.h"

#include <stddef.h>
#include <stdint.h>

enum st_screen_palette {
	ST_SCREEN_PALETTE_MONO,
	ST_SCREEN_PALETTE_GREYSCALE,
	ST_SCREEN_PALETTE_COLOUR,
	ST_SCREEN_PALETTE_C64,
	ST_SCREEN_PALETTE_ATARI,
	ST_SCREEN_PALETTE_PRESTEL,
	ST_SCREEN_PALETTE_ATARI_ST_16,
	ST_SCREEN_PALETTE_ATARI_ST_4,
	ST_SCREEN_PALETTE_ATARI_ST_2,
};

struct st_screen_mode_descriptor {
	enum st_screen_mode mode;
	const char *name;
	const char *ini_name;
	struct st_console_config console;
	enum st_screen_palette palette;
	uint8_t palette_count;
	const uint32_t *palette_rgb;
	uint8_t default_attribute;
	uint8_t font_selector;
};

size_t st_screen_mode_count(void);
const struct st_screen_mode_descriptor *st_screen_mode_at(size_t index);
const struct st_screen_mode_descriptor *st_screen_mode_get(
	enum st_screen_mode mode);
int st_screen_mode_custom(struct st_screen_mode_descriptor *result,
	uint32_t columns, uint32_t rows, uint32_t font_height,
	uint32_t aspect_width, uint32_t aspect_height);
/* Convert the descriptor's DOS attribute order to console palette indexes. */
void st_screen_mode_console_palette(
	const struct st_screen_mode_descriptor *mode, uint32_t result[16]);
/* Map DOS attribute nibbles to canonical console palette entries. */
void st_screen_mode_palette_mapping(
	const struct st_screen_mode_descriptor *mode, uint16_t result[16]);

#endif

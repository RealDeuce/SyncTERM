/* The supplied VGA-capable BIG 1.03 console/video profile. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_BIG_PROFILE_H_
#define ST_BIG_PROFILE_H_
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
struct st_console_font;
struct st_big_mode {
	unsigned resolution;
	uint8_t bios_mode;
	uint32_t width, height, columns, rows, cell_height;
	uint32_t aspect_width, aspect_height;
	size_t page_bytes; /* Per-plane page pitch, including retained padding. */
	uint16_t visual_page_bytes; /* BIOS CRT_LEN, independent of BGI pitch. */
};
const struct st_big_mode *st_big_mode(unsigned resolution);
/* Borrow exact existing font0 banks; BGI graphical text is a separate font. */
bool st_big_console_font(unsigned resolution, struct st_console_font *);
#endif

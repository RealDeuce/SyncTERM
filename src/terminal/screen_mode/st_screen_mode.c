/* Authoritative SyncTERM screen-mode descriptions. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_screen_mode.h"

#include "io_error.h"

#include <string.h>

#define MODE_CONFIG(columns_, rows_, cursor_start_, cursor_end_, cell_width_, \
	cell_height_, pixel_width_, pixel_height_, aspect_width_, aspect_height_, \
	blink_, flags_) \
	{ \
		.columns = (columns_), .rows = (rows_), \
		.cell_width = (cell_width_), .cell_height = (cell_height_), \
		.pixel_width = (pixel_width_), .pixel_height = (pixel_height_), \
		.default_cursor = { \
			.start = (cursor_start_), .end = (cursor_end_), \
			.range = (cell_height_), .visible = true, .blink = true, \
		}, \
		.wrap = true, .blink_mode = (blink_), .video_flags = (flags_), \
		.scaling_mode = ST_CONSOLE_SCALING_INTERNAL, \
		.scaling_policy = ST_PIXEL_SCALING_BLOCKY, \
		.aspect_width = (aspect_width_), .aspect_height = (aspect_height_), \
	}

#define PC_CONFIG(columns_, rows_, cursor_start_, cursor_end_, cell_height_, \
	pixel_width_, pixel_height_, aspect_width_, aspect_height_) \
	MODE_CONFIG(columns_, rows_, cursor_start_, cursor_end_, 8, cell_height_, \
		pixel_width_, pixel_height_, aspect_width_, aspect_height_, \
		ST_CONSOLE_BLINK_PC, 0)

static const uint32_t palette_values[][16] = {
	[ST_SCREEN_PALETTE_MONO] = {
		0x000000, 0xa8a8a8, 0xa8a8a8, 0xa8a8a8,
		0xa8a8a8, 0xa8a8a8, 0xa8a8a8, 0xa8a8a8,
		0xa8a8a8, 0xa8a8a8, 0xa8a8a8, 0xa8a8a8,
		0xa8a8a8, 0xa8a8a8, 0xa8a8a8, 0xa8a8a8,
	},
	[ST_SCREEN_PALETTE_GREYSCALE] = {
		0x000000, 0xa8a8a8, 0xa8a8a8, 0xa8a8a8,
		0xa8a8a8, 0xa8a8a8, 0xa8a8a8, 0xa8a8a8,
		0x545454, 0xffffff, 0xffffff, 0xffffff,
		0xffffff, 0xffffff, 0xffffff, 0xffffff,
	},
	[ST_SCREEN_PALETTE_COLOUR] = {
		0x000000, 0x0000a8, 0x00a800, 0x00a8a8,
		0xa80000, 0xa800a8, 0xa85400, 0xa8a8a8,
		0x545454, 0x5454ff, 0x54ff54, 0x54ffff,
		0xff5454, 0xff54ff, 0xffff54, 0xffffff,
	},
	[ST_SCREEN_PALETTE_C64] = {
		0x000000, 0xffffff, 0x813338, 0x75cec8,
		0x8e3c97, 0x56ac4d, 0x2e2c9b, 0xedf171,
		0x8e5029, 0x553800, 0xc46c71, 0x4a4a4a,
		0x7b7b7b, 0xa9ff9f, 0x706deb, 0xb2b2b2,
	},
	[ST_SCREEN_PALETTE_ATARI] = {
		0x005181, 0x60b7e7, 0x60b7e7, 0x60b7e7,
		0x60b7e7, 0x60b7e7, 0x60b7e7, 0x60b7e7,
		0x60b7e7, 0x60b7e7, 0x60b7e7, 0x60b7e7,
		0x60b7e7, 0x60b7e7, 0x60b7e7, 0x60b7e7,
	},
	[ST_SCREEN_PALETTE_PRESTEL] = {
		0x000000, 0x0000ff, 0x00ff00, 0x00ffff,
		0xff0000, 0xff00ff, 0xffff00, 0xffffff,
		0x000000, 0x0000ff, 0x00ff00, 0x00ffff,
		0xff0000, 0xff00ff, 0xffff00, 0xffffff,
	},
	[ST_SCREEN_PALETTE_ATARI_ST_16] = {
		0xffffff, 0xff0000, 0x00ff00, 0xffff00,
		0x0000ff, 0xff00ff, 0x00ffff, 0xb6b6b6,
		0x6d6d6d, 0xff6d6d, 0x6dff6d, 0xffff6d,
		0x6d6dff, 0xff6dff, 0x6dffff, 0x000000,
	},
	[ST_SCREEN_PALETTE_ATARI_ST_4] = {
		0xffffff, 0xff0000, 0x00ff00, 0x000000,
		0xffffff, 0xff0000, 0x00ff00, 0x000000,
		0xffffff, 0xff0000, 0x00ff00, 0x000000,
		0xffffff, 0xff0000, 0x00ff00, 0x000000,
	},
	[ST_SCREEN_PALETTE_ATARI_ST_2] = {
		0xffffff, 0x000000, 0xffffff, 0x000000,
		0xffffff, 0x000000, 0xffffff, 0x000000,
		0xffffff, 0x000000, 0xffffff, 0x000000,
		0xffffff, 0x000000, 0xffffff, 0x000000,
	},
};

#define MODE(mode_, name_, ini_, config_, palette_, count_, attr_, font_) \
	[mode_] = { \
		.mode = (mode_), .name = (name_), .ini_name = (ini_), \
		.console = config_, .palette = (palette_), \
		.palette_rgb = palette_values[(palette_)], \
		.palette_count = (count_), .default_attribute = (attr_), \
		.font_selector = (font_), \
	}

static const struct st_screen_mode_descriptor modes[] = {
	MODE(ST_SCREEN_MODE_CURRENT, "Current", "Current",
	    PC_CONFIG(80, 25, 14, 15, 16, 640, 400, 4, 3),
	    ST_SCREEN_PALETTE_COLOUR, 16, 7, 0),
	MODE(ST_SCREEN_MODE_80X25, "80x25", "80x25",
	    PC_CONFIG(80, 25, 14, 15, 16, 640, 400, 4, 3),
	    ST_SCREEN_PALETTE_COLOUR, 16, 7, 0),
	MODE(ST_SCREEN_MODE_LCD_80X25, "LCD 80x25", "LCD80x25",
	    PC_CONFIG(80, 25, 14, 15, 16, 640, 400, 8, 5),
	    ST_SCREEN_PALETTE_COLOUR, 16, 7, 0),
	MODE(ST_SCREEN_MODE_80X28, "80x28", "80x28",
	    PC_CONFIG(80, 28, 12, 13, 14, 640, 392, 4, 3),
	    ST_SCREEN_PALETTE_COLOUR, 16, 7, 0),
	MODE(ST_SCREEN_MODE_80X30, "80x30", "80x30",
	    PC_CONFIG(80, 30, 14, 15, 16, 640, 480, 4, 3),
	    ST_SCREEN_PALETTE_COLOUR, 16, 7, 0),
	MODE(ST_SCREEN_MODE_80X43, "80x43", "80x43",
	    PC_CONFIG(80, 43, 7, 7, 8, 640, 350, 4, 3),
	    ST_SCREEN_PALETTE_COLOUR, 16, 7, 0),
	MODE(ST_SCREEN_MODE_80X50, "80x50", "80x50",
	    PC_CONFIG(80, 50, 7, 7, 8, 640, 400, 4, 3),
	    ST_SCREEN_PALETTE_COLOUR, 16, 7, 0),
	MODE(ST_SCREEN_MODE_80X60, "80x60", "80x60",
	    PC_CONFIG(80, 60, 7, 7, 8, 640, 480, 4, 3),
	    ST_SCREEN_PALETTE_COLOUR, 16, 7, 0),
	MODE(ST_SCREEN_MODE_132X37, "132x37 (16:9)", "132x37",
	    PC_CONFIG(132, 37, 14, 15, 16, 1056, 592, 16, 9),
	    ST_SCREEN_PALETTE_COLOUR, 16, 7, 0),
	MODE(ST_SCREEN_MODE_132X52, "132x52 (5:4)", "132x52",
	    PC_CONFIG(132, 52, 14, 15, 16, 1056, 832, 5, 4),
	    ST_SCREEN_PALETTE_COLOUR, 16, 7, 0),
	MODE(ST_SCREEN_MODE_132X25, "132x25", "132x25",
	    PC_CONFIG(132, 25, 14, 15, 16, 1056, 400, 4, 3),
	    ST_SCREEN_PALETTE_COLOUR, 16, 7, 0),
	MODE(ST_SCREEN_MODE_132X28, "132x28", "132x28",
	    PC_CONFIG(132, 28, 12, 13, 14, 1056, 392, 4, 3),
	    ST_SCREEN_PALETTE_COLOUR, 16, 7, 0),
	MODE(ST_SCREEN_MODE_132X30, "132x30", "132x30",
	    PC_CONFIG(132, 30, 14, 15, 16, 1056, 480, 4, 3),
	    ST_SCREEN_PALETTE_COLOUR, 16, 7, 0),
	MODE(ST_SCREEN_MODE_132X34, "132x34", "132x34",
	    PC_CONFIG(132, 34, 12, 13, 14, 1056, 476, 4, 3),
	    ST_SCREEN_PALETTE_COLOUR, 16, 7, 0),
	MODE(ST_SCREEN_MODE_132X43, "132x43", "132x43",
	    PC_CONFIG(132, 43, 7, 7, 8, 1056, 350, 4, 3),
	    ST_SCREEN_PALETTE_COLOUR, 16, 7, 0),
	MODE(ST_SCREEN_MODE_132X50, "132x50", "132x50",
	    PC_CONFIG(132, 50, 7, 7, 8, 1056, 400, 4, 3),
	    ST_SCREEN_PALETTE_COLOUR, 16, 7, 0),
	MODE(ST_SCREEN_MODE_132X60, "132x60", "132x60",
	    PC_CONFIG(132, 60, 7, 7, 8, 1056, 480, 4, 3),
	    ST_SCREEN_PALETTE_COLOUR, 16, 7, 0),
	MODE(ST_SCREEN_MODE_C64, "C64", "C64",
	    MODE_CONFIG(40, 25, 0, 7, 8, 8, 320, 200, 6, 5,
		ST_CONSOLE_BLINK_COMMODORE,
		ST_CONSOLE_VIDEO_BRIGHT_BACKGROUND | ST_CONSOLE_VIDEO_NO_BLINK),
	    ST_SCREEN_PALETTE_C64, 16, 0x6e, 33),
	MODE(ST_SCREEN_MODE_C128_40, "C128 (40col)", "C128-40col",
	    MODE_CONFIG(40, 25, 0, 7, 8, 8, 320, 200, 6, 5,
		ST_CONSOLE_BLINK_COMMODORE,
		ST_CONSOLE_VIDEO_BRIGHT_BACKGROUND | ST_CONSOLE_VIDEO_NO_BLINK),
	    ST_SCREEN_PALETTE_C64, 16, 0xbd, 35),
	MODE(ST_SCREEN_MODE_C128_80, "C128 (80col)", "C128-80col",
	    MODE_CONFIG(80, 25, 0, 7, 8, 8, 640, 200, 4, 3,
		ST_CONSOLE_BLINK_COMMODORE,
		ST_CONSOLE_VIDEO_BRIGHT_BACKGROUND | ST_CONSOLE_VIDEO_NO_BLINK),
	    ST_SCREEN_PALETTE_COLOUR, 16, 7, 35),
	MODE(ST_SCREEN_MODE_ATARI, "Atari", "Atari",
	    MODE_CONFIG(40, 24, 0, 7, 8, 8, 320, 192, 4, 3,
		ST_CONSOLE_BLINK_ATARI, 0),
	    ST_SCREEN_PALETTE_ATARI, 2, 7, 36),
	MODE(ST_SCREEN_MODE_ATARI_XEP80, "Atari XEP80", "Atari-XEP80",
	    MODE_CONFIG(80, 25, 0, 15, 8, 16, 640, 400, 4, 3,
		ST_CONSOLE_BLINK_ATARI, 0),
	    ST_SCREEN_PALETTE_GREYSCALE, 4, 7, 0),
	[ST_SCREEN_MODE_CUSTOM] = {
		.mode = ST_SCREEN_MODE_CUSTOM, .name = "Custom", .ini_name = "Custom",
	},
	MODE(ST_SCREEN_MODE_EGA_80X25, "EGA 80x25", "EGA80x25",
	    PC_CONFIG(80, 25, 12, 13, 14, 640, 350, 4, 3),
	    ST_SCREEN_PALETTE_COLOUR, 16, 7, 0),
	MODE(ST_SCREEN_MODE_VGA_80X25, "VGA 80x25", "VGA80x25",
	    MODE_CONFIG(80, 25, 14, 15, 9, 16, 720, 400, 4, 3,
		ST_CONSOLE_BLINK_PC, ST_CONSOLE_VIDEO_EXPAND
		| ST_CONSOLE_VIDEO_LINE_GRAPHICS_EXPAND),
	    ST_SCREEN_PALETTE_COLOUR, 16, 7, 0),
	MODE(ST_SCREEN_MODE_PRESTEL, "Prestel", "Prestel",
	    MODE_CONFIG(40, 25, 14, 15, 12, 20, 480, 500, 3077, 2400,
		ST_CONSOLE_BLINK_PRESTEL, 0),
	    ST_SCREEN_PALETTE_PRESTEL, 8, 7, 43),
	MODE(ST_SCREEN_MODE_BEEB, "BBC Micro Mode 7", "BBCMicro7",
	    MODE_CONFIG(40, 25, 14, 15, 12, 20, 480, 500, 3077, 2400,
		ST_CONSOLE_BLINK_PRESTEL, 0),
	    ST_SCREEN_PALETTE_PRESTEL, 8, 7, 43),
	MODE(ST_SCREEN_MODE_ATARIST_40X25, "Atari ST 40x25", "AtariST40x25",
	    MODE_CONFIG(40, 25, 0, 7, 8, 8, 320, 200, 4, 3,
		ST_CONSOLE_BLINK_ATARI_ST,
		ST_CONSOLE_VIDEO_BRIGHT_BACKGROUND | ST_CONSOLE_VIDEO_NO_BLINK),
	    ST_SCREEN_PALETTE_ATARI_ST_16, 16, 15, 0),
	MODE(ST_SCREEN_MODE_ATARIST_80X25, "Atari ST 80x25", "AtariST80x25",
	    MODE_CONFIG(80, 25, 0, 7, 8, 8, 640, 200, 4, 3,
		ST_CONSOLE_BLINK_ATARI_ST,
		ST_CONSOLE_VIDEO_BRIGHT_BACKGROUND | ST_CONSOLE_VIDEO_NO_BLINK),
	    ST_SCREEN_PALETTE_ATARI_ST_4, 4, 15, 0),
	MODE(ST_SCREEN_MODE_ATARIST_80X25_MONO, "Atari ST 80x25 Mono",
	    "AtariST80x25Mono",
	    MODE_CONFIG(80, 25, 0, 15, 8, 16, 640, 400, 4, 3,
		ST_CONSOLE_BLINK_ATARI_ST,
		ST_CONSOLE_VIDEO_BRIGHT_BACKGROUND | ST_CONSOLE_VIDEO_NO_BLINK),
	    ST_SCREEN_PALETTE_ATARI_ST_2, 2, 15, 0),
};

size_t
st_screen_mode_count(void)
{
	return sizeof(modes) / sizeof(modes[0]);
}

void
st_screen_mode_profile_palette(const struct st_screen_mode_descriptor *mode,
	const uint32_t *palette, size_t count, uint32_t result[16])
{
	uint32_t expanded[16];
	if (palette == NULL || count == 0 || count > 16 || result == NULL)
		return;
	for (size_t i = 0; i < 16; ++i)
		expanded[i] = palette[i % count];
	for (size_t i = 0; i < 16; ++i) {
		size_t index = i;
		switch (mode == NULL ? ST_SCREEN_PALETTE_COLOUR : mode->palette) {
		case ST_SCREEN_PALETTE_MONO:
		case ST_SCREEN_PALETTE_ATARI: index = i != 0; break;
		case ST_SCREEN_PALETTE_GREYSCALE:
			index = i == 0 ? 0 : i < 8 ? 1 : i == 8 ? 2 : 3;
			break;
		case ST_SCREEN_PALETTE_PRESTEL: index = i % 8; break;
		case ST_SCREEN_PALETTE_ATARI_ST_4: index = i % 4; break;
		case ST_SCREEN_PALETTE_ATARI_ST_2: index = i % 2; break;
		default: break;
		}
		result[i] = expanded[index];
	}
}

const struct st_screen_mode_descriptor *
st_screen_mode_at(size_t index)
{
	return index < st_screen_mode_count() ? &modes[index] : NULL;
}

const struct st_screen_mode_descriptor *
st_screen_mode_get(enum st_screen_mode mode)
{
	if (mode < ST_SCREEN_MODE_CURRENT || (size_t)mode >= st_screen_mode_count())
		return NULL;
	return st_screen_mode_at((size_t)mode);
}

int
st_screen_mode_custom(struct st_screen_mode_descriptor *result,
	uint32_t columns, uint32_t rows, uint32_t font_height,
	uint32_t aspect_width, uint32_t aspect_height)
{
	const struct st_screen_mode_descriptor *base;
	uint32_t thickness;

	if (result == NULL || columns == 0 || rows == 0 || font_height == 0
	    || aspect_width == 0 || aspect_height == 0)
		return IO_ERROR_INVALID_ARGUMENT;
	base = st_screen_mode_get(ST_SCREEN_MODE_80X25);
	*result = *base;
	result->mode = ST_SCREEN_MODE_CUSTOM;
	result->name = modes[ST_SCREEN_MODE_CUSTOM].name;
	result->ini_name = modes[ST_SCREEN_MODE_CUSTOM].ini_name;
	result->console.columns = columns;
	result->console.rows = rows;
	result->console.cell_height = font_height;
	result->console.pixel_width = 0;
	result->console.pixel_height = 0;
	result->console.aspect_width = aspect_width;
	result->console.aspect_height = aspect_height;
	thickness = font_height > 8 ? 2 : 1;
	result->console.default_cursor = (struct st_console_custom_cursor){
		.start = font_height - thickness,
		.end = font_height - 1,
		.range = font_height,
		.visible = true,
		.blink = true,
	};
	return 0;
}

void
st_screen_mode_console_palette(const struct st_screen_mode_descriptor *mode,
	uint32_t result[16])
{
	static const uint8_t native_index[16] = {
		0, 4, 2, 6, 1, 5, 3, 7, 8, 12, 10, 14, 9, 13, 11, 15,
	};

	if (mode == NULL || result == NULL || mode->palette_rgb == NULL)
		return;
	for (size_t i = 0; i < 16; ++i)
		result[native_index[i]] = mode->palette_rgb[i];
}

void
st_screen_mode_palette_mapping(const struct st_screen_mode_descriptor *mode,
	uint16_t result[16])
{
	static const uint16_t mappings[][16] = {
		[ST_SCREEN_PALETTE_MONO] = {
			0, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
		},
		[ST_SCREEN_PALETTE_GREYSCALE] = {
			0, 7, 7, 7, 7, 7, 7, 7, 8, 15, 15, 15, 15, 15, 15, 15,
		},
		[ST_SCREEN_PALETTE_COLOUR] = {
			0, 4, 2, 6, 1, 5, 3, 7, 8, 12, 10, 14, 9, 13, 11, 15,
		},
		[ST_SCREEN_PALETTE_C64] = {
			256, 257, 258, 259, 260, 261, 262, 263,
			264, 265, 266, 267, 268, 269, 270, 271,
		},
		[ST_SCREEN_PALETTE_ATARI] = {
			272, 273, 273, 273, 273, 273, 273, 273,
			273, 273, 273, 273, 273, 273, 273, 273,
		},
		[ST_SCREEN_PALETTE_PRESTEL] = {
			0, 21, 46, 51, 196, 201, 226, 15,
			0, 21, 46, 51, 196, 201, 226, 15,
		},
		[ST_SCREEN_PALETTE_ATARI_ST_16] = {
			15, 196, 46, 226, 21, 201, 51, 274,
			275, 276, 277, 278, 279, 280, 281, 0,
		},
		[ST_SCREEN_PALETTE_ATARI_ST_4] = {
			15, 196, 46, 0, 15, 196, 46, 0,
			15, 196, 46, 0, 15, 196, 46, 0,
		},
		[ST_SCREEN_PALETTE_ATARI_ST_2] = {
			15, 0, 15, 0, 15, 0, 15, 0,
			15, 0, 15, 0, 15, 0, 15, 0,
		},
	};

	if (mode == NULL || result == NULL
	    || (unsigned)mode->palette
	    >= sizeof(mappings) / sizeof(mappings[0]))
		return;
	memcpy(result, mappings[mode->palette], sizeof(mappings[0]));
}

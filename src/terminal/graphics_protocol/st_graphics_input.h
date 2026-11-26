/* Connected graphics keyboard and pointer dispatch. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_GRAPHICS_INPUT_H_
#define ST_GRAPHICS_INPUT_H_

#include "st_cterm.h"

/* Give the attached graphics device first refusal, then apply emulation. */
bool st_graphics_handle_key(st_cterm_t *, uint16_t code, uint32_t modifiers);
bool st_graphics_handle_text_key(st_cterm_t *, uint32_t scalar);
bool st_graphics_handle_codepage_key(st_cterm_t *, uint8_t byte);
bool st_graphics_handle_doorway_key(st_cterm_t *, uint8_t scan);
bool st_graphics_handle_mouse(st_cterm_t *, const struct st_console_event *);

void st_graphics_sync_mouse_buttons(st_cterm_t *, uint32_t buttons);

#endif

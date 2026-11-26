/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP154_INTERNAL_H
#define ST_RIP154_INTERNAL_H
#include "cterm_rip_engine.h"
#include "st_rip154.h"

struct st_rip154 {
	st_cterm_t                      *terminal;
	struct cterm_rip_engine          engine;
	const char                      *rip_directory;
	st_cterm_rip_prompt_callback_t   rip_prompt;
	void                            *rip_prompt_context;
	st_cterm_rip_choice_callback_t   rip_choice;
	void                            *rip_choice_context;
	st_cterm_rip_transfer_callback_t rip_transfer;
	void                            *rip_transfer_context;
	bool                             rip_text_disabled;
	int                              rip_text_font_style;

	struct {
		int                          x;
		int                          y;
		int                          left;
		int                          top;
		int                          right;
		int                          bottom;
		int                          font_style;
		bool                         wrap;
		bool                         disabled;
		bool                         cursor_visible;
		enum st_console_cursor_shape cursor_shape;
		uint8_t                      attr;
		uint32_t                     foreground;
		uint32_t                     background;
	} rip_saved_text;

	uint16_t rip_text_font_identifiers[6];
	bool     rip_text_font_registered[6];
};
#endif

/* Internal ownership helpers shared by the settings model and store. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_SETTINGS_INTERNAL_H_
#define ST_SETTINGS_INTERNAL_H_

#include "st_settings.h"

bool st_settings_fonts_copy(st_settings_font_list_t *result,
	st_settings_font_list_t source);
void st_settings_fonts_free(st_settings_font_list_t *fonts);
bool st_settings_fonts_equal(st_settings_font_list_t left,
	st_settings_font_list_t right);
int st_settings_font_draft_replace(struct st_settings_font_draft *draft,
	st_settings_font_list_t fonts);

#endif

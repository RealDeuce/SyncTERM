/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_profile.h"
#include "st_builtin_fonts.h"
static const struct st_big_mode modes[] = {
	{1,0x0e,640,200,80,25,8,4,3,0x4000},
	{2,0x10,640,350,80,25,14,4,3,0x8000},
	{3,0x12,640,480,80,30,16,4,3,0x9600},
};
const struct st_big_mode *st_big_mode(unsigned resolution)
{
	return resolution >= 1 && resolution <= 3 ? &modes[resolution-1] : NULL;
}
bool st_big_console_font(unsigned resolution, struct st_console_font *font)
{
	const struct st_big_mode *mode = st_big_mode(resolution);
	return mode != NULL && st_builtin_font_get(0,8,mode->cell_height,font);
}

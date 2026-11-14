/* BIG p's negative clamp and original gotoxy byte/window validation. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_console.h"
#include <stddef.h>
bool st_big_console_position(const uint8_t bounds[4], uint16_t x, uint16_t y,
    uint8_t position[2])
{
	if (bounds==NULL || position==NULL) return false;
	if ((x&0x8000u)!=0) x=0;
	if ((y&0x8000u)!=0) y=0;
	uint8_t column=(uint8_t)((uint8_t)x-1u+bounds[0]);
	uint8_t row=(uint8_t)((uint8_t)y-1u+bounds[1]);
	if (column<bounds[0] || column>bounds[2] || row<bounds[1] || row>bounds[3]) return false;
	position[0]=column; position[1]=row;
	return true;
}

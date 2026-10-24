/* Libretro keyboard translation into the shared console event contract. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_LIBRETRO_INPUT_H_
#define ST_LIBRETRO_INPUT_H_
#include "st_console.h"
#include <stddef.h>
size_t st_libretro_key_events(struct st_console_event events[2], bool down,
	unsigned key, uint32_t character, uint16_t modifiers, bool repeat);
#endif

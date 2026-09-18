/* X11 input translation used by the X11 console backend. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_CONSOLE_X11_KEYS_H_
#define ST_CONSOLE_X11_KEYS_H_

#include "st_console.h"

#include <X11/Xlib.h>
#include <stdint.h>

uint16_t st_console_x11_evdev_from_keysym(KeySym keysym);
uint16_t st_console_x11_evdev_from_keyname(const char name[4]);
uint16_t st_console_x11_key_from_keysym(KeySym keysym);
uint32_t st_console_x11_modifiers(unsigned int state);

#endif

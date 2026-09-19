/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

/* Compile-only audit: compare the runtime loader with the Xlib declaration. */
#include "../console_x11.c"

_Static_assert(_Generic(&XkbFreeKeyboard,
	void (*)(XkbDescPtr, unsigned int, Bool): 1, default: 0),
	"Xlib declares XkbFreeKeyboard with a void return type");
_Static_assert(_Generic((x11_xkb_free_keyboard_fn)0,
	void (*)(XkbDescPtr, unsigned int, Bool): 1, default: 0),
	"loader typedef must match XkbFreeKeyboard");
_Static_assert(_Generic(((struct x11_api *)0)->xkb_free_keyboard,
	void (*)(XkbDescPtr, unsigned int, Bool): 1, default: 0),
	"loader member must match XkbFreeKeyboard");

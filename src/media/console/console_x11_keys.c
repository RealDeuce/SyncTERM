/* Locale-independent X11 key translation. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "console_x11_keys.h"

#include <X11/keysym.h>

#include <string.h>

static int
keyname_number(const char *key, const char *prefix)
{
	size_t length = strlen(prefix);

	if (strncmp(key, prefix, length) != 0
	    || key[length] < '0' || key[length] > '9'
	    || key[length + 1] < '0' || key[length + 1] > '9'
	    || key[length + 2] != '\0')
		return 0;
	return (key[length] - '0') * 10 + key[length + 1] - '0';
}

uint16_t
st_console_x11_evdev_from_keyname(const char name[4])
{
	static const uint16_t ae[] = {
		0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,
	};
	static const uint16_t ad[] = {
		0, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27,
	};
	static const uint16_t ac[] = {
		0, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
	};
	static const uint16_t ab[] = {
		0, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53,
	};
	static const struct {
		char name[5];
		uint16_t evdev;
	} fixed[] = {
		{"ESC", 1}, {"BKSP", 14}, {"TAB", 15}, {"RTRN", 28},
		{"LCTL", 29}, {"RCTL", 97}, {"TLDE", 41}, {"LFSH", 42},
		{"RTSH", 54}, {"BKSL", 43}, {"KPMU", 55}, {"LALT", 56},
		{"RALT", 100}, {"SPCE", 57}, {"CAPS", 58}, {"NMLK", 69},
		{"SCLK", 70}, {"KP7", 71}, {"KP8", 72}, {"KP9", 73},
		{"KPSU", 74}, {"KP4", 75}, {"KP5", 76}, {"KP6", 77},
		{"KPAD", 78}, {"KP1", 79}, {"KP2", 80}, {"KP3", 81},
		{"KP0", 82}, {"KPDL", 83}, {"LSGT", 86}, {"KPEN", 96},
		{"KPDV", 98}, {"PRSC", 99}, {"HOME", 102}, {"UP", 103},
		{"PGUP", 104}, {"LEFT", 105}, {"RGHT", 106}, {"END", 107},
		{"DOWN", 108}, {"PGDN", 109}, {"INS", 110}, {"DELE", 111},
		{"PAUS", 119}, {"LWIN", 125}, {"RWIN", 126}, {"MENU", 139},
	};
	char key[5];
	int number;

	memcpy(key, name, 4);
	key[4] = '\0';
	for (int i = 3; i >= 0 && (key[i] == '\0' || key[i] == ' '); --i)
		key[i] = '\0';
	number = keyname_number(key, "AE");
	if (number > 0 && (size_t)number < sizeof(ae) / sizeof(ae[0]))
		return ae[number];
	number = keyname_number(key, "AD");
	if (number > 0 && (size_t)number < sizeof(ad) / sizeof(ad[0]))
		return ad[number];
	number = keyname_number(key, "AC");
	if (number > 0 && (size_t)number < sizeof(ac) / sizeof(ac[0]))
		return ac[number];
	number = keyname_number(key, "AB");
	if (number > 0 && (size_t)number < sizeof(ab) / sizeof(ab[0]))
		return ab[number];
	number = keyname_number(key, "FK");
	if (number >= 1 && number <= 10)
		return (uint16_t)(58 + number);
	if (number >= 11 && number <= 12)
		return (uint16_t)(87 + number - 11);
	if (number >= 13 && number <= 24)
		return (uint16_t)(183 + number - 13);
	for (size_t i = 0; i < sizeof(fixed) / sizeof(fixed[0]); ++i) {
		if (strcmp(key, fixed[i].name) == 0)
			return fixed[i].evdev;
	}
	return 0;
}

uint32_t
st_console_x11_modifiers(unsigned int state)
{
	uint32_t result = 0;

	if ((state & ShiftMask) != 0)
		result |= ST_CONSOLE_MODIFIER_SHIFT;
	if ((state & ControlMask) != 0)
		result |= ST_CONSOLE_MODIFIER_CONTROL;
	if ((state & Mod1Mask) != 0)
		result |= ST_CONSOLE_MODIFIER_ALT;
	if ((state & Mod4Mask) != 0)
		result |= ST_CONSOLE_MODIFIER_GUI;
	if ((state & LockMask) != 0)
		result |= ST_CONSOLE_MODIFIER_CAPS_LOCK;
	if ((state & Mod2Mask) != 0)
		result |= ST_CONSOLE_MODIFIER_NUM_LOCK;
	return result;
}

uint16_t
st_console_x11_key_from_keysym(KeySym keysym)
{
	if (keysym >= XK_F1 && keysym <= XK_F12)
		return (uint16_t)(ST_CONSOLE_KEY_F1 + keysym - XK_F1);
	switch (keysym) {
	case XK_BackSpace: return 8;
	case XK_Tab:
	case XK_ISO_Left_Tab: return 9;
	case XK_Return:
	case XK_KP_Enter: return 13;
	case XK_Escape: return 27;
	case XK_Home:
	case XK_KP_Home: return ST_CONSOLE_KEY_HOME;
	case XK_Left:
	case XK_KP_Left: return ST_CONSOLE_KEY_LEFT;
	case XK_Up:
	case XK_KP_Up: return ST_CONSOLE_KEY_UP;
	case XK_Right:
	case XK_KP_Right: return ST_CONSOLE_KEY_RIGHT;
	case XK_Down:
	case XK_KP_Down: return ST_CONSOLE_KEY_DOWN;
	case XK_Prior:
	case XK_KP_Prior: return ST_CONSOLE_KEY_PAGE_UP;
	case XK_Next:
	case XK_KP_Next: return ST_CONSOLE_KEY_PAGE_DOWN;
	case XK_End:
	case XK_KP_End: return ST_CONSOLE_KEY_END;
	case XK_Insert:
	case XK_KP_Insert: return ST_CONSOLE_KEY_INSERT;
	case XK_Delete:
	case XK_KP_Delete: return ST_CONSOLE_KEY_DELETE;
	default: return 0;
	}
}

uint16_t
st_console_x11_evdev_from_keysym(KeySym keysym)
{
	if (keysym >= XK_F1 && keysym <= XK_F10)
		return (uint16_t)(59 + keysym - XK_F1);
	if (keysym >= XK_F11 && keysym <= XK_F12)
		return (uint16_t)(87 + keysym - XK_F11);
	if (keysym >= XK_1 && keysym <= XK_9)
		return (uint16_t)(2 + keysym - XK_1);
	if (keysym >= XK_a && keysym <= XK_z) {
		static const uint16_t letters[] = {
			30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38, 50,
			49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44,
		};
		return letters[keysym - XK_a];
	}
	if (keysym >= XK_A && keysym <= XK_Z)
		return st_console_x11_evdev_from_keysym(keysym - XK_A + XK_a);
	if (keysym >= XK_KP_0 && keysym <= XK_KP_9) {
		static const uint16_t keypad[] = {
			82, 79, 80, 81, 75, 76, 77, 71, 72, 73,
		};
		return keypad[keysym - XK_KP_0];
	}
	switch (keysym) {
	case XK_Escape: return 1;
	case XK_0: return 11;
	case XK_minus: return 12;
	case XK_equal: return 13;
	case XK_BackSpace: return 14;
	case XK_Tab:
	case XK_ISO_Left_Tab: return 15;
	case XK_bracketleft: return 26;
	case XK_bracketright: return 27;
	case XK_Return:
	case XK_KP_Enter: return 28;
	case XK_Control_L: return 29;
	case XK_semicolon: return 39;
	case XK_apostrophe: return 40;
	case XK_grave: return 41;
	case XK_Shift_L: return 42;
	case XK_backslash: return 43;
	case XK_comma: return 51;
	case XK_period: return 52;
	case XK_slash: return 53;
	case XK_Shift_R: return 54;
	case XK_KP_Multiply: return 55;
	case XK_Alt_L:
	case XK_Meta_L: return 56;
	case XK_space: return 57;
	case XK_Caps_Lock: return 58;
	case XK_Num_Lock: return 69;
	case XK_Scroll_Lock: return 70;
	case XK_KP_7:
	case XK_KP_Home: return 71;
	case XK_KP_8:
	case XK_KP_Up: return 72;
	case XK_KP_9:
	case XK_KP_Prior: return 73;
	case XK_KP_Subtract: return 74;
	case XK_KP_4:
	case XK_KP_Left: return 75;
	case XK_KP_5:
	case XK_Begin:
	case XK_KP_Begin: return 76;
	case XK_KP_6:
	case XK_KP_Right: return 77;
	case XK_KP_Add: return 78;
	case XK_KP_1:
	case XK_KP_End: return 79;
	case XK_KP_2:
	case XK_KP_Down: return 80;
	case XK_KP_3:
	case XK_KP_Next: return 81;
	case XK_KP_0:
	case XK_KP_Insert: return 82;
	case XK_KP_Decimal:
	case XK_KP_Delete: return 83;
	case XK_Control_R: return 97;
	case XK_KP_Divide: return 98;
	case XK_Print: return 99;
	case XK_Alt_R:
	case XK_Meta_R: return 100;
	/* Numeric entry uses physical identity: dedicated navigation keys
	 * must remain distinct from their keypad aliases. */
	case XK_Home: return 102;
	case XK_Up: return 103;
	case XK_Prior: return 104;
	case XK_Left: return 105;
	case XK_Right: return 106;
	case XK_End: return 107;
	case XK_Down: return 108;
	case XK_Next: return 109;
	case XK_Insert: return 110;
	case XK_Delete: return 111;
	case XK_Pause: return 119;
	case XK_Super_L: return 125;
	case XK_Super_R: return 126;
	default: return 0;
	}
}

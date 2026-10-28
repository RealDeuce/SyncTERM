/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_libretro_input.h"
#include "libretro.h"

static uint16_t
physical_key(unsigned key)
{
	static const uint16_t letters[] = {
		30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38, 50,
		49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44,
	};
	if (key >= RETROK_a && key <= RETROK_z) return letters[key - RETROK_a];
	if (key >= RETROK_1 && key <= RETROK_9) return (uint16_t)(key - RETROK_1 + 2);
	if (key >= RETROK_KP0 && key <= RETROK_KP9) {
		static const uint16_t keypad[] = {82, 79, 80, 81, 75, 76, 77, 71, 72, 73};
		return keypad[key - RETROK_KP0];
	}
	if (key >= RETROK_F1 && key <= RETROK_F10) return (uint16_t)(key - RETROK_F1 + 59);
	switch (key) {
	case RETROK_0: return 11;
	case RETROK_ESCAPE: return 1;
	case RETROK_MINUS: return 12;
	case RETROK_EQUALS: return 13;
	case RETROK_BACKSPACE: return 14;
	case RETROK_TAB: return 15;
	case RETROK_LEFTBRACKET: return 26;
	case RETROK_RIGHTBRACKET: return 27;
	case RETROK_RETURN: return 28;
	case RETROK_LCTRL: return 29;
	case RETROK_SEMICOLON: return 39;
	case RETROK_QUOTE: return 40;
	case RETROK_BACKQUOTE: return 41;
	case RETROK_LSHIFT: return 42;
	case RETROK_BACKSLASH: return 43;
	case RETROK_COMMA: return 51;
	case RETROK_PERIOD: return 52;
	case RETROK_SLASH: return 53;
	case RETROK_RSHIFT: return 54;
	case RETROK_LALT: return 56;
	case RETROK_SPACE: return 57;
	case RETROK_CAPSLOCK: return 58;
	case RETROK_F11: return 87;
	case RETROK_F12: return 88;
	case RETROK_KP_ENTER: return 96;
	case RETROK_RCTRL: return 97;
	case RETROK_RALT: return 100;
	case RETROK_HOME: return 102;
	case RETROK_UP: return 103;
	case RETROK_PAGEUP: return 104;
	case RETROK_LEFT: return 105;
	case RETROK_RIGHT: return 106;
	case RETROK_END: return 107;
	case RETROK_DOWN: return 108;
	case RETROK_PAGEDOWN: return 109;
	case RETROK_INSERT: return 110;
	case RETROK_DELETE: return 111;
	default: return 0;
	}
}

size_t
st_libretro_key_events(struct st_console_event events[2], bool down,
	unsigned key, uint32_t character, uint16_t modifiers, bool repeat)
{
	uint32_t mods = 0;
	uint16_t physical = physical_key(key), code = 0;
	size_t count = 0;
	if (modifiers & RETROKMOD_SHIFT) mods |= ST_CONSOLE_MODIFIER_SHIFT;
	if (modifiers & RETROKMOD_CTRL) mods |= ST_CONSOLE_MODIFIER_CONTROL;
	if (modifiers & RETROKMOD_ALT) mods |= ST_CONSOLE_MODIFIER_ALT;
	if (modifiers & RETROKMOD_META) mods |= ST_CONSOLE_MODIFIER_GUI;
	if (modifiers & RETROKMOD_CAPSLOCK) mods |= ST_CONSOLE_MODIFIER_CAPS_LOCK;
	if (modifiers & RETROKMOD_NUMLOCK) mods |= ST_CONSOLE_MODIFIER_NUM_LOCK;
	if (physical != 0)
		events[count++] = (struct st_console_event){
			.type = ST_CONSOLE_EVENT_PHYSICAL_KEY,
			.data.physical_key = {physical, mods, down, down && repeat},
		};
	if (!down) return count;
	if (key >= RETROK_F1 && key <= RETROK_F12)
		code = (uint16_t)(ST_CONSOLE_KEY_F1 + key - RETROK_F1);
	else switch (key) {
	case RETROK_BACKSPACE: code = 8; break;
	case RETROK_TAB: code = 9; break;
	case RETROK_RETURN: case RETROK_KP_ENTER: code = 13; break;
	case RETROK_ESCAPE: code = 27; break;
	case RETROK_DELETE: code = ST_CONSOLE_KEY_DELETE; break;
	case RETROK_INSERT: code = ST_CONSOLE_KEY_INSERT; break;
	case RETROK_HOME: code = ST_CONSOLE_KEY_HOME; break;
	case RETROK_END: code = ST_CONSOLE_KEY_END; break;
	case RETROK_UP: code = ST_CONSOLE_KEY_UP; break;
	case RETROK_DOWN: code = ST_CONSOLE_KEY_DOWN; break;
	case RETROK_LEFT: code = ST_CONSOLE_KEY_LEFT; break;
	case RETROK_RIGHT: code = ST_CONSOLE_KEY_RIGHT; break;
	case RETROK_PAGEUP: code = ST_CONSOLE_KEY_PAGE_UP; break;
	case RETROK_PAGEDOWN: code = ST_CONSOLE_KEY_PAGE_DOWN; break;
	case RETROK_KP_PERIOD:
		if (modifiers & RETROKMOD_NUMLOCK) character = '.';
		else code = ST_CONSOLE_KEY_DELETE;
		break;
	case RETROK_KP_DIVIDE: character = '/'; break;
	case RETROK_KP_MULTIPLY: character = '*'; break;
	case RETROK_KP_MINUS: character = '-'; break;
	case RETROK_KP_PLUS: character = '+'; break;
	case RETROK_KP_EQUALS: character = '='; break;
	default: break;
	}
	if (key >= RETROK_KP0 && key <= RETROK_KP9) {
		static const uint16_t navigation[] = {
			ST_CONSOLE_KEY_INSERT, ST_CONSOLE_KEY_END, ST_CONSOLE_KEY_DOWN,
			ST_CONSOLE_KEY_PAGE_DOWN, ST_CONSOLE_KEY_LEFT, 0,
			ST_CONSOLE_KEY_RIGHT, ST_CONSOLE_KEY_HOME, ST_CONSOLE_KEY_UP,
			ST_CONSOLE_KEY_PAGE_UP,
		};
		if (modifiers & RETROKMOD_NUMLOCK) character = '0' + key - RETROK_KP0;
		else code = navigation[key - RETROK_KP0];
	}
	if (code != 0) {
		events[count++] = (struct st_console_event){
			.type = ST_CONSOLE_EVENT_KEY_CODE, .data.key = {code, mods},
		};
		return count;
	}
	/* Control/Alt letters are generated once from the physical event, as in
	 * SDL. Ctrl punctuation has no physical compatibility mapping. */
	if (mods & (ST_CONSOLE_MODIFIER_ALT | ST_CONSOLE_MODIFIER_GUI)) return count;
	if (mods & ST_CONSOLE_MODIFIER_CONTROL) {
		if ((key >= RETROK_a && key <= RETROK_z) || key == RETROK_BACKQUOTE)
			return count;
		if (key == RETROK_SPACE || key == RETROK_AT) character = 0;
		else if (key >= RETROK_LEFTBRACKET && key <= RETROK_UNDERSCORE)
			character = key & 31;
		else return count;
		events[count++] = (struct st_console_event){
			.type = ST_CONSOLE_EVENT_KEY_CODE,
			.data.key = {(uint16_t)character, mods},
		};
		return count;
	}
	if (character == 0 && key >= 32 && key <= 126) {
		character = key;
		if (key >= RETROK_a && key <= RETROK_z) {
			if (!!(mods & ST_CONSOLE_MODIFIER_SHIFT)
			    != !!(mods & ST_CONSOLE_MODIFIER_CAPS_LOCK)) character -= 32;
		} else if (mods & ST_CONSOLE_MODIFIER_SHIFT) {
			static const char plain[] = "1234567890-=[]\\;'`,./";
			static const char shifted[] = "!@#$%^&*()_+{}|:\"~<>?";
			for (size_t i = 0; i < sizeof(plain) - 1; ++i)
				if (character == (unsigned char)plain[i]) { character = (unsigned char)shifted[i]; break; }
		}
	}
	if (character >= 32 && character <= 0x10ffff
	    && (character < 0xd800 || character > 0xdfff))
		events[count++] = (struct st_console_event){
			.type = ST_CONSOLE_EVENT_TEXT, .data.text = {character, mods},
		};
	return count;
}

/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_libretro_input.h"
#include "libretro.h"
#include <stdio.h>
#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%d: %s\n", __LINE__, #e); return 1; } } while (0)
int main(void)
{
	struct st_console_event events[2];
	size_t count = st_libretro_key_events(events, true, RETROK_b, 0, RETROKMOD_ALT, false);
	CHECK(count == 1 && events[0].type == ST_CONSOLE_EVENT_PHYSICAL_KEY);
	CHECK(events[0].data.physical_key.evdev == 48 && events[0].data.physical_key.modifiers == ST_CONSOLE_MODIFIER_ALT);
	count = st_libretro_key_events(events, false, RETROK_b, 0, 0, true);
	CHECK(count == 1 && !events[0].data.physical_key.pressed && !events[0].data.physical_key.repeat);
	count = st_libretro_key_events(events, true, RETROK_a, 0x1f937, 0, false);
	CHECK(count == 2 && events[1].type == ST_CONSOLE_EVENT_TEXT && events[1].data.text.codepoint == 0x1f937);
	count = st_libretro_key_events(events, true, RETROK_a, 0, RETROKMOD_SHIFT, true);
	CHECK(count == 2 && events[0].data.physical_key.repeat && events[1].data.text.codepoint == 'A');
	count = st_libretro_key_events(events, true, RETROK_a, 1, RETROKMOD_CTRL, false);
	CHECK(count == 1);
	count = st_libretro_key_events(events, true, RETROK_1, 0, RETROKMOD_SHIFT, false);
	CHECK(count == 2 && events[1].data.text.codepoint == '!');
	count = st_libretro_key_events(events, true, RETROK_F12, 0, RETROKMOD_CTRL, false);
	CHECK(count == 2 && events[1].type == ST_CONSOLE_EVENT_KEY_CODE && events[1].data.key.code == ST_CONSOLE_KEY_F12);
	count = st_libretro_key_events(events, true, RETROK_KP1, 0, RETROKMOD_NUMLOCK, false);
	CHECK(count == 2 && events[0].data.physical_key.evdev == 79
	    && events[1].data.text.codepoint == '1');
	count = st_libretro_key_events(events, true, RETROK_KP1, 0, 0, false);
	CHECK(count == 2 && events[0].data.physical_key.evdev == 79
	    && events[1].data.key.code == ST_CONSOLE_KEY_END);
	count = st_libretro_key_events(events, true, RETROK_UNKNOWN, 0xd800, 0, false);
	CHECK(count == 0);
	return 0;
}

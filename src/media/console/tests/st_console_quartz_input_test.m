/* Quartz key translation without creating an application or a window. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */

#include "console_internal.h"

static struct st_console_event captured;
static unsigned captured_count;
static bool capture_key(st_console_display_t *, const struct st_console_event *);

/* Compile the actual helper, redirecting its publisher. Rename the backend's
 * external entry points and Objective-C classes to coexist with st_console. */
#define st_console_display_post_key_event capture_key
#define st_console_quartz_init test_quartz_init
#define st_console_quartz_pump test_quartz_pump
#define st_console_quartz_run_application test_quartz_run_application
#define STConsoleQuartzView STConsoleQuartzInputTestView
#define STConsoleQuartzDelegate STConsoleQuartzInputTestDelegate
#define STConsoleQuartzCommandReceiver STConsoleQuartzInputTestCommandReceiver
#include "../console_quartz.m"
#undef STConsoleQuartzCommandReceiver
#undef STConsoleQuartzDelegate
#undef STConsoleQuartzView
#undef st_console_quartz_run_application
#undef st_console_quartz_pump
#undef st_console_quartz_init
#undef st_console_display_post_key_event

#include <stdio.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
		return 1; \
	} \
} while (false)

static bool
capture_key(st_console_display_t *display, const struct st_console_event *event)
{
	(void)display;
	captured = *event;
	++captured_count;
	return true;
}

static NSEvent *
key_event(NSEventType type, unsigned short key, NSEventModifierFlags modifiers,
	BOOL repeat)
{
	return [NSEvent keyEventWithType:type location:NSZeroPoint
	    modifierFlags:modifiers timestamp:0 windowNumber:0 context:nil
	    characters:@"" charactersIgnoringModifiers:@"" isARepeat:repeat keyCode:key];
}

int
main(void)
{
	@autoreleasepool {
		struct quartz_state state = {0};
		/* Ticket 298: right Shift (keyCode 60) arrives as FlagsChanged.
		 * Querying isARepeat on this real NSEvent raises an AppKit exception. */
		NSEvent *event = key_event(NSEventTypeFlagsChanged, kVK_RightShift,
		    NSEventModifierFlagShift, NO);
		CHECK(event != nil && event.type == NSEventTypeFlagsChanged);
		CHECK(post_physical(&state, event, true));
		CHECK(captured_count == 1 && captured.type == ST_CONSOLE_EVENT_PHYSICAL_KEY);
		CHECK(captured.data.physical_key.evdev == 54
		    && captured.data.physical_key.modifiers == ST_CONSOLE_MODIFIER_SHIFT
		    && captured.data.physical_key.pressed && !captured.data.physical_key.repeat);
		event = key_event(NSEventTypeFlagsChanged, kVK_RightShift, 0, NO);
		CHECK(event != nil && post_physical(&state, event, false));
		CHECK(captured_count == 2 && !captured.data.physical_key.pressed
		    && !captured.data.physical_key.repeat && captured.data.physical_key.modifiers == 0);
		for (unsigned repeat = 0; repeat < 2; ++repeat) {
			event = key_event(NSEventTypeKeyDown, kVK_ANSI_A,
			    NSEventModifierFlagControl, repeat ? YES : NO);
			CHECK(event != nil && post_physical(&state, event, true));
			CHECK(captured.data.physical_key.evdev == 30
			    && captured.data.physical_key.modifiers == ST_CONSOLE_MODIFIER_CONTROL
			    && captured.data.physical_key.pressed
			    && captured.data.physical_key.repeat == (repeat != 0));
		}
		event = key_event(NSEventTypeKeyUp, kVK_ANSI_A, 0, NO);
		CHECK(event != nil && post_physical(&state, event, false));
		CHECK(captured_count == 5 && !captured.data.physical_key.pressed
		    && !captured.data.physical_key.repeat);
	}
	return 0;
}

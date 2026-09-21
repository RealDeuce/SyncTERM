/* Native macOS AppKit and Quartz bitmap console backend. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>

#include "console_internal.h"

#include "console_config.h"
#include "io_error.h"
#include "st_time.h"
#include "st_utf8.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct quartz_state;

@interface STConsoleQuartzView : NSView {
@public
	struct quartz_state *consoleState;
}
@end

@interface STConsoleQuartzDelegate : NSObject <NSWindowDelegate> {
@public
	struct quartz_state *consoleState;
}
@end

@interface STConsoleQuartzCommandReceiver : NSObject
- (void)executeCommandValue:(NSValue *)value;
@end

struct quartz_state {
	/* Only the backend thread changes or dereferences this pointer. */
	st_console_t *console;
	st_console_display_t *console_display;
	struct st_console_bitmap_geometry window_geometry;
	NSWindow *window;
	STConsoleQuartzView *view;
	STConsoleQuartzDelegate *delegate;
	NSCursor *invisible_cursor;
	struct st_console_bitmap_presenter *presenter;
	struct st_console_bitmap_frame frame;
	uint32_t *pixels;
	uint32_t pixel_width;
	uint32_t pixel_height;
	uint32_t minimum_pixel_width;
	uint32_t minimum_pixel_height;
	uint32_t windowed_pixel_width;
	uint32_t windowed_pixel_height;
	struct st_console_rect damage;
	bool have_damage;
	bool pointer_visible;
	bool fullscreen;
	bool modifier_down[256];
};


enum quartz_command_type {
	QUARTZ_COMMAND_CREATE,
	QUARTZ_COMMAND_DESTROY,
	QUARTZ_COMMAND_PRESENT,
	QUARTZ_COMMAND_ACTIVATE,
	QUARTZ_COMMAND_RESIZE,
	QUARTZ_COMMAND_TITLE,
	QUARTZ_COMMAND_CLIPBOARD_SET,
	QUARTZ_COMMAND_CLIPBOARD_GET,
	QUARTZ_COMMAND_OPEN_URL,
	QUARTZ_COMMAND_POINTER,
	QUARTZ_COMMAND_FULLSCREEN,
	QUARTZ_COMMAND_GET_SCALING,
};

struct quartz_command {
	enum quartz_command_type type;
	struct quartz_state *state;
	union {
		st_console_t *console;
		struct {
			uint32_t width;
			uint32_t height;
			uint32_t minimum_width;
			uint32_t minimum_height;
		} size;
		const char *text;
		char **text_result;
		double *scaling_result;
		bool enabled;
	} data;
	int result;
};

#if !ST_CONSOLE_HAVE_SDL
struct application_call {
	int argc;
	char **argv;
	int (*function)(int, char **);
};
#endif

static STConsoleQuartzCommandReceiver *command_receiver;
#if !ST_CONSOLE_HAVE_SDL
static atomic_bool application_done;
static atomic_int application_result;
#endif
static bool runner_active;

static uint32_t
now_ms(void)
{
	uint64_t value = 0;

	(void)st_time_monotonic_ms(&value);
	return (uint32_t)value;
}

static uint16_t
physical_key(unsigned short key)
{
	static const uint16_t map[128] = {
		[kVK_ANSI_A] = 30, [kVK_ANSI_S] = 31,
		[kVK_ANSI_D] = 32, [kVK_ANSI_F] = 33,
		[kVK_ANSI_H] = 35, [kVK_ANSI_G] = 34,
		[kVK_ANSI_Z] = 44, [kVK_ANSI_X] = 45,
		[kVK_ANSI_C] = 46, [kVK_ANSI_V] = 47,
		[kVK_ANSI_B] = 48, [kVK_ANSI_Q] = 16,
		[kVK_ANSI_W] = 17, [kVK_ANSI_E] = 18,
		[kVK_ANSI_R] = 19, [kVK_ANSI_Y] = 21,
		[kVK_ANSI_T] = 20, [kVK_ANSI_1] = 2,
		[kVK_ANSI_2] = 3, [kVK_ANSI_3] = 4,
		[kVK_ANSI_4] = 5, [kVK_ANSI_6] = 7,
		[kVK_ANSI_5] = 6, [kVK_ANSI_Equal] = 13,
		[kVK_ANSI_9] = 10, [kVK_ANSI_7] = 8,
		[kVK_ANSI_Minus] = 12, [kVK_ANSI_8] = 9,
		[kVK_ANSI_0] = 11, [kVK_ANSI_RightBracket] = 27,
		[kVK_ANSI_O] = 24, [kVK_ANSI_U] = 22,
		[kVK_ANSI_LeftBracket] = 26, [kVK_ANSI_I] = 23,
		[kVK_ANSI_P] = 25, [kVK_Return] = 28,
		[kVK_ANSI_L] = 38, [kVK_ANSI_J] = 36,
		[kVK_ANSI_Quote] = 40, [kVK_ANSI_K] = 37,
		[kVK_ANSI_Semicolon] = 39, [kVK_ANSI_Backslash] = 43,
		[kVK_ANSI_Comma] = 51, [kVK_ANSI_Slash] = 53,
		[kVK_ANSI_N] = 49, [kVK_ANSI_M] = 50,
		[kVK_ANSI_Period] = 52, [kVK_Tab] = 15,
		[kVK_Space] = 57, [kVK_ANSI_Grave] = 41,
		[kVK_Delete] = 14, [kVK_Escape] = 1,
		[kVK_F1] = 59, [kVK_F2] = 60, [kVK_F3] = 61,
		[kVK_F4] = 62, [kVK_F5] = 63, [kVK_F6] = 64,
		[kVK_F7] = 65, [kVK_F8] = 66, [kVK_F9] = 67,
		[kVK_F10] = 68, [kVK_F11] = 87, [kVK_F12] = 88,
		[kVK_ANSI_KeypadDecimal] = 83,
		[kVK_ANSI_KeypadMultiply] = 55,
		[kVK_ANSI_KeypadPlus] = 78,
		[kVK_ANSI_KeypadMinus] = 74,
		[kVK_ANSI_Keypad0] = 82, [kVK_ANSI_Keypad1] = 79,
		[kVK_ANSI_Keypad2] = 80, [kVK_ANSI_Keypad3] = 81,
		[kVK_ANSI_Keypad4] = 75, [kVK_ANSI_Keypad5] = 76,
		[kVK_ANSI_Keypad6] = 77, [kVK_ANSI_Keypad7] = 71,
		[kVK_ANSI_Keypad8] = 72, [kVK_ANSI_Keypad9] = 73,
	};

	switch (key) {
	case kVK_ANSI_KeypadEnter: return 96;
	case kVK_Control: return 29;
	case kVK_RightControl: return 97;
	case kVK_Option: return 56;
	case kVK_RightOption: return 100;
	case kVK_Shift: return 42;
	case kVK_RightShift: return 54;
	case kVK_Command: return 125;
	case kVK_RightCommand: return 126;
	case kVK_CapsLock: return 58;
	case kVK_Home: return 102;
	case kVK_PageUp: return 104;
	case kVK_ForwardDelete: return 111;
	case kVK_End: return 107;
	case kVK_PageDown: return 109;
	case kVK_LeftArrow: return 105;
	case kVK_RightArrow: return 106;
	case kVK_DownArrow: return 108;
	case kVK_UpArrow: return 103;
	default: return key < sizeof(map) / sizeof(map[0]) ? map[key] : 0;
	}
}

static uint16_t
translated_key(unsigned short key)
{
	switch (key) {
	case kVK_F1: return ST_CONSOLE_KEY_F1;
	case kVK_F2: return ST_CONSOLE_KEY_F2;
	case kVK_F3: return ST_CONSOLE_KEY_F3;
	case kVK_F4: return ST_CONSOLE_KEY_F4;
	case kVK_F5: return ST_CONSOLE_KEY_F5;
	case kVK_F6: return ST_CONSOLE_KEY_F6;
	case kVK_F7: return ST_CONSOLE_KEY_F7;
	case kVK_F8: return ST_CONSOLE_KEY_F8;
	case kVK_F9: return ST_CONSOLE_KEY_F9;
	case kVK_F10: return ST_CONSOLE_KEY_F10;
	case kVK_F11: return ST_CONSOLE_KEY_F11;
	case kVK_F12: return ST_CONSOLE_KEY_F12;
	case kVK_Delete: return 8;
	case kVK_Tab: return 9;
	case kVK_Return:
	case kVK_ANSI_KeypadEnter: return 13;
	case kVK_Escape: return 27;
	case kVK_Home: return ST_CONSOLE_KEY_HOME;
	case kVK_LeftArrow: return ST_CONSOLE_KEY_LEFT;
	case kVK_UpArrow: return ST_CONSOLE_KEY_UP;
	case kVK_RightArrow: return ST_CONSOLE_KEY_RIGHT;
	case kVK_DownArrow: return ST_CONSOLE_KEY_DOWN;
	case kVK_Help: return ST_CONSOLE_KEY_INSERT;
	case kVK_ForwardDelete: return ST_CONSOLE_KEY_DELETE;
	case kVK_End: return ST_CONSOLE_KEY_END;
	case kVK_PageUp: return ST_CONSOLE_KEY_PAGE_UP;
	case kVK_PageDown: return ST_CONSOLE_KEY_PAGE_DOWN;
	default: return 0;
	}
}

static uint32_t
event_modifiers(NSEventModifierFlags flags)
{
	uint32_t result = 0;

	if ((flags & NSEventModifierFlagShift) != 0)
		result |= ST_CONSOLE_MODIFIER_SHIFT;
	if ((flags & NSEventModifierFlagControl) != 0)
		result |= ST_CONSOLE_MODIFIER_CONTROL;
	if ((flags & NSEventModifierFlagOption) != 0)
		result |= ST_CONSOLE_MODIFIER_ALT;
	if ((flags & NSEventModifierFlagCommand) != 0)
		result |= ST_CONSOLE_MODIFIER_GUI;
	if ((flags & NSEventModifierFlagCapsLock) != 0)
		result |= ST_CONSOLE_MODIFIER_CAPS_LOCK;
	return result;
}

static void
post_physical(struct quartz_state *state, NSEvent *source, bool pressed)
{
	uint16_t key = physical_key(source.keyCode);
	struct st_console_event event;

	if (key == 0)
		return;
	event = (struct st_console_event){
		.type = ST_CONSOLE_EVENT_PHYSICAL_KEY,
		.data.physical_key = {
			.evdev = key,
			.modifiers = event_modifiers(source.modifierFlags),
			.pressed = pressed,
			.repeat = pressed && source.isARepeat,
		},
	};
	(void)st_console_display_post_event(state->console_display, &event);
}

static void
post_text(struct quartz_state *state, NSString *text,
	NSEventModifierFlags modifier_flags)
{
	NSUInteger length = text.length;

	for (NSUInteger i = 0; i < length; ++i) {
		uint32_t codepoint = [text characterAtIndex:i];

		if (codepoint >= 0xd800 && codepoint <= 0xdbff && i + 1 < length) {
			uint32_t low = [text characterAtIndex:i + 1];

			if (low >= 0xdc00 && low <= 0xdfff) {
				codepoint = UINT32_C(0x10000)
				    + ((codepoint - 0xd800) << 10) + low - 0xdc00;
				++i;
			}
		}
		if (!st_unicode_scalar_valid(codepoint))
			continue;
		if (codepoint < 0x20 || codepoint == 0x7f)
			continue;
		(void)st_console_display_post_event(state->console_display,
		    &(struct st_console_event){
			.type = ST_CONSOLE_EVENT_TEXT,
			.data.text = {
				.codepoint = codepoint,
				.modifiers = event_modifiers(modifier_flags),
			},
		    });
	}
}

static void
center_window(struct quartz_state *state)
{
	NSScreen *screen = state->window.screen;
	NSRect frame;
	NSRect available;

	if (screen == nil)
		screen = NSScreen.mainScreen;
	if (screen == nil)
		return;
	frame = state->window.frame;
	available = screen.visibleFrame;
	frame.origin.x = available.origin.x
	    + (available.size.width - frame.size.width) / 2;
	frame.origin.y = available.origin.y
	    + (available.size.height - frame.size.height) / 2;
	[state->window setFrameOrigin:frame.origin];
}

static void
snap_window(struct quartz_state *state, bool larger)
{
	CGFloat scale = state->window.backingScaleFactor;
	NSSize current = state->view.bounds.size;
	NSRect available;
	uint32_t width;
	uint32_t height;
	uint32_t maximum_width = 0;
	uint32_t maximum_height = 0;

	if (state->fullscreen
	    || (state->window.styleMask & NSWindowStyleMaskFullScreen) != 0)
		return;
	if (scale <= 0)
		scale = 1;
	if (state->window.screen != nil) {
		available = [NSWindow contentRectForFrameRect:
		    state->window.screen.visibleFrame styleMask:state->window.styleMask];
		if (available.size.width > 0 && available.size.height > 0) {
			maximum_width = (uint32_t)(available.size.width * scale);
			maximum_height = (uint32_t)(available.size.height * scale);
		}
	}
	if (st_console_snap_geometry_size(&state->window_geometry,
	    (uint32_t)(current.width * scale),
	    (uint32_t)(current.height * scale), larger, maximum_width,
	    maximum_height, &width, &height) != 0
	    || (width == (uint32_t)(current.width * scale)
	    && height == (uint32_t)(current.height * scale)))
		return;
	state->windowed_pixel_width = width;
	state->windowed_pixel_height = height;
	[state->window setContentSize:NSMakeSize(width / scale, height / scale)];
}

static void
post_mouse(struct quartz_state *state, NSEvent *source,
	enum st_console_mouse_action action)
{
	NSPoint point = [state->view convertPoint:source.locationInWindow
	    fromView:nil];
	CGFloat scale = state->window.backingScaleFactor;
	uint32_t buttons = 0;
	NSUInteger native_buttons = [NSEvent pressedMouseButtons];
	struct st_console_event event = {
		.type = ST_CONSOLE_EVENT_MOUSE,
		.data.mouse = {
			.action = action,
			.modifiers = event_modifiers(source.modifierFlags),
		},
	};
	struct st_console_bitmap_frame frame = state->frame;
	double x = point.x * scale;
	double y = (state->view.bounds.size.height - point.y) * scale;

	if ((native_buttons & (1UL << 0)) != 0)
		buttons |= ST_CONSOLE_MOUSE_BUTTON_LEFT;
	if ((native_buttons & (1UL << 1)) != 0)
		buttons |= ST_CONSOLE_MOUSE_BUTTON_RIGHT;
	if ((native_buttons & (1UL << 2)) != 0)
		buttons |= ST_CONSOLE_MOUSE_BUTTON_MIDDLE;
	if ((native_buttons & (1UL << 3)) != 0)
		buttons |= ST_CONSOLE_MOUSE_BUTTON_X1;
	if ((native_buttons & (1UL << 4)) != 0)
		buttons |= ST_CONSOLE_MOUSE_BUTTON_X2;
	event.data.mouse.buttons = buttons;
	st_console_bitmap_mouse_position(&frame, (int64_t)x, (int64_t)y,
	    &event);
	if (action == ST_CONSOLE_MOUSE_WHEEL) {
		event.data.mouse.wheel_x = source.scrollingDeltaX > 0 ? 1
		    : source.scrollingDeltaX < 0 ? -1 : 0;
		event.data.mouse.wheel_y = source.scrollingDeltaY > 0 ? 1
		    : source.scrollingDeltaY < 0 ? -1 : 0;
		if (source.isDirectionInvertedFromDevice) {
			event.data.mouse.wheel_x = -event.data.mouse.wheel_x;
			event.data.mouse.wheel_y = -event.data.mouse.wheel_y;
		}
	}
	(void)st_console_display_post_event(state->console_display, &event);
}

@implementation STConsoleQuartzView

- (BOOL)isFlipped
{
	return NO;
}

- (BOOL)acceptsFirstResponder
{
	return YES;
}

- (void)resetCursorRects
{
	if (consoleState != NULL && !consoleState->pointer_visible)
		[self addCursorRect:self.bounds cursor:consoleState->invisible_cursor];
	else
		[super resetCursorRects];
}

- (void)drawRect:(NSRect)dirtyRect
{
	struct quartz_state *state = consoleState;
	CGContextRef context;
	CGColorSpaceRef colour_space;
	CGDataProviderRef provider;
	CGImageRef image;
	CGFloat scale;
	CGRect destination;
	size_t bytes;

	(void)dirtyRect;
	if (state == NULL)
		return;
	context = NSGraphicsContext.currentContext.CGContext;
	CGContextSetRGBFillColor(context, 0, 0, 0, 1);
	CGContextFillRect(context, NSRectToCGRect(self.bounds));
	if (state->pixels == NULL || state->pixel_width == 0
	    || state->pixel_height == 0)
		return;
	bytes = (size_t)state->pixel_width * state->pixel_height
	    * sizeof(state->pixels[0]);
	provider = CGDataProviderCreateWithData(NULL, state->pixels, bytes, NULL);
	colour_space = CGColorSpaceCreateDeviceRGB();
	image = CGImageCreate(state->pixel_width, state->pixel_height, 8, 32,
	    (size_t)state->pixel_width * sizeof(state->pixels[0]), colour_space,
	    kCGBitmapByteOrder32Little | kCGImageAlphaNoneSkipFirst, provider,
	    NULL, false, kCGRenderingIntentDefault);
	CGColorSpaceRelease(colour_space);
	CGDataProviderRelease(provider);
	if (image == NULL)
		return;
	scale = state->window.backingScaleFactor;
	if (scale <= 0)
		scale = 1;
	destination = CGRectMake(state->frame.destination.x / scale,
	    self.bounds.size.height
	    - (state->frame.destination.y + state->frame.destination.height)
	    / scale,
	    state->frame.destination.width / scale,
	    state->frame.destination.height / scale);
	CGContextSetInterpolationQuality(context,
	    state->frame.scaling_mode == ST_CONSOLE_SCALING_EXTERNAL
	    ? kCGInterpolationDefault : kCGInterpolationNone);
	CGContextDrawImage(context, destination, image);
	CGImageRelease(image);
}

- (void)keyDown:(NSEvent *)event
{
	uint16_t key;
	NSEventModifierFlags modifiers = event.modifierFlags;
	bool command = (modifiers & NSEventModifierFlagCommand) != 0;
	bool control = (modifiers & NSEventModifierFlagControl) != 0;
	bool option = (modifiers & NSEventModifierFlagOption) != 0;
	NSString *characters = event.charactersIgnoringModifiers;
	unichar character = characters.length == 0
	    ? 0 : [characters characterAtIndex:0];

	if (command && !control && !option
	    && (character == 'q' || character == 'Q')) {
		(void)st_console_display_post_event(consoleState->console_display,
		    &(struct st_console_event){.type = ST_CONSOLE_EVENT_CLOSE});
		return;
	}
	if (command && !control && !option
	    && (character == 'v' || character == 'V')) {
		(void)st_console_display_post_event(consoleState->console_display,
		    &(struct st_console_event){
			.type = ST_CONSOLE_EVENT_KEY_CODE,
			.data.key = {
				.code = ST_CONSOLE_KEY_INSERT,
				.modifiers = ST_CONSOLE_MODIFIER_SHIFT,
			},
		    });
		return;
	}
	if (command && control && !option
	    && (character == 'f' || character == 'F')) {
		[consoleState->window toggleFullScreen:nil];
		return;
	}
	post_physical(consoleState, event, true);
	if (command || option) {
		if ((event.keyCode == kVK_Return
		    || event.keyCode == kVK_ANSI_KeypadEnter) && !event.isARepeat) {
			if (!consoleState->fullscreen) {
				CGFloat scale = consoleState->window.backingScaleFactor;
				NSSize size = consoleState->view.bounds.size;

				if (scale <= 0)
					scale = 1;
				consoleState->windowed_pixel_width
				    = (uint32_t)(size.width * scale);
				consoleState->windowed_pixel_height
				    = (uint32_t)(size.height * scale);
			}
			[consoleState->window toggleFullScreen:nil];
			return;
		}
		if (event.keyCode == kVK_LeftArrow) {
			snap_window(consoleState, false);
			return;
		}
		if (event.keyCode == kVK_RightArrow) {
			snap_window(consoleState, true);
			return;
		}
		if (event.keyCode == kVK_Home) {
			center_window(consoleState);
			return;
		}
	}
	key = translated_key(event.keyCode);
	if (key != 0)
		(void)st_console_display_post_event(consoleState->console_display,
		    &(struct st_console_event){
			.type = ST_CONSOLE_EVENT_KEY_CODE,
			.data.key = {
				.code = key,
				.modifiers = event_modifiers(event.modifierFlags),
			},
		    });
	if (key == 0
	    && (event.modifierFlags & (NSEventModifierFlagCommand
	    | NSEventModifierFlagControl)) == 0)
		post_text(consoleState, event.characters, event.modifierFlags);
}

- (void)keyUp:(NSEvent *)event
{
	post_physical(consoleState, event, false);
}

- (void)flagsChanged:(NSEvent *)event
{
	uint16_t key = physical_key(event.keyCode);
	bool pressed;

	if (key == 0 || key >= sizeof(consoleState->modifier_down))
		return;
	pressed = !consoleState->modifier_down[key];
	consoleState->modifier_down[key] = pressed;
	post_physical(consoleState, event, pressed);
}

- (void)mouseMoved:(NSEvent *)event
{
	post_mouse(consoleState, event, ST_CONSOLE_MOUSE_MOVE);
}

- (void)mouseDragged:(NSEvent *)event
{
	post_mouse(consoleState, event, ST_CONSOLE_MOUSE_MOVE);
}

- (void)rightMouseDragged:(NSEvent *)event
{
	post_mouse(consoleState, event, ST_CONSOLE_MOUSE_MOVE);
}

- (void)otherMouseDragged:(NSEvent *)event
{
	post_mouse(consoleState, event, ST_CONSOLE_MOUSE_MOVE);
}

- (void)mouseDown:(NSEvent *)event
{
	post_mouse(consoleState, event, ST_CONSOLE_MOUSE_BUTTON);
}

- (void)mouseUp:(NSEvent *)event
{
	post_mouse(consoleState, event, ST_CONSOLE_MOUSE_BUTTON);
}

- (void)rightMouseDown:(NSEvent *)event
{
	post_mouse(consoleState, event, ST_CONSOLE_MOUSE_BUTTON);
}

- (void)rightMouseUp:(NSEvent *)event
{
	post_mouse(consoleState, event, ST_CONSOLE_MOUSE_BUTTON);
}

- (void)otherMouseDown:(NSEvent *)event
{
	post_mouse(consoleState, event, ST_CONSOLE_MOUSE_BUTTON);
}

- (void)otherMouseUp:(NSEvent *)event
{
	post_mouse(consoleState, event, ST_CONSOLE_MOUSE_BUTTON);
}

- (void)scrollWheel:(NSEvent *)event
{
	post_mouse(consoleState, event, ST_CONSOLE_MOUSE_WHEEL);
}

@end

@implementation STConsoleQuartzDelegate

- (BOOL)windowShouldClose:(NSWindow *)sender
{
	(void)sender;
	(void)st_console_display_post_event(consoleState->console_display,
	    &(struct st_console_event){.type = ST_CONSOLE_EVENT_CLOSE});
	return NO;
}

- (void)windowDidResize:(NSNotification *)notification
{
	CGFloat scale = consoleState->window.backingScaleFactor;
	NSSize size = consoleState->view.bounds.size;

	(void)notification;
	if (scale <= 0)
		scale = 1;
	if (!consoleState->fullscreen
	    && (consoleState->window.styleMask
	    & NSWindowStyleMaskFullScreen) == 0) {
		consoleState->windowed_pixel_width = (uint32_t)(size.width * scale);
		consoleState->windowed_pixel_height = (uint32_t)(size.height * scale);
	}
	[consoleState->view setNeedsDisplay:YES];
}

- (void)windowDidEnterFullScreen:(NSNotification *)notification
{
	(void)notification;
	consoleState->fullscreen = true;
}

- (void)windowDidExitFullScreen:(NSNotification *)notification
{
	CGFloat scale = consoleState->window.backingScaleFactor;

	(void)notification;
	if (scale <= 0)
		scale = 1;
	[consoleState->window setContentSize:NSMakeSize(
	    consoleState->windowed_pixel_width / scale,
	    consoleState->windowed_pixel_height / scale)];
	consoleState->fullscreen = false;
}

- (void)windowDidBecomeKey:(NSNotification *)notification
{
	(void)notification;
	(void)st_console_display_post_event(consoleState->console_display,
	    &(struct st_console_event){
		.type = ST_CONSOLE_EVENT_FOCUS,
		.data.focus.focused = true,
	    });
}

- (void)windowDidResignKey:(NSNotification *)notification
{
	(void)notification;
	memset(consoleState->modifier_down, 0,
	    sizeof(consoleState->modifier_down));
	(void)st_console_display_post_event(consoleState->console_display,
	    &(struct st_console_event){
		.type = ST_CONSOLE_EVENT_FOCUS,
		.data.focus.focused = false,
	    });
}

- (void)windowDidChangeBackingProperties:(NSNotification *)notification
{
	CGFloat scale = consoleState->window.backingScaleFactor;

	(void)notification;
	if (scale <= 0)
		scale = 1;
	consoleState->window.contentMinSize = NSMakeSize(
	    consoleState->minimum_pixel_width / scale,
	    consoleState->minimum_pixel_height / scale);
	if (!consoleState->fullscreen)
		[consoleState->window setContentSize:NSMakeSize(
		    consoleState->windowed_pixel_width / scale,
		    consoleState->windowed_pixel_height / scale)];
	[consoleState->view setNeedsDisplay:YES];
}

@end

static void
add_damage(struct quartz_state *state,
	const struct st_console_rect *rectangle)
{
	uint32_t right = rectangle->x + rectangle->width;
	uint32_t bottom = rectangle->y + rectangle->height;

	if (!state->have_damage) {
		state->damage = *rectangle;
		state->have_damage = true;
		return;
	}
	if (rectangle->x < state->damage.x) {
		state->damage.width += state->damage.x - rectangle->x;
		state->damage.x = rectangle->x;
	}
	if (rectangle->y < state->damage.y) {
		state->damage.height += state->damage.y - rectangle->y;
		state->damage.y = rectangle->y;
	}
	if (right > state->damage.x + state->damage.width)
		state->damage.width = right - state->damage.x;
	if (bottom > state->damage.y + state->damage.height)
		state->damage.height = bottom - state->damage.y;
}

static int
upload(void *context, const struct st_rgb_buffer *pixels,
	const struct st_console_rect *rectangle, const void *image_identity,
	uint64_t generation)
{
	struct quartz_state *state = context;
	size_t count;

	(void)image_identity;
	(void)generation;

	if (pixels->height != 0 && pixels->width > SIZE_MAX / pixels->height)
		return IO_ERROR_OUT_OF_RANGE;
	count = (size_t)pixels->width * pixels->height;
	if (state->pixel_width != pixels->width
	    || state->pixel_height != pixels->height) {
		uint32_t *replacement;

		if (count > SIZE_MAX / sizeof(*replacement))
			return IO_ERROR_OUT_OF_RANGE;
		replacement = malloc(count * sizeof(*replacement));
		if (replacement == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		memcpy(replacement, pixels->pixels, count * sizeof(*replacement));
		free(state->pixels);
		state->pixels = replacement;
		state->pixel_width = pixels->width;
		state->pixel_height = pixels->height;
		state->damage = (struct st_console_rect){0, 0,
		    pixels->width, pixels->height};
		state->have_damage = true;
		return 0;
	}
	for (uint32_t y = 0; y < rectangle->height; ++y) {
		size_t offset = (size_t)(rectangle->y + y) * pixels->width
		    + rectangle->x;

		memcpy(&state->pixels[offset], &pixels->pixels[offset],
		    (size_t)rectangle->width * sizeof(state->pixels[0]));
	}
	add_damage(state, rectangle);
	return 0;
}

static void
invalidate_damage(struct quartz_state *state, bool forced)
{
	CGFloat scale = state->window.backingScaleFactor;
	struct st_console_rect damage = state->damage;
	struct st_console_bitmap_frame *frame = &state->frame;
	double left;
	double top;
	double right;
	double bottom;

	if (forced || !state->have_damage || frame->pixels == NULL
	    || frame->destination.width == 0 || frame->destination.height == 0) {
		[state->view setNeedsDisplay:YES];
		state->have_damage = false;
		return;
	}
	if (scale <= 0)
		scale = 1;
	left = frame->destination.x + (double)damage.x
	    * frame->destination.width / frame->pixels->width;
	top = frame->destination.y + (double)damage.y
	    * frame->destination.height / frame->pixels->height;
	right = frame->destination.x + (double)(damage.x + damage.width)
	    * frame->destination.width / frame->pixels->width;
	bottom = frame->destination.y + (double)(damage.y + damage.height)
	    * frame->destination.height / frame->pixels->height;
	[state->view setNeedsDisplayInRect:NSMakeRect(left / scale,
	    state->view.bounds.size.height - bottom / scale,
	    (right - left) / scale, (bottom - top) / scale)];
	state->have_damage = false;
}

static int
render(struct quartz_state *state, const st_console_t *console, bool forced)
{
	NSSize size = state->view.bounds.size;
	CGFloat scale = state->window.backingScaleFactor;
	uint32_t width;
	uint32_t height;
	int status;

	if (size.width <= 0 || size.height <= 0)
		return 0;
	if (scale <= 0)
		scale = 1;
	width = (uint32_t)(size.width * scale);
	height = (uint32_t)(size.height * scale);
	status = st_console_bitmap_presenter_frame(state->presenter, console,
	    width, height, now_ms(), upload, state, &state->frame);
	if (status == 0 && (state->frame.changed || forced))
		invalidate_damage(state, forced);
	return status;
}

static NSCursor *
make_invisible_cursor(void)
{
	NSImage *image = [[NSImage alloc] initWithSize:NSMakeSize(1, 1)];
	NSCursor *cursor = [[NSCursor alloc] initWithImage:image
	    hotSpot:NSMakePoint(0, 0)];

	[image release];
	return cursor;
}

static void
set_application_icon(struct quartz_state *state)
{
	const struct st_console_icon *icon = state->console_display->config.icon;
	NSBitmapImageRep *representation;
	NSImage *image;

	if (icon == NULL)
		return;
	representation = [[NSBitmapImageRep alloc]
	    initWithBitmapDataPlanes:NULL
	    pixelsWide:(NSInteger)icon->width pixelsHigh:(NSInteger)icon->height
	    bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO
	    colorSpaceName:NSDeviceRGBColorSpace
	    bitmapFormat:NSBitmapFormatAlphaNonpremultiplied
	    bytesPerRow:(NSInteger)icon->stride bitsPerPixel:32];
	if (representation == nil)
		return;
	for (uint32_t y = 0; y < icon->height; ++y)
		memcpy(&representation.bitmapData[(size_t)y * representation.bytesPerRow],
		    &icon->pixels[(size_t)y * icon->stride],
		    (size_t)icon->width * 4);
	image = [[NSImage alloc] initWithSize:NSMakeSize(icon->width,
	    icon->height)];
	if (image != nil) {
		[image addRepresentation:representation];
		[NSApp setApplicationIconImage:image];
		state->window.miniwindowImage = image;
		[image release];
	}
	[representation release];
}

static void destroy_state(struct quartz_state *state);

static int
create_state(struct quartz_state *state, uint32_t width, uint32_t height,
	uint32_t minimum_width, uint32_t minimum_height)
{
	CGFloat scale = NSScreen.mainScreen.backingScaleFactor;
	NSRect content;
	NSUInteger style;

	if (!runner_active)
		return IO_ERROR_NOT_SUPPORTED;
	if (scale <= 0)
		scale = 1;
	content = NSMakeRect(0, 0, width / scale, height / scale);
	style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
	    | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
	state->window = [[NSWindow alloc] initWithContentRect:content
	    styleMask:style backing:NSBackingStoreBuffered defer:NO];
	state->view = [[STConsoleQuartzView alloc] initWithFrame:content];
	state->delegate = [[STConsoleQuartzDelegate alloc] init];
	state->invisible_cursor = make_invisible_cursor();
	if (state->window == nil || state->view == nil || state->delegate == nil
	    || state->invisible_cursor == nil) {
		[state->invisible_cursor release];
		[state->delegate release];
		[state->view release];
		[state->window release];
		state->invisible_cursor = nil;
		state->delegate = nil;
		state->view = nil;
		state->window = nil;
		return IO_ERROR_OUT_OF_MEMORY;
	}
	state->view->consoleState = state;
	state->delegate->consoleState = state;
	state->window.releasedWhenClosed = NO;
	state->window.contentView = state->view;
	state->window.delegate = state->delegate;
	state->window.title = @"SyncTERM";
	set_application_icon(state);
	state->window.acceptsMouseMovedEvents = YES;
	state->minimum_pixel_width = minimum_width;
	state->minimum_pixel_height = minimum_height;
	state->windowed_pixel_width = width;
	state->windowed_pixel_height = height;
	state->window.contentMinSize = NSMakeSize(minimum_width / scale,
	    minimum_height / scale);
	[state->window center];
	[state->window makeKeyAndOrderFront:nil];
	int status = render(state, state->console, true);

	if (status != 0)
		destroy_state(state);
	return status;
}

static void
destroy_state(struct quartz_state *state)
{
	state->view->consoleState = NULL;
	state->delegate->consoleState = NULL;
	state->window.delegate = nil;
	[state->window orderOut:nil];
	[state->window close];
	[state->invisible_cursor release];
	[state->delegate release];
	[state->view release];
	[state->window release];
	state->invisible_cursor = nil;
	state->delegate = nil;
	state->view = nil;
	state->window = nil;
}

static int
copy_clipboard(char **result)
{
	NSString *string = [NSPasteboard.generalPasteboard
	    stringForType:NSPasteboardTypeString];
	const char *source;
	size_t length;
	char *copy;

	if (string == nil)
		return IO_ERROR_NOT_FOUND;
	source = string.UTF8String;
	if (source == NULL)
		return IO_ERROR_IO;
	length = strlen(source);
	copy = malloc(length + 1);
	if (copy == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	memcpy(copy, source, length + 1);
	*result = copy;
	return 0;
}

static int
process_command(struct quartz_command *command)
{
	struct quartz_state *state = command->state;
	NSString *string;

	switch (command->type) {
	case QUARTZ_COMMAND_CREATE:
		return create_state(state, command->data.size.width,
		    command->data.size.height, command->data.size.minimum_width,
		    command->data.size.minimum_height);
	case QUARTZ_COMMAND_DESTROY:
		destroy_state(state);
		return 0;
	case QUARTZ_COMMAND_ACTIVATE:
		state->console = command->data.console;
		return 0;
	case QUARTZ_COMMAND_PRESENT:
		return render(state, state->console, false);
	case QUARTZ_COMMAND_RESIZE: {
		struct st_console_bitmap_geometry next = {
			command->data.size.width, command->data.size.height,
			state->console->aspect_width, state->console->aspect_height};
		CGFloat scale = state->window.backingScaleFactor;
		uint32_t width;
		uint32_t height;
		uint32_t current_width;
		uint32_t current_height;
		NSSize current = state->view.bounds.size;

		if (scale <= 0)
			scale = 1;
		current_width = state->fullscreen ? state->windowed_pixel_width
		    : (uint32_t)(current.width * scale);
		current_height = state->fullscreen ? state->windowed_pixel_height
		    : (uint32_t)(current.height * scale);
		if (st_console_preserve_geometry_scale(&state->window_geometry, &next,
		    current_width, current_height, &width, &height,
		    &state->minimum_pixel_width,
		    &state->minimum_pixel_height) != 0)
			return IO_ERROR_OUT_OF_RANGE;
		state->windowed_pixel_width = width;
		state->windowed_pixel_height = height;
		state->window_geometry = next;
		state->window.contentMinSize = NSMakeSize(
		    state->minimum_pixel_width / scale,
		    state->minimum_pixel_height / scale);
		if (!state->fullscreen)
			[state->window setContentSize:NSMakeSize(width / scale,
			    height / scale)];
		return render(state, NULL, true);
	}
	case QUARTZ_COMMAND_GET_SCALING: {
		CGFloat scale = state->window.backingScaleFactor;
		NSSize size = state->view.bounds.size;

		if (scale <= 0)
			scale = 1;
		return st_console_bitmap_geometry_scaling(&state->window_geometry,
		    (uint32_t)(size.width * scale),
		    (uint32_t)(size.height * scale),
		    command->data.scaling_result);
	}
	case QUARTZ_COMMAND_TITLE:
		string = [NSString stringWithUTF8String:command->data.text];
		if (string == nil)
			return IO_ERROR_INVALID_ARGUMENT;
		state->window.title = string;
		return 0;
	case QUARTZ_COMMAND_CLIPBOARD_SET:
		string = [NSString stringWithUTF8String:command->data.text];
		if (string == nil)
			return IO_ERROR_INVALID_ARGUMENT;
		[NSPasteboard.generalPasteboard clearContents];
		return [NSPasteboard.generalPasteboard writeObjects:@[string]]
		    ? 0 : IO_ERROR_IO;
	case QUARTZ_COMMAND_CLIPBOARD_GET:
		return copy_clipboard(command->data.text_result);
	case QUARTZ_COMMAND_OPEN_URL:
		string = [NSString stringWithUTF8String:command->data.text];
		if (string == nil)
			return IO_ERROR_INVALID_ARGUMENT;
		return [NSWorkspace.sharedWorkspace openURL:
		    [NSURL URLWithString:string]] ? 0 : IO_ERROR_IO;
	case QUARTZ_COMMAND_POINTER:
		state->pointer_visible = command->data.enabled;
		[state->window invalidateCursorRectsForView:state->view];
		return 0;
	case QUARTZ_COMMAND_FULLSCREEN:
		if (((state->window.styleMask & NSWindowStyleMaskFullScreen) != 0)
		    != command->data.enabled) {
			if (command->data.enabled) {
				CGFloat scale = state->window.backingScaleFactor;
				NSSize size = state->view.bounds.size;

				if (scale <= 0)
					scale = 1;
				state->windowed_pixel_width
				    = (uint32_t)(size.width * scale);
				state->windowed_pixel_height
				    = (uint32_t)(size.height * scale);
				state->fullscreen = true;
			}
			[state->window toggleFullScreen:nil];
		}
		return 0;
	}
	return IO_ERROR_INVALID_ARGUMENT;
}

@implementation STConsoleQuartzCommandReceiver

- (void)executeCommandValue:(NSValue *)value
{
	struct quartz_command *command = value.pointerValue;

	command->result = process_command(command);
}

@end

static int
submit_command(struct quartz_command *command)
{
	if (!runner_active || command_receiver == nil)
		return IO_ERROR_NOT_SUPPORTED;
	if (NSThread.isMainThread)
		command->result = process_command(command);
	else
		[command_receiver performSelectorOnMainThread:
		    @selector(executeCommandValue:)
		    withObject:[NSValue valueWithPointer:command]
		    waitUntilDone:YES];
	return command->result;
}

void
st_console_quartz_pump(void)
{
	@autoreleasepool {
		NSEvent *event;

		event = [NSApp nextEventMatchingMask:NSEventMaskAny
		    untilDate:[NSDate dateWithTimeIntervalSinceNow:0]
		    inMode:NSDefaultRunLoopMode dequeue:YES];
		while (event != nil) {
			[NSApp sendEvent:event];
			event = [NSApp nextEventMatchingMask:NSEventMaskAny
			    untilDate:[NSDate dateWithTimeIntervalSinceNow:0]
			    inMode:NSDefaultRunLoopMode dequeue:YES];
		}
		[NSApp updateWindows];
	}
}

#if !ST_CONSOLE_HAVE_SDL
static int
application_worker(void *argument)
{
	struct application_call *call = argument;
	int result = call->function(call->argc, call->argv);

	atomic_store_explicit(&application_result, result, memory_order_relaxed);
	atomic_store_explicit(&application_done, true, memory_order_release);
	return 0;
}
#endif

int
st_console_quartz_run_application(int argc, char **argv,
	int (*application_main)(int, char **))
{
	int result;

	if (application_main == NULL || !NSThread.isMainThread)
		return 1;
	@autoreleasepool {
		[NSApplication sharedApplication];
		[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
		[NSApp finishLaunching];
		command_receiver = [[STConsoleQuartzCommandReceiver alloc] init];
		runner_active = true;
#if ST_CONSOLE_HAVE_SDL
		result = st_console_sdl_run_application(argc, argv,
		    application_main);
#else
		struct application_call call = {argc, argv, application_main};
		st_thread_t application;

		atomic_store_explicit(&application_done, false,
		    memory_order_relaxed);
		atomic_store_explicit(&application_result, 1,
		    memory_order_relaxed);
		if (st_thread_create(&application, application_worker, &call)
		    != ST_THREAD_SUCCESS)
			result = 1;
		else {
			while (!atomic_load_explicit(&application_done,
			    memory_order_acquire)) {
				@autoreleasepool {
					NSEvent *event = [NSApp
					    nextEventMatchingMask:NSEventMaskAny
					    untilDate:[NSDate
						dateWithTimeIntervalSinceNow:0.01]
					    inMode:NSDefaultRunLoopMode dequeue:YES];

					if (event != nil)
						[NSApp sendEvent:event];
				}
				st_console_quartz_pump();
			}
			(void)st_thread_join(application, NULL);
			result = atomic_load_explicit(&application_result,
			    memory_order_relaxed);
		}
#endif
		runner_active = false;
		[command_receiver release];
		command_receiver = nil;
	}
	return result;
}

static int
quartz_activate(st_console_t *console)
{
	return submit_command(&(struct quartz_command){
		.type = QUARTZ_COMMAND_ACTIVATE,
		.state = console->display->backend_state,
		.data.console = console,
	});
}

static int
quartz_present(st_console_t *console)
{
	return submit_command(&(struct quartz_command){
		.type = QUARTZ_COMMAND_PRESENT,
		.state = console->display->backend_state,
	});
}

static int
quartz_resize(st_console_t *console, uint32_t columns, uint32_t rows)
{

	if (columns > (UINT32_MAX - console->pixel_extra_width)
	    / console->cell_width
	    || rows > (UINT32_MAX - console->pixel_extra_height)
	    / console->cell_height)
		return IO_ERROR_OUT_OF_RANGE;
	return submit_command(&(struct quartz_command){
		.type = QUARTZ_COMMAND_RESIZE,
		.state = console->display->backend_state,
		.data.size = {
			.width = columns * console->cell_width
			    + console->pixel_extra_width,
			.height = rows * console->cell_height
			    + console->pixel_extra_height,
		},
	});
}

static int
quartz_set_title(st_console_display_t *display, const char *title)
{
	return submit_command(&(struct quartz_command){
		.type = QUARTZ_COMMAND_TITLE,
		.state = display->backend_state,
		.data.text = title,
	});
}

static int
quartz_set_clipboard(st_console_display_t *display, const char *text)
{
	return submit_command(&(struct quartz_command){
		.type = QUARTZ_COMMAND_CLIPBOARD_SET,
		.state = display->backend_state,
		.data.text = text,
	});
}

static int
quartz_get_clipboard(st_console_display_t *display, char **result)
{
	return submit_command(&(struct quartz_command){
		.type = QUARTZ_COMMAND_CLIPBOARD_GET,
		.state = display->backend_state,
		.data.text_result = result,
	});
}

static int
quartz_open_url(st_console_display_t *display, const char *url)
{
	return submit_command(&(struct quartz_command){
		.type = QUARTZ_COMMAND_OPEN_URL,
		.state = display->backend_state,
		.data.text = url,
	});
}

static int
quartz_set_pointer(st_console_display_t *display, bool visible)
{
	return submit_command(&(struct quartz_command){
		.type = QUARTZ_COMMAND_POINTER,
		.state = display->backend_state,
		.data.enabled = visible,
	});
}

static int
quartz_set_fullscreen(st_console_display_t *display, bool fullscreen)
{
	return submit_command(&(struct quartz_command){
		.type = QUARTZ_COMMAND_FULLSCREEN,
		.state = display->backend_state,
		.data.enabled = fullscreen,
	});
}

static int
quartz_get_scaling(st_console_display_t *display, double *result)
{
	return submit_command(&(struct quartz_command){
		.type = QUARTZ_COMMAND_GET_SCALING,
		.state = display->backend_state,
		.data.scaling_result = result,
	});
}

static void
quartz_destroy(st_console_display_t *display)
{
	struct quartz_state *state = display->backend_state;

	(void)submit_command(&(struct quartz_command){
		.type = QUARTZ_COMMAND_DESTROY,
		.state = state,
	});
	st_console_bitmap_presenter_destroy(state->presenter);
	free(state->pixels);
	free(state);
	display->backend_state = NULL;
}

static const struct st_console_backend_ops quartz_ops = {
	.presents_pixels = true,
	.deferred_present = true,
	.animated_present = true,
	.present = quartz_present,
	.resize = quartz_resize,
	.set_title = quartz_set_title,
	.set_clipboard_text = quartz_set_clipboard,
	.get_clipboard_text = quartz_get_clipboard,
	.open_url = quartz_open_url,
	.set_pointer_visible = quartz_set_pointer,
	.set_fullscreen = quartz_set_fullscreen,
	.get_scaling = quartz_get_scaling,
	.activate = quartz_activate,
	.destroy = quartz_destroy,
};

int
st_console_quartz_init(st_console_t *console)
{
	struct quartz_state *state;
	uint32_t minimum_width;
	uint32_t minimum_height;
	int status;

	if (console->pixels == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	status = st_console_aspect_correct_size(console->pixels->width,
	    console->pixels->height, console->aspect_width,
	    console->aspect_height, &minimum_width, &minimum_height);
	if (status != 0)
		return status;
	state = calloc(1, sizeof(*state));
	if (state == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	state->console = console;
	state->window_geometry = (struct st_console_bitmap_geometry){
		console->pixels->width, console->pixels->height,
		console->aspect_width, console->aspect_height};
	state->pointer_visible = true;
	state->console_display = console->display;
	status = st_console_bitmap_presenter_create(&state->presenter, console);
	if (status != 0) {
		free(state);
		return status;
	}
	status = submit_command(&(struct quartz_command){
		.type = QUARTZ_COMMAND_CREATE,
		.state = state,
		.data.size = {
			.width = console->display->initial_window_width,
			.height = console->display->initial_window_height,
			.minimum_width = minimum_width,
			.minimum_height = minimum_height,
		},
	});
	if (status != 0) {
		st_console_bitmap_presenter_destroy(state->presenter);
		free(state->pixels);
		free(state);
		return status;
	}
	console->display->backend_state = state;
	console->display->backend_ops = &quartz_ops;
	return 0;
}

/* Native Windows console text backend. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include "console_internal.h"
#include "io_error.h"
#include "st_windows_utf.h"

#include <inttypes.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

struct win32_state {
	st_console_display_t *display;
	HANDLE input;
	HANDLE original_output;
	HANDLE output;
	HANDLE stop_event;
	DWORD original_input_mode;
	DWORD original_display_mode;
	st_thread_t input_thread;
	struct st_console_cell *drawn;
	uint32_t columns;
	uint32_t rows;
	wchar_t pending_high_surrogate;
	bool input_started;
	bool input_mode_changed;
	bool output_activated;
	bool display_mode_known;
	bool invalid_colour;
	uint32_t invalid_colour_value;
};

static atomic_flag win32_in_use = ATOMIC_FLAG_INIT;

static wchar_t *
utf8_to_utf16(const char *text)
{
	wchar_t *result = NULL;

	return st_windows_utf8_to_utf16(&result, text) == 0 ? result : NULL;
}

static char *
utf16_to_utf8(const wchar_t *text)
{
	char *result = NULL;

	return st_windows_utf16_to_utf8(&result, text) == 0 ? result : NULL;
}

static WORD
console_colour(const st_console_t *console, struct win32_state *state,
	uint32_t colour, bool background)
{
	uint32_t rgb;
	uint32_t best_distance = UINT32_MAX;
	unsigned int value = 0;

	if (!st_console_colour_rgb_locked(console, colour, &rgb)) {
		if (!state->invalid_colour) {
			state->invalid_colour = true;
			state->invalid_colour_value = colour;
		}
		rgb = 0;
	}
	for (unsigned int i = 0; i < 16; ++i) {
		uint32_t candidate = console->palette[i];
		int32_t red = (int32_t)((rgb >> 16) & UINT32_C(0xff))
		    - (int32_t)((candidate >> 16) & UINT32_C(0xff));
		int32_t green = (int32_t)((rgb >> 8) & UINT32_C(0xff))
		    - (int32_t)((candidate >> 8) & UINT32_C(0xff));
		int32_t blue = (int32_t)(rgb & UINT32_C(0xff))
		    - (int32_t)(candidate & UINT32_C(0xff));
		uint32_t distance = (uint32_t)(red * red + green * green
		    + blue * blue);

		if (distance < best_distance) {
			best_distance = distance;
			value = i;
		}
	}
	if (background)
		return (WORD)(((value & 7U) << 4)
		    | ((value & 8U) != 0 ? BACKGROUND_INTENSITY : 0));
	return (WORD)((value & 7U) | ((value & 8U) != 0
	    ? FOREGROUND_INTENSITY : 0));
}

static WORD
cell_attributes(const st_console_t *console, struct win32_state *state,
	const struct st_console_cell *cell)
{
	uint32_t foreground = cell->foreground;
	uint32_t background = cell->background;
	WORD result;

	if ((cell->flags & ST_CONSOLE_CELL_REVERSE) != 0) {
		uint32_t temporary = foreground;

		foreground = background;
		background = temporary;
	}
	result = console_colour(console, state, foreground, false)
	    | console_colour(console, state, background, true);
	if ((cell->flags & ST_CONSOLE_CELL_UNDERLINE) != 0)
		result |= COMMON_LVB_UNDERSCORE;
	return result;
}

static WCHAR
cell_character(const struct st_console_cell *cell)
{
	uint32_t value = cell->codepoint;

	if ((cell->flags & ST_CONSOLE_CELL_CONTINUATION) != 0) {
		if (value > UINT32_C(0xffff) && value <= UINT32_C(0x10ffff))
			return (WCHAR)(UINT32_C(0xdc00)
			    + ((value - UINT32_C(0x10000)) & UINT32_C(0x3ff)));
		return L' ';
	}
	if (value > UINT32_C(0xffff) && value <= UINT32_C(0x10ffff))
		return (WCHAR)(UINT32_C(0xd800)
		    + ((value - UINT32_C(0x10000)) >> 10));
	if (value >= UINT32_C(0xd800) && value <= UINT32_C(0xdfff))
		return L'?';
	return (WCHAR)value;
}

static int
allocate_drawn(struct win32_state *state, uint32_t columns, uint32_t rows)
{
	struct st_console_cell *drawn;
	size_t count;

	if (rows != 0 && columns > SIZE_MAX / rows)
		return IO_ERROR_OUT_OF_RANGE;
	count = (size_t)columns * rows;
	drawn = calloc(count, sizeof(*drawn));
	if (drawn == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	free(state->drawn);
	state->drawn = drawn;
	state->columns = columns;
	state->rows = rows;
	return 0;
}

static int
write_run(struct win32_state *state, const st_console_t *console,
	uint32_t y, uint32_t first, uint32_t count)
{
	CHAR_INFO buffer[256];
	COORD buffer_size;
	COORD buffer_origin = {0, 0};
	SMALL_RECT destination;
	uint32_t offset = 0;

	while (offset < count) {
		uint32_t amount = count - offset;

		if (amount > sizeof(buffer) / sizeof(buffer[0]))
			amount = sizeof(buffer) / sizeof(buffer[0]);
		for (uint32_t i = 0; i < amount; ++i) {
			size_t index = (size_t)y * console->columns + first + offset + i;
			const struct st_console_cell *source = &console->cells[index];
			struct st_console_cell effective;
			const struct st_console_cell *cell;

			st_console_effective_cell_locked(console, source, &effective);
			cell = &effective;
			buffer[i].Char.UnicodeChar = cell_character(cell);
			buffer[i].Attributes = cell_attributes(console, state, cell);
			if ((cell->flags & ST_CONSOLE_CELL_CONTINUATION) != 0)
				buffer[i].Attributes |= COMMON_LVB_TRAILING_BYTE;
			else if (i + offset + first + 1 < console->columns
			    && (console->cells[index + 1].flags
			    & ST_CONSOLE_CELL_CONTINUATION) != 0)
				buffer[i].Attributes |= COMMON_LVB_LEADING_BYTE;
			state->drawn[index] = *source;
			state->drawn[index].flags &= ~ST_CONSOLE_CELL_DIRTY;
		}
		buffer_size.X = (SHORT)amount;
		buffer_size.Y = 1;
		destination.Left = (SHORT)(first + offset);
		destination.Top = (SHORT)y;
		destination.Right = (SHORT)(first + offset + amount - 1);
		destination.Bottom = (SHORT)y;
		if (!WriteConsoleOutputW(state->output, buffer, buffer_size,
		    buffer_origin, &destination))
			return IO_ERROR_IO;
		offset += amount;
	}
	return 0;
}

static int
win32_present(st_console_t *console)
{
	struct win32_state *state = console->display->backend_state;
	CONSOLE_CURSOR_INFO cursor;
	COORD position;
	uint32_t cursor_x;
	uint32_t cursor_columns;
	int status;

	if (console->columns > SHRT_MAX || console->rows > SHRT_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	state->invalid_colour = false;
	if (state->columns != console->columns || state->rows != console->rows) {
		status = allocate_drawn(state, console->columns, console->rows);
		if (status != 0)
			return status;
	}
	for (uint32_t y = 0; y < console->rows; ++y) {
		uint32_t x = 0;

		while (x < console->columns) {
			uint32_t first;

			while (x < console->columns) {
				size_t index = (size_t)y * console->columns + x;
				const struct st_console_cell *cell = &console->cells[index];

				if ((cell->flags & ST_CONSOLE_CELL_DIRTY) != 0
				    || !st_console_cells_equal(cell,
				    &state->drawn[index]))
					break;
				++x;
			}
			first = x;
			while (x < console->columns) {
				size_t index = (size_t)y * console->columns + x;
				const struct st_console_cell *cell = &console->cells[index];

				if ((cell->flags & ST_CONSOLE_CELL_DIRTY) == 0
				    && st_console_cells_equal(cell,
				    &state->drawn[index]))
					break;
				++x;
			}
			if (x != first) {
				status = write_run(state, console, y, first, x - first);
				if (status != 0)
					return status;
			}
		}
	}
	if (state->invalid_colour)
		fprintf(stderr, "win32 console: invalid colour value %08" PRIx32
		    "; using black\n", state->invalid_colour_value);
	st_console_cursor_span_locked(console, &cursor_x, &cursor_columns);
	(void)cursor_columns;
	position.X = (SHORT)cursor_x;
	position.Y = (SHORT)console->cursor_y;
	if (!SetConsoleCursorPosition(state->output, position))
		return IO_ERROR_IO;
	cursor.bVisible = console->cursor_visible;
	cursor.dwSize = console->cursor_shape == ST_CONSOLE_CURSOR_UNDERLINE
	    ? 15 : console->cursor_shape == ST_CONSOLE_CURSOR_BAR ? 25 : 100;
	return SetConsoleCursorInfo(state->output, &cursor) ? 0 : IO_ERROR_IO;
}

static int
set_screen_size(struct win32_state *state, uint32_t columns, uint32_t rows)
{
	CONSOLE_SCREEN_BUFFER_INFO information;
	SMALL_RECT window;
	COORD size;

	if (columns == 0 || rows == 0 || columns > SHRT_MAX || rows > SHRT_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	if (!GetConsoleScreenBufferInfo(state->output, &information))
		return IO_ERROR_IO;
	size.X = (SHORT)columns;
	size.Y = (SHORT)rows;
	window = (SMALL_RECT){0, 0, (SHORT)(columns - 1), (SHORT)(rows - 1)};
	if (columns > (uint32_t)information.dwSize.X
	    || rows > (uint32_t)information.dwSize.Y) {
		if (!SetConsoleScreenBufferSize(state->output, size)
		    || !SetConsoleWindowInfo(state->output, TRUE, &window))
			return IO_ERROR_IO;
	}
	else if (!SetConsoleWindowInfo(state->output, TRUE, &window)
	    || !SetConsoleScreenBufferSize(state->output, size))
		return IO_ERROR_IO;
	return allocate_drawn(state, columns, rows);
}

static int
win32_resize(st_console_t *console, uint32_t columns, uint32_t rows)
{
	return set_screen_size(console->display->backend_state, columns, rows);
}

static uint32_t
input_modifiers(DWORD state)
{
	uint32_t result = 0;

	if ((state & SHIFT_PRESSED) != 0)
		result |= ST_CONSOLE_MODIFIER_SHIFT;
	if ((state & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0)
		result |= ST_CONSOLE_MODIFIER_CONTROL;
	if ((state & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0)
		result |= ST_CONSOLE_MODIFIER_ALT;
	if ((state & CAPSLOCK_ON) != 0)
		result |= ST_CONSOLE_MODIFIER_CAPS_LOCK;
	if ((state & NUMLOCK_ON) != 0)
		result |= ST_CONSOLE_MODIFIER_NUM_LOCK;
	return result;
}

static uint16_t
physical_key(const KEY_EVENT_RECORD *record)
{
	WORD scan = record->wVirtualScanCode & 0xff;
	bool enhanced = (record->dwControlKeyState & ENHANCED_KEY) != 0;

	if (record->wVirtualKeyCode == VK_PAUSE)
		return 119;
	if (!enhanced)
		return (scan >= 1 && scan <= 0x58) ? scan : 0;
	switch (scan) {
	case 0x1c: return 96;
	case 0x1d: return 97;
	case 0x35: return 98;
	case 0x37: return 99;
	case 0x38: return 100;
	case 0x47: return 102;
	case 0x48: return 103;
	case 0x49: return 104;
	case 0x4b: return 105;
	case 0x4d: return 106;
	case 0x4f: return 107;
	case 0x50: return 108;
	case 0x51: return 109;
	case 0x52: return 110;
	case 0x53: return 111;
	case 0x5b: return 125;
	case 0x5c: return 126;
	case 0x5d: return 139;
	default: return 0;
	}
}

static uint16_t
translated_key(WORD key)
{
	if (key >= VK_F1 && key <= VK_F12)
		return (uint16_t)(ST_CONSOLE_KEY_F1 + key - VK_F1);
	switch (key) {
	case VK_BACK: return 8;
	case VK_TAB: return 9;
	case VK_RETURN: return 13;
	case VK_ESCAPE: return 27;
	case VK_HOME: return ST_CONSOLE_KEY_HOME;
	case VK_LEFT: return ST_CONSOLE_KEY_LEFT;
	case VK_UP: return ST_CONSOLE_KEY_UP;
	case VK_RIGHT: return ST_CONSOLE_KEY_RIGHT;
	case VK_DOWN: return ST_CONSOLE_KEY_DOWN;
	case VK_INSERT: return ST_CONSOLE_KEY_INSERT;
	case VK_DELETE: return ST_CONSOLE_KEY_DELETE;
	case VK_END: return ST_CONSOLE_KEY_END;
	case VK_PRIOR: return ST_CONSOLE_KEY_PAGE_UP;
	case VK_NEXT: return ST_CONSOLE_KEY_PAGE_DOWN;
	default: return 0;
	}
}

static void
post_key(struct win32_state *state, const KEY_EVENT_RECORD *record)
{
	struct st_console_event event;
	uint16_t physical = physical_key(record);
	uint16_t translated = translated_key(record->wVirtualKeyCode);
	uint32_t codepoint = record->uChar.UnicodeChar;

	if (physical != 0) {
		event = (struct st_console_event){
			.type = ST_CONSOLE_EVENT_PHYSICAL_KEY,
			.data.physical_key = {
				.evdev = physical,
				.modifiers = input_modifiers(record->dwControlKeyState),
				.pressed = record->bKeyDown != FALSE,
				.repeat = record->bKeyDown != FALSE
				    && record->wRepeatCount > 1,
			},
		};
		(void)st_console_display_post_event(state->display, &event);
	}
	if (!record->bKeyDown)
		return;
	if (translated != 0) {
		event = (struct st_console_event){
			.type = ST_CONSOLE_EVENT_KEY_CODE,
			.data.key = {
				.code = translated,
				.modifiers = input_modifiers(record->dwControlKeyState),
			},
		};
		(void)st_console_display_post_event(state->display, &event);
	}
	if (codepoint >= UINT32_C(0xd800) && codepoint <= UINT32_C(0xdbff)) {
		state->pending_high_surrogate = (wchar_t)codepoint;
		return;
	}
	if (codepoint >= UINT32_C(0xdc00) && codepoint <= UINT32_C(0xdfff)
	    && state->pending_high_surrogate != 0) {
		codepoint = UINT32_C(0x10000)
		    + (((uint32_t)state->pending_high_surrogate - 0xd800) << 10)
		    + codepoint - 0xdc00;
	}
	else if (codepoint >= UINT32_C(0xd800)
	    && codepoint <= UINT32_C(0xdfff))
		codepoint = 0;
	state->pending_high_surrogate = 0;
	if (codepoint >= 0x20 && codepoint != 0x7f) {
		event = (struct st_console_event){
			.type = ST_CONSOLE_EVENT_TEXT,
			.data.text = {
				.codepoint = codepoint,
				.modifiers = input_modifiers(record->dwControlKeyState),
			},
		};
		(void)st_console_display_post_event(state->display, &event);
	}
}

static void
post_mouse(struct win32_state *state, const MOUSE_EVENT_RECORD *record)
{
	struct st_console_event event = {
		.type = ST_CONSOLE_EVENT_MOUSE,
		.data.mouse = {
			.x = record->dwMousePosition.X,
			.y = record->dwMousePosition.Y,
			.pixel_x = -1,
			.pixel_y = -1,
			.action = ST_CONSOLE_MOUSE_BUTTON,
		},
	};
	DWORD buttons = record->dwButtonState;

	if ((buttons & FROM_LEFT_1ST_BUTTON_PRESSED) != 0)
		event.data.mouse.buttons |= ST_CONSOLE_MOUSE_BUTTON_LEFT;
	if ((buttons & FROM_LEFT_2ND_BUTTON_PRESSED) != 0)
		event.data.mouse.buttons |= ST_CONSOLE_MOUSE_BUTTON_MIDDLE;
	if ((buttons & RIGHTMOST_BUTTON_PRESSED) != 0)
		event.data.mouse.buttons |= ST_CONSOLE_MOUSE_BUTTON_RIGHT;
	if ((buttons & FROM_LEFT_3RD_BUTTON_PRESSED) != 0)
		event.data.mouse.buttons |= ST_CONSOLE_MOUSE_BUTTON_X1;
	if ((buttons & FROM_LEFT_4TH_BUTTON_PRESSED) != 0)
		event.data.mouse.buttons |= ST_CONSOLE_MOUSE_BUTTON_X2;
	if (record->dwEventFlags == MOUSE_MOVED)
		event.data.mouse.action = ST_CONSOLE_MOUSE_MOVE;
	else if (record->dwEventFlags == MOUSE_WHEELED) {
		event.data.mouse.action = ST_CONSOLE_MOUSE_WHEEL;
		event.data.mouse.wheel_y = (SHORT)HIWORD(buttons) / WHEEL_DELTA;
	}
#ifdef MOUSE_HWHEELED
	else if (record->dwEventFlags == MOUSE_HWHEELED) {
		event.data.mouse.action = ST_CONSOLE_MOUSE_WHEEL;
		event.data.mouse.wheel_x = (SHORT)HIWORD(buttons) / WHEEL_DELTA;
	}
#endif
	else if (record->dwEventFlags != 0)
		return;
	(void)st_console_display_post_event(state->display, &event);
}

static int
input_worker(void *argument)
{
	struct win32_state *state = argument;
	HANDLE handles[2] = {state->stop_event, state->input};

	for (;;) {
		INPUT_RECORD record;
		DWORD count;
		DWORD wait_result = WaitForMultipleObjects(2, handles, FALSE, INFINITE);

		if (wait_result == WAIT_OBJECT_0)
			break;
		if (wait_result != WAIT_OBJECT_0 + 1
		    || !ReadConsoleInputW(state->input, &record, 1, &count)
		    || count != 1)
			break;
		switch (record.EventType) {
		case KEY_EVENT:
			post_key(state, &record.Event.KeyEvent);
			break;
		case MOUSE_EVENT:
			post_mouse(state, &record.Event.MouseEvent);
			break;
		case WINDOW_BUFFER_SIZE_EVENT: {
			struct st_console_event event = {
				.type = ST_CONSOLE_EVENT_RESIZE,
				.data.resize = {
					.columns = record.Event.WindowBufferSizeEvent.dwSize.X,
					.rows = record.Event.WindowBufferSizeEvent.dwSize.Y,
				},
			};

			(void)st_console_display_post_event(state->display, &event);
			break;
		}
		case FOCUS_EVENT: {
			struct st_console_event event = {
				.type = ST_CONSOLE_EVENT_FOCUS,
				.data.focus.focused = record.Event.FocusEvent.bSetFocus != FALSE,
			};

			(void)st_console_display_post_event(state->display, &event);
			break;
		}
		default:
			break;
		}
	}
	return 0;
}

static void
win32_palette_changed(st_console_t *console, uint16_t first,
	const uint32_t *colours, uint32_t count)
{
	struct win32_state *state = console->display->backend_state;
	CONSOLE_SCREEN_BUFFER_INFOEX information = {
		.cbSize = sizeof(information),
	};
	uint32_t end = (uint32_t)first + count;

	if (!GetConsoleScreenBufferInfoEx(state->output, &information))
		return;
	if (end > 16)
		end = 16;
	for (uint32_t i = first; i < end; ++i) {
		uint32_t colour = colours[i - first];

		information.ColorTable[i] = RGB((colour >> 16) & 0xff,
		    (colour >> 8) & 0xff, colour & 0xff);
	}
	(void)SetConsoleScreenBufferInfoEx(state->output, &information);
}

static int
win32_set_title(st_console_display_t *display, const char *title)
{
	wchar_t *wide = utf8_to_utf16(title);
	int status;

	(void)display;
	if (wide == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = SetConsoleTitleW(wide) ? 0 : IO_ERROR_IO;
	free(wide);
	return status;
}

static int
win32_set_clipboard(st_console_display_t *display, const char *text)
{
	wchar_t *wide = utf8_to_utf16(text);
	HGLOBAL memory;
	wchar_t *target;
	size_t bytes;

	(void)display;
	if (wide == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	bytes = (wcslen(wide) + 1) * sizeof(*wide);
	memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
	if (memory == NULL) {
		free(wide);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	target = GlobalLock(memory);
	if (target == NULL) {
		(void)GlobalFree(memory);
		free(wide);
		return IO_ERROR_IO;
	}
	memcpy(target, wide, bytes);
	(void)GlobalUnlock(memory);
	free(wide);
	if (!OpenClipboard(NULL)) {
		(void)GlobalFree(memory);
		return IO_ERROR_IO;
	}
	if (!EmptyClipboard() || SetClipboardData(CF_UNICODETEXT, memory) == NULL) {
		(void)CloseClipboard();
		(void)GlobalFree(memory);
		return IO_ERROR_IO;
	}
	(void)CloseClipboard();
	return 0;
}

static int
win32_get_clipboard(st_console_display_t *display, char **result)
{
	HANDLE memory;
	const wchar_t *wide;
	char *text;
	size_t count;
	size_t length;

	(void)display;
	if (!OpenClipboard(NULL))
		return IO_ERROR_IO;
	memory = GetClipboardData(CF_UNICODETEXT);
	if (memory == NULL) {
		(void)CloseClipboard();
		return IO_ERROR_NOT_FOUND;
	}
	count = GlobalSize(memory) / sizeof(wchar_t);
	wide = GlobalLock(memory);
	if (wide == NULL) {
		(void)CloseClipboard();
		return IO_ERROR_IO;
	}
	for (length = 0; length < count && wide[length] != L'\0'; ++length)
		;
	text = length < count ? utf16_to_utf8(wide) : NULL;
	(void)GlobalUnlock(memory);
	(void)CloseClipboard();
	if (text == NULL)
		return IO_ERROR_IO;
	*result = text;
	return 0;
}

static int
win32_open_url(st_console_display_t *display, const char *url)
{
	wchar_t *wide = utf8_to_utf16(url);
	int status;

	(void)display;
	if (wide == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = (INT_PTR)ShellExecuteW(NULL, L"open", wide, NULL, NULL,
	    SW_SHOWNORMAL) > 32 ? 0 : IO_ERROR_IO;
	free(wide);
	return status;
}

static int
win32_set_fullscreen(st_console_display_t *display, bool fullscreen)
{
	struct win32_state *state = display->backend_state;
	COORD dimensions;
	DWORD mode = fullscreen ? CONSOLE_FULLSCREEN_MODE : CONSOLE_WINDOWED_MODE;

	if (SetConsoleDisplayMode(state->output, mode, &dimensions))
		return 0;
	DWORD error = GetLastError();

	return error == ERROR_CALL_NOT_IMPLEMENTED || error == ERROR_INVALID_FUNCTION
	    || error == ERROR_NOT_SUPPORTED ? IO_ERROR_NOT_SUPPORTED : IO_ERROR_IO;
}

static void
win32_destroy(st_console_display_t *display)
{
	struct win32_state *state = display->backend_state;

	if (state == NULL)
		return;
	if (state->input_started) {
		(void)SetEvent(state->stop_event);
		(void)st_thread_join(state->input_thread, NULL);
	}
	if (state->input_mode_changed)
		(void)SetConsoleMode(state->input, state->original_input_mode);
	if (state->original_output != INVALID_HANDLE_VALUE)
		(void)SetConsoleActiveScreenBuffer(state->original_output);
	if (state->display_mode_known
	    && state->original_output != INVALID_HANDLE_VALUE) {
		COORD dimensions;
		DWORD mode = (state->original_display_mode & CONSOLE_FULLSCREEN) != 0
		    ? CONSOLE_FULLSCREEN_MODE : CONSOLE_WINDOWED_MODE;

		(void)SetConsoleDisplayMode(state->original_output, mode, &dimensions);
	}
	if (state->output != INVALID_HANDLE_VALUE)
		(void)CloseHandle(state->output);
	if (state->stop_event != NULL)
		(void)CloseHandle(state->stop_event);
	free(state->drawn);
	free(state);
	display->backend_state = NULL;
	atomic_flag_clear_explicit(&win32_in_use, memory_order_release);
}

static int
win32_attach(st_console_t *console)
{
	if (console->pixels != NULL)
		return IO_ERROR_NOT_SUPPORTED;
	if (console->columns > SHRT_MAX || console->rows > SHRT_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	return 0;
}

static uint32_t
win32_capabilities(const st_console_t *console)
{
	(void)console;
	return ST_CONSOLE_CAP_PALETTE_CHANGE
	    | ST_CONSOLE_CAP_BRIGHT_BACKGROUND;
}

static const struct st_console_backend_ops win32_ops = {
	.attach = win32_attach,
	.present = win32_present,
	.resize = win32_resize,
	.palette_changed = win32_palette_changed,
	.capabilities = win32_capabilities,
	.set_title = win32_set_title,
	.set_clipboard_text = win32_set_clipboard,
	.get_clipboard_text = win32_get_clipboard,
	.open_url = win32_open_url,
	.set_fullscreen = win32_set_fullscreen,
	.destroy = win32_destroy,
};

int
st_console_win32_init(st_console_t *console)
{
	struct win32_state *state;
	DWORD input_mode;
	int status;

	status = win32_attach(console);
	if (status != 0)
		return status;
	if (atomic_flag_test_and_set_explicit(&win32_in_use,
	    memory_order_acquire))
		return IO_ERROR_BUSY;
	state = calloc(1, sizeof(*state));
	if (state == NULL) {
		atomic_flag_clear_explicit(&win32_in_use, memory_order_release);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	state->display = console->display;
	state->input = GetStdHandle(STD_INPUT_HANDLE);
	state->original_output = GetStdHandle(STD_OUTPUT_HANDLE);
	state->display_mode_known = GetConsoleDisplayMode(
	    &state->original_display_mode) != FALSE;
	state->output = CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE,
	    FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CONSOLE_TEXTMODE_BUFFER,
	    NULL);
	state->stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
	if (state->input == INVALID_HANDLE_VALUE
	    || state->original_output == INVALID_HANDLE_VALUE
	    || state->output == INVALID_HANDLE_VALUE || state->stop_event == NULL
	    || state->input == NULL || state->original_output == NULL
	    || !GetConsoleMode(state->input, &input_mode)) {
		status = IO_ERROR_NOT_SUPPORTED;
		goto fail;
	}
	state->original_input_mode = input_mode;
	input_mode |= ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT
	    | ENABLE_MOUSE_INPUT;
	input_mode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT
	    | ENABLE_PROCESSED_INPUT | ENABLE_QUICK_EDIT_MODE);
	if (!SetConsoleMode(state->input, input_mode)) {
		status = IO_ERROR_IO;
		goto fail;
	}
	state->input_mode_changed = true;
	if (!SetConsoleActiveScreenBuffer(state->output)) {
		status = IO_ERROR_IO;
		goto fail;
	}
	state->output_activated = true;
	status = set_screen_size(state, console->columns, console->rows);
	if (status != 0)
		goto fail;
	console->display->backend_state = state;
	console->display->backend_ops = &win32_ops;
	if (st_thread_create(&state->input_thread, input_worker, state)
	    != ST_THREAD_SUCCESS) {
		status = IO_ERROR_IO;
		goto fail_attached;
	}
	state->input_started = true;
	status = win32_present(console);
	if (status != 0) {
		win32_destroy(console->display);
		console->display->backend_ops = NULL;
	}
	return status;

fail_attached:
	console->display->backend_state = NULL;
	console->display->backend_ops = NULL;
fail:
	if (state->input_mode_changed)
		(void)SetConsoleMode(state->input, state->original_input_mode);
	if (state->output_activated
	    && state->original_output != INVALID_HANDLE_VALUE)
		(void)SetConsoleActiveScreenBuffer(state->original_output);
	if (state->output != NULL && state->output != INVALID_HANDLE_VALUE)
		(void)CloseHandle(state->output);
	if (state->stop_event != NULL)
		(void)CloseHandle(state->stop_event);
	free(state->drawn);
	free(state);
	atomic_flag_clear_explicit(&win32_in_use, memory_order_release);
	return status;
}

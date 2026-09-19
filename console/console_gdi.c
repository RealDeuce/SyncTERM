/* Native Windows GDI bitmap console backend. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>

#include "console_internal.h"
#include "io_error.h"
#include "st_time.h"
#include "st_windows_utf.h"

#include <limits.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>

#define GDI_COMMAND (WM_APP + 1)
#define GDI_CURSOR_ARROW 32512

enum command_type {
	COMMAND_PRESENT,
	COMMAND_ACTIVATE,
	COMMAND_RESIZE,
	COMMAND_TITLE,
	COMMAND_CLIPBOARD_SET,
	COMMAND_CLIPBOARD_GET,
	COMMAND_OPEN_URL,
	COMMAND_POINTER,
	COMMAND_FULLSCREEN,
	COMMAND_DESTROY,
};

struct command {
	enum command_type type;
	union {
		st_console_t *console;
		const char *text;
		char **text_result;
		struct {
			uint32_t width;
			uint32_t height;
		} size;
		bool enabled;
	} data;
	int result;
};

struct gdi_state {
	/* Only the backend thread changes or dereferences this pointer. */
	st_console_t *console;
	st_console_display_t *console_display;
	struct st_console_bitmap_geometry window_geometry;
	st_thread_t thread;
	st_mutex_t mutex;
	st_condition_t changed;
	bool started;
	int start_status;
	HWND window;
	HICON icon;
	struct st_console_bitmap_presenter *presenter;
	struct st_console_bitmap_frame frame;
	BITMAPINFO bitmap;
	uint32_t *pixels;
	uint32_t pixel_width;
	uint32_t pixel_height;
	uint32_t client_width;
	uint32_t client_height;
	uint32_t minimum_client_width;
	uint32_t minimum_client_height;
	uint32_t windowed_client_width;
	uint32_t windowed_client_height;
	struct st_console_rect damage;
	bool have_damage;
	bool pointer_visible;
	bool fullscreen;
	bool maximized;
	WINDOWPLACEMENT placement;
	DWORD style;
	wchar_t pending_high_surrogate;
};


static st_once_flag_t class_once = ST_ONCE_FLAG_INIT;
static ATOM window_class;

static HICON
create_icon(const struct st_console_icon *icon)
{
	BITMAPINFOHEADER *header;
	unsigned char *resource;
	uint32_t *colours;
	unsigned char *mask;
	size_t colour_size;
	size_t mask_stride;
	size_t mask_size;
	size_t resource_size;
	HICON result;

	if (icon == NULL || icon->width > LONG_MAX
	    || icon->height > LONG_MAX / 2)
		return NULL;
	colour_size = (size_t)icon->width * icon->height * sizeof(*colours);
	mask_stride = ((size_t)icon->width + 31) / 32 * 4;
	mask_size = mask_stride * icon->height;
	if (colour_size > SIZE_MAX - sizeof(*header)
	    || mask_size > SIZE_MAX - sizeof(*header) - colour_size)
		return NULL;
	resource_size = sizeof(*header) + colour_size + mask_size;
	if (resource_size > UINT32_MAX)
		return NULL;
	resource = calloc(1, resource_size);
	if (resource == NULL)
		return NULL;
	header = (BITMAPINFOHEADER *)resource;
	colours = (uint32_t *)&resource[sizeof(*header)];
	mask = &resource[sizeof(*header) + colour_size];
	header->biSize = sizeof(*header);
	header->biWidth = (LONG)icon->width;
	header->biHeight = (LONG)(icon->height * 2);
	header->biPlanes = 1;
	header->biBitCount = 32;
	header->biCompression = BI_RGB;
	header->biSizeImage = (DWORD)colour_size;
	for (uint32_t y = 0; y < icon->height; ++y) {
		const unsigned char *source =
		    &icon->pixels[(size_t)y * icon->stride];
		uint32_t *destination = &colours[(size_t)(icon->height - y - 1)
		    * icon->width];
		unsigned char *mask_row = &mask[(size_t)(icon->height - y - 1)
		    * mask_stride];

		for (uint32_t x = 0; x < icon->width; ++x) {
			const unsigned char *pixel = &source[(size_t)x * 4];

			destination[x] = ((uint32_t)pixel[3] << 24)
			    | ((uint32_t)pixel[0] << 16)
			    | ((uint32_t)pixel[1] << 8) | pixel[2];
			if (pixel[3] <= 127)
				mask_row[x / 8] |= (unsigned char)(0x80U >> (x & 7));
		}
	}
	result = CreateIconFromResourceEx(resource, (DWORD)resource_size, TRUE,
	    0x00030000, (int)icon->width, (int)icon->height, LR_DEFAULTCOLOR);
	free(resource);
	return result;
}

static uint32_t
now_ms(void)
{
	uint64_t value = 0;

	(void)st_time_monotonic_ms(&value);
	return (uint32_t)value;
}

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

static uint32_t
modifiers(void)
{
	uint32_t result = 0;

	if ((GetKeyState(VK_SHIFT) & 0x8000) != 0)
		result |= ST_CONSOLE_MODIFIER_SHIFT;
	if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
		result |= ST_CONSOLE_MODIFIER_CONTROL;
	if ((GetKeyState(VK_MENU) & 0x8000) != 0)
		result |= ST_CONSOLE_MODIFIER_ALT;
	if (((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) != 0)
		result |= ST_CONSOLE_MODIFIER_GUI;
	if ((GetKeyState(VK_CAPITAL) & 1) != 0)
		result |= ST_CONSOLE_MODIFIER_CAPS_LOCK;
	if ((GetKeyState(VK_NUMLOCK) & 1) != 0)
		result |= ST_CONSOLE_MODIFIER_NUM_LOCK;
	return result;
}

static uint16_t
evdev_key(LPARAM value)
{
	uint16_t scan = (uint16_t)((value >> 16) & 0xff);
	bool extended = (value & (1L << 24)) != 0;

	if (!extended)
		return scan;
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
	default: return scan;
	}
}

static uint16_t
translated_key(WPARAM key)
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
add_damage(struct gdi_state *state,
	const struct st_console_rect *rectangle)
{
	st_console_rect_accumulate(&state->damage, &state->have_damage,
	    rectangle);
}

static int
upload(void *context, const struct st_rgb_buffer *pixels,
	const struct st_console_rect *rectangle, const void *image_identity,
	uint64_t generation)
{
	struct gdi_state *state = context;
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
		memcpy(replacement, pixels->pixels,
		    count * sizeof(*replacement));
		free(state->pixels);
		state->pixels = replacement;
		state->pixel_width = pixels->width;
		state->pixel_height = pixels->height;
		state->bitmap.bmiHeader = (BITMAPINFOHEADER){
			.biSize = sizeof(BITMAPINFOHEADER),
			.biWidth = (LONG)pixels->width,
			.biHeight = -(LONG)pixels->height,
			.biPlanes = 1,
			.biBitCount = 32,
			.biCompression = BI_RGB,
		};
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
invalidate_damage(struct gdi_state *state, bool forced)
{
	struct st_console_bitmap_frame *frame = &state->frame;
	RECT rectangle;

	if (forced || !state->have_damage || frame->pixels == NULL
	    || frame->destination.width == 0 || frame->destination.height == 0) {
		(void)InvalidateRect(state->window, NULL, FALSE);
		state->have_damage = false;
		return;
	}
	rectangle.left = (LONG)(frame->destination.x
	    + (uint64_t)state->damage.x * frame->destination.width
	    / frame->pixels->width);
	rectangle.top = (LONG)(frame->destination.y
	    + (uint64_t)state->damage.y * frame->destination.height
	    / frame->pixels->height);
	rectangle.right = (LONG)(frame->destination.x
	    + ((uint64_t)(state->damage.x + state->damage.width)
	    * frame->destination.width + frame->pixels->width - 1)
	    / frame->pixels->width);
	rectangle.bottom = (LONG)(frame->destination.y
	    + ((uint64_t)(state->damage.y + state->damage.height)
	    * frame->destination.height + frame->pixels->height - 1)
	    / frame->pixels->height);
	(void)InvalidateRect(state->window, &rectangle, FALSE);
	state->have_damage = false;
}

static int
render(struct gdi_state *state, const st_console_t *console, bool forced)
{
	int status;

	if (state->client_width == 0 || state->client_height == 0)
		return 0;
	status = st_console_bitmap_presenter_frame(state->presenter, console,
	    state->client_width, state->client_height, now_ms(), upload, state,
	    &state->frame);

	if (status == 0 && (state->frame.changed || forced))
		invalidate_damage(state, forced);
	return status;
}

static void
paint(struct gdi_state *state)
{
	PAINTSTRUCT paint;
	HDC device = BeginPaint(state->window, &paint);
	RECT client;

	(void)GetClientRect(state->window, &client);
	(void)FillRect(device, &client, (HBRUSH)GetStockObject(BLACK_BRUSH));
	if (state->pixels != NULL) {
		int mode = state->frame.scaling_mode == ST_CONSOLE_SCALING_EXTERNAL
		    ? HALFTONE : COLORONCOLOR;

		(void)SetStretchBltMode(device, mode);
		if (mode == HALFTONE)
			(void)SetBrushOrgEx(device, 0, 0, NULL);
		(void)StretchDIBits(device, (int)state->frame.destination.x,
		    (int)state->frame.destination.y,
		    (int)state->frame.destination.width,
		    (int)state->frame.destination.height, 0, 0,
		    (int)state->pixel_width, (int)state->pixel_height,
		    state->pixels, &state->bitmap, DIB_RGB_COLORS, SRCCOPY);
	}
	(void)EndPaint(state->window, &paint);
}

static void
post_mouse(struct gdi_state *state, enum st_console_mouse_action action,
	int x, int y, int wheel_x, int wheel_y)
{
	struct st_console_event event = {
		.type = ST_CONSOLE_EVENT_MOUSE,
		.data.mouse = {
			.action = action,
			.wheel_x = wheel_x,
			.wheel_y = wheel_y,
		},
	};

	if ((GetKeyState(VK_LBUTTON) & 0x8000) != 0)
		event.data.mouse.buttons |= ST_CONSOLE_MOUSE_BUTTON_LEFT;
	if ((GetKeyState(VK_MBUTTON) & 0x8000) != 0)
		event.data.mouse.buttons |= ST_CONSOLE_MOUSE_BUTTON_MIDDLE;
	if ((GetKeyState(VK_RBUTTON) & 0x8000) != 0)
		event.data.mouse.buttons |= ST_CONSOLE_MOUSE_BUTTON_RIGHT;
	if ((GetKeyState(VK_XBUTTON1) & 0x8000) != 0)
		event.data.mouse.buttons |= ST_CONSOLE_MOUSE_BUTTON_X1;
	if ((GetKeyState(VK_XBUTTON2) & 0x8000) != 0)
		event.data.mouse.buttons |= ST_CONSOLE_MOUSE_BUTTON_X2;
	st_console_bitmap_mouse_position(&state->frame, x, y, &event);
	(void)st_console_display_post_event(state->console_display, &event);
}

static int
clipboard_set(HWND window, const char *text)
{
	wchar_t *wide = utf8_to_utf16(text);
	HGLOBAL memory;
	wchar_t *target;
	size_t bytes;

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
	if (!OpenClipboard(window)) {
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
clipboard_get(HWND window, char **result)
{
	HANDLE memory;
	const wchar_t *wide;
	char *text;
	size_t count;
	size_t length;

	if (!OpenClipboard(window))
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
		return IO_ERROR_OUT_OF_MEMORY;
	*result = text;
	return 0;
}

static bool
set_fullscreen(struct gdi_state *state, bool enabled)
{
	if (enabled == state->fullscreen)
		return true;
	if (enabled) {
		MONITORINFO information = {.cbSize = sizeof(information)};

		state->placement = (WINDOWPLACEMENT){
			.length = sizeof(state->placement),
		};
		if (!GetWindowPlacement(state->window, &state->placement)
		    || !GetMonitorInfoW(MonitorFromWindow(state->window,
		    MONITOR_DEFAULTTONEAREST), &information))
			return false;
		if (!state->maximized) {
			state->windowed_client_width = state->client_width;
			state->windowed_client_height = state->client_height;
		}
		state->style = (DWORD)GetWindowLongPtrW(state->window, GWL_STYLE);
		state->fullscreen = true;
		(void)SetWindowLongPtrW(state->window, GWL_STYLE,
		    state->style & ~WS_OVERLAPPEDWINDOW);
		if (!SetWindowPos(state->window, HWND_TOP,
		    information.rcMonitor.left, information.rcMonitor.top,
		    information.rcMonitor.right - information.rcMonitor.left,
		    information.rcMonitor.bottom - information.rcMonitor.top,
		    SWP_FRAMECHANGED | SWP_NOOWNERZORDER))
			goto fail;
	}
	else {
		(void)SetWindowLongPtrW(state->window, GWL_STYLE, state->style);
		if (!SetWindowPlacement(state->window, &state->placement)
		    || !SetWindowPos(state->window, NULL, 0, 0, 0, 0,
		    SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER
		    | SWP_NOOWNERZORDER))
			return false;
		state->fullscreen = false;
		{
			RECT client;

			if (GetClientRect(state->window, &client)) {
				state->client_width = (uint32_t)client.right;
				state->client_height = (uint32_t)client.bottom;
			}
		}
	}
	return true;

fail:
	(void)SetWindowLongPtrW(state->window, GWL_STYLE, state->style);
	state->fullscreen = false;
	return false;
}

static void
snap_window(struct gdi_state *state, bool larger)
{
	MONITORINFO information = {.cbSize = sizeof(information)};
	RECT frame = {0};
	RECT rectangle;
	DWORD style;
	uint32_t maximum_width = 0;
	uint32_t maximum_height = 0;
	uint32_t width;
	uint32_t height;

	if (state->fullscreen || state->maximized)
		return;
	style = (DWORD)GetWindowLongPtrW(state->window, GWL_STYLE);
	if (GetMonitorInfoW(MonitorFromWindow(state->window,
	    MONITOR_DEFAULTTONEAREST), &information)
	    && AdjustWindowRectEx(&frame, style, FALSE, 0)) {
		LONG available_width = information.rcWork.right
		    - information.rcWork.left - (frame.right - frame.left);
		LONG available_height = information.rcWork.bottom
		    - information.rcWork.top - (frame.bottom - frame.top);

		if (available_width > 0)
			maximum_width = (uint32_t)available_width;
		if (available_height > 0)
			maximum_height = (uint32_t)available_height;
	}
	if (st_console_snap_geometry_size(&state->window_geometry, state->client_width,
	    state->client_height, larger, maximum_width, maximum_height,
	    &width, &height) != 0
	    || (width == state->client_width && height == state->client_height)
	    || width > LONG_MAX || height > LONG_MAX)
		return;
	rectangle = (RECT){0, 0, (LONG)width, (LONG)height};
	if (!AdjustWindowRectEx(&rectangle, style, FALSE, 0))
		return;
	if (SetWindowPos(state->window, NULL, 0, 0,
	    rectangle.right - rectangle.left, rectangle.bottom - rectangle.top,
	    SWP_NOMOVE | SWP_NOZORDER)) {
		state->windowed_client_width = width;
		state->windowed_client_height = height;
	}
}

static void
center_window(struct gdi_state *state)
{
	MONITORINFO information = {.cbSize = sizeof(information)};
	RECT rectangle;
	LONG width;
	LONG height;
	LONG x;
	LONG y;

	if (state->fullscreen || state->maximized
	    || !GetWindowRect(state->window, &rectangle)
	    || !GetMonitorInfoW(MonitorFromWindow(state->window,
	    MONITOR_DEFAULTTONEAREST), &information))
		return;
	width = rectangle.right - rectangle.left;
	height = rectangle.bottom - rectangle.top;
	x = information.rcWork.left
	    + (information.rcWork.right - information.rcWork.left - width) / 2;
	y = information.rcWork.top
	    + (information.rcWork.bottom - information.rcWork.top - height) / 2;
	(void)SetWindowPos(state->window, NULL, x, y, 0, 0,
	    SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOOWNERZORDER | SWP_NOZORDER);
}

static void
post_character(struct gdi_state *state, uint32_t codepoint)
{
	if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
		state->pending_high_surrogate = (wchar_t)codepoint;
		return;
	}
	if (codepoint >= 0xdc00 && codepoint <= 0xdfff
	    && state->pending_high_surrogate != 0) {
		codepoint = UINT32_C(0x10000)
		    + (((uint32_t)state->pending_high_surrogate - 0xd800) << 10)
		    + codepoint - 0xdc00;
	}
	else if (codepoint >= 0xd800 && codepoint <= 0xdfff)
		codepoint = 0;
	state->pending_high_surrogate = 0;
	if (codepoint >= 0x20 && codepoint != 0x7f
	    && codepoint <= UINT32_C(0x10ffff))
		(void)st_console_display_post_event(state->console_display,
		    &(struct st_console_event){
			.type = ST_CONSOLE_EVENT_TEXT,
			.data.text = {
				.codepoint = codepoint,
				.modifiers = modifiers(),
			},
		    });
}

static void
post_key(struct gdi_state *state, UINT message, WPARAM key, LPARAM detail)
{
	bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
	uint16_t physical = evdev_key(detail);
	uint16_t translated = translated_key(key);
	uint32_t key_modifiers = modifiers();

	if (physical != 0)
		(void)st_console_display_post_event(state->console_display,
		    &(struct st_console_event){
			.type = ST_CONSOLE_EVENT_PHYSICAL_KEY,
			.data.physical_key = {
				.evdev = physical,
				.modifiers = key_modifiers,
				.pressed = down,
				.repeat = down && (detail & (1L << 30)) != 0,
			},
		    });
	if (down && (key_modifiers & ST_CONSOLE_MODIFIER_ALT) != 0
	    && (key_modifiers & ST_CONSOLE_MODIFIER_CONTROL) == 0) {
		if (key == VK_RETURN && (detail & (1L << 30)) == 0) {
			(void)set_fullscreen(state, !state->fullscreen);
			return;
		}
		if (key == VK_LEFT) {
			snap_window(state, false);
			return;
		}
		if (key == VK_RIGHT) {
			snap_window(state, true);
			return;
		}
		if (key == VK_HOME) {
			center_window(state);
			return;
		}
		if (key == VK_F4) {
			(void)st_console_display_post_event(state->console_display,
			    &(struct st_console_event){
				.type = ST_CONSOLE_EVENT_CLOSE,
			    });
			return;
		}
	}
	if (down && translated != 0)
		(void)st_console_display_post_event(state->console_display,
		    &(struct st_console_event){
			.type = ST_CONSOLE_EVENT_KEY_CODE,
			.data.key = {
				.code = translated,
				.modifiers = key_modifiers,
			},
		    });
}

static LRESULT
constrain_sizing(struct gdi_state *state, WPARAM edge, RECT *rectangle)
{
	RECT frame = {0};
	uint32_t proposed_width;
	uint32_t proposed_height;
	uint32_t width;
	uint32_t height;
	uint32_t minimum_width;
	uint32_t minimum_height;
	LONG frame_width;
	LONG frame_height;

	if (!AdjustWindowRectEx(&frame,
	    (DWORD)GetWindowLongPtrW(state->window, GWL_STYLE), FALSE,
	    (DWORD)GetWindowLongPtrW(state->window, GWL_EXSTYLE)))
		return FALSE;
	frame_width = frame.right - frame.left;
	frame_height = frame.bottom - frame.top;
	if (rectangle->right - rectangle->left <= frame_width
	    || rectangle->bottom - rectangle->top <= frame_height)
		return FALSE;
	proposed_width = (uint32_t)(rectangle->right - rectangle->left
	    - frame_width);
	proposed_height = (uint32_t)(rectangle->bottom - rectangle->top
	    - frame_height);
	if (st_console_preserve_geometry_scale(&state->window_geometry,
	    &state->window_geometry,
	    proposed_width, proposed_height, &width, &height, &minimum_width,
	    &minimum_height) != 0)
		return FALSE;
	width += (uint32_t)frame_width;
	height += (uint32_t)frame_height;
	if (edge == WMSZ_LEFT || edge == WMSZ_TOPLEFT
	    || edge == WMSZ_BOTTOMLEFT)
		rectangle->left = rectangle->right - (LONG)width;
	else
		rectangle->right = rectangle->left + (LONG)width;
	if (edge == WMSZ_TOP || edge == WMSZ_TOPLEFT
	    || edge == WMSZ_TOPRIGHT)
		rectangle->top = rectangle->bottom - (LONG)height;
	else
		rectangle->bottom = rectangle->top + (LONG)height;
	return TRUE;
}

static LRESULT
process_command(struct gdi_state *state, struct command *command)
{
	RECT rectangle;
	wchar_t *wide;

	switch (command->type) {
	case COMMAND_ACTIVATE:
		state->console = command->data.console;
		command->result = 0;
		break;
	case COMMAND_PRESENT:
		command->result = render(state, state->console, false);
		break;
	case COMMAND_RESIZE:
		{
		struct st_console_bitmap_geometry next = {
			command->data.size.width, command->data.size.height,
			state->console->aspect_width, state->console->aspect_height};
		uint32_t width;
		uint32_t height;
		uint32_t current_width = state->fullscreen || state->maximized
		    ? state->windowed_client_width : state->client_width;
		uint32_t current_height = state->fullscreen || state->maximized
		    ? state->windowed_client_height : state->client_height;

		if (st_console_preserve_geometry_scale(&state->window_geometry, &next,
		    current_width, current_height, &width, &height,
		    &state->minimum_client_width,
		    &state->minimum_client_height) != 0
		    || width > LONG_MAX || height > LONG_MAX) {
			command->result = IO_ERROR_OUT_OF_RANGE;
			break;
		}
		rectangle = (RECT){0, 0, (LONG)width, (LONG)height};
		if (!AdjustWindowRectEx(&rectangle, state->fullscreen ? state->style
		    : (DWORD)GetWindowLongPtrW(state->window, GWL_STYLE), FALSE, 0))
			command->result = IO_ERROR_IO;
		else if (state->fullscreen) {
			state->windowed_client_width = width;
			state->windowed_client_height = height;
			state->placement.rcNormalPosition.right
			    = state->placement.rcNormalPosition.left
			    + rectangle.right - rectangle.left;
			state->placement.rcNormalPosition.bottom
			    = state->placement.rcNormalPosition.top
			    + rectangle.bottom - rectangle.top;
			command->result = 0;
		}
		else if (state->maximized) {
			WINDOWPLACEMENT placement = {.length = sizeof(placement)};

			state->windowed_client_width = width;
			state->windowed_client_height = height;
			if (!GetWindowPlacement(state->window, &placement))
				command->result = IO_ERROR_IO;
			else {
				placement.rcNormalPosition.right
				    = placement.rcNormalPosition.left
				    + rectangle.right - rectangle.left;
				placement.rcNormalPosition.bottom
				    = placement.rcNormalPosition.top
				    + rectangle.bottom - rectangle.top;
				command->result = SetWindowPlacement(state->window,
				    &placement) ? 0 : IO_ERROR_IO;
			}
		}
		else
			command->result = SetWindowPos(state->window, NULL, 0, 0,
			    rectangle.right - rectangle.left,
			    rectangle.bottom - rectangle.top,
			    SWP_NOMOVE | SWP_NOZORDER) ? 0 : IO_ERROR_IO;
		if (command->result == 0)
			state->window_geometry = next;
		break;
		}
	case COMMAND_TITLE:
		wide = utf8_to_utf16(command->data.text);
		command->result = wide != NULL
		    && SetWindowTextW(state->window, wide)
		    ? 0 : IO_ERROR_INVALID_ARGUMENT;
		free(wide);
		break;
	case COMMAND_CLIPBOARD_SET:
		command->result = clipboard_set(state->window, command->data.text);
		break;
	case COMMAND_CLIPBOARD_GET:
		command->result = clipboard_get(state->window,
		    command->data.text_result);
		break;
	case COMMAND_OPEN_URL:
		wide = utf8_to_utf16(command->data.text);
		if (wide == NULL)
			command->result = IO_ERROR_INVALID_ARGUMENT;
		else
			command->result = (INT_PTR)ShellExecuteW(state->window, L"open",
			    wide, NULL, NULL, SW_SHOWNORMAL) > 32 ? 0 : IO_ERROR_IO;
		free(wide);
		break;
	case COMMAND_POINTER:
		state->pointer_visible = command->data.enabled;
		(void)SetCursor(state->pointer_visible ? LoadCursorW(NULL,
		    MAKEINTRESOURCEW(GDI_CURSOR_ARROW)) : NULL);
		command->result = 0;
		break;
	case COMMAND_FULLSCREEN:
		command->result = set_fullscreen(state, command->data.enabled)
		    ? 0 : IO_ERROR_IO;
		break;
	case COMMAND_DESTROY:
		command->result = DestroyWindow(state->window) ? 0 : IO_ERROR_IO;
		break;
	}
	return 0;
}

static LRESULT CALLBACK
window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	struct gdi_state *state = (struct gdi_state *)GetWindowLongPtrW(window,
	    GWLP_USERDATA);

	if (message == WM_NCCREATE) {
		state = ((CREATESTRUCTW *)lparam)->lpCreateParams;
		(void)SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)state);
		state->window = window;
	}
	if (state == NULL)
		return DefWindowProcW(window, message, wparam, lparam);
	switch (message) {
	case WM_GETMINMAXINFO: {
		MINMAXINFO *information = (MINMAXINFO *)lparam;
		RECT rectangle = {0, 0, (LONG)state->minimum_client_width,
		    (LONG)state->minimum_client_height};

		if (AdjustWindowRectEx(&rectangle,
		    (DWORD)GetWindowLongPtrW(window, GWL_STYLE), FALSE,
		    (DWORD)GetWindowLongPtrW(window, GWL_EXSTYLE))) {
			information->ptMinTrackSize.x = rectangle.right
			    - rectangle.left;
			information->ptMinTrackSize.y = rectangle.bottom
			    - rectangle.top;
		}
		return 0;
	}
	case WM_SIZING:
		return constrain_sizing(state, wparam, (RECT *)lparam);
	case WM_PAINT:
		paint(state);
		return 0;
	case WM_ERASEBKGND:
		return 1;
	case WM_SIZE:
		{
		RECT client;

		if (!state->fullscreen) {
			if (wparam == SIZE_MAXIMIZED)
				state->maximized = true;
			else if (wparam == SIZE_RESTORED)
				state->maximized = false;
		}

		if (GetClientRect(window, &client)) {
			state->client_width = client.right > 0
			    ? (uint32_t)client.right : 0;
			state->client_height = client.bottom > 0
			    ? (uint32_t)client.bottom : 0;
		}
		else {
			state->client_width = 0;
			state->client_height = 0;
		}
		if (!state->fullscreen && !state->maximized) {
			state->windowed_client_width = state->client_width;
			state->windowed_client_height = state->client_height;
		}
		}
		(void)render(state, NULL, true);
		return 0;
	case WM_SETFOCUS:
	case WM_KILLFOCUS:
		(void)st_console_display_post_event(state->console_display,
		    &(struct st_console_event){
			.type = ST_CONSOLE_EVENT_FOCUS,
			.data.focus.focused = message == WM_SETFOCUS,
		    });
		return 0;
	case WM_CLOSE:
		(void)st_console_display_post_event(state->console_display,
		    &(struct st_console_event){.type = ST_CONSOLE_EVENT_CLOSE});
		return 0;
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
	case WM_KEYUP:
	case WM_SYSKEYUP:
		post_key(state, message, wparam, lparam);
		return 0;
	case WM_CHAR:
		post_character(state, (uint32_t)wparam);
		return 0;
#ifdef WM_UNICHAR
	case WM_UNICHAR:
		if (wparam == UNICODE_NOCHAR)
			return TRUE;
		post_character(state, (uint32_t)wparam);
		return 0;
#endif
	case WM_MOUSEMOVE:
		post_mouse(state, ST_CONSOLE_MOUSE_MOVE, GET_X_LPARAM(lparam),
		    GET_Y_LPARAM(lparam), 0, 0);
		return 0;
	case WM_LBUTTONDOWN:
	case WM_MBUTTONDOWN:
	case WM_RBUTTONDOWN:
	case WM_XBUTTONDOWN:
		(void)SetCapture(window);
		post_mouse(state, ST_CONSOLE_MOUSE_BUTTON, GET_X_LPARAM(lparam),
		    GET_Y_LPARAM(lparam), 0, 0);
		return 0;
	case WM_LBUTTONUP:
	case WM_MBUTTONUP:
	case WM_RBUTTONUP:
	case WM_XBUTTONUP:
		if ((wparam & (MK_LBUTTON | MK_MBUTTON | MK_RBUTTON
		    | MK_XBUTTON1 | MK_XBUTTON2)) == 0)
			(void)ReleaseCapture();
		post_mouse(state, ST_CONSOLE_MOUSE_BUTTON, GET_X_LPARAM(lparam),
		    GET_Y_LPARAM(lparam), 0, 0);
		return 0;
	case WM_MOUSEWHEEL:
	case WM_MOUSEHWHEEL: {
		POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
		int amount = GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA;

		(void)ScreenToClient(window, &point);
		post_mouse(state, ST_CONSOLE_MOUSE_WHEEL, point.x, point.y,
		    message == WM_MOUSEHWHEEL ? amount : 0,
		    message == WM_MOUSEWHEEL ? amount : 0);
		return 0;
	}
	case WM_SETCURSOR:
		(void)SetCursor(state->pointer_visible ? LoadCursorW(NULL,
		    MAKEINTRESOURCEW(GDI_CURSOR_ARROW)) : NULL);
		return TRUE;
	case GDI_COMMAND:
		return process_command(state, (struct command *)lparam);
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	default:
		return DefWindowProcW(window, message, wparam, lparam);
	}
}

static void
register_class(void)
{
	WNDCLASSEXW definition = {
		.cbSize = sizeof(definition),
		.style = CS_HREDRAW | CS_VREDRAW,
		.lpfnWndProc = window_proc,
		.hInstance = GetModuleHandleW(NULL),
		.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(GDI_CURSOR_ARROW)),
		.lpszClassName = L"STConsoleGDI",
	};

	window_class = RegisterClassExW(&definition);
	if (window_class == 0 && GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
		window_class = 1;
}

static int
worker(void *argument)
{
	struct gdi_state *state = argument;
	MSG message;
	RECT rectangle;
	uint32_t minimum_width;
	uint32_t minimum_height;
	uint32_t width = state->console->display->initial_window_width;
	uint32_t height = state->console->display->initial_window_height;

	if (st_console_aspect_correct_size(state->console->pixels->width,
	    state->console->pixels->height, state->console->aspect_width,
	    state->console->aspect_height, &minimum_width, &minimum_height) != 0
	    || width > LONG_MAX || height > LONG_MAX) {
		(void)st_mutex_lock(&state->mutex);
		state->start_status = IO_ERROR_OUT_OF_RANGE;
		state->started = true;
		(void)st_condition_broadcast(&state->changed);
		(void)st_mutex_unlock(&state->mutex);
		return 1;
	}
	state->minimum_client_width = minimum_width;
	state->minimum_client_height = minimum_height;
	state->windowed_client_width = width;
	state->windowed_client_height = height;
	rectangle = (RECT){0, 0, (LONG)width, (LONG)height};

	st_call_once(&class_once, register_class);
	if (window_class != 0) {
		(void)AdjustWindowRect(&rectangle, WS_OVERLAPPEDWINDOW, FALSE);
		state->window = CreateWindowExW(0, L"STConsoleGDI", L"SyncTERM",
		    WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
		    rectangle.right - rectangle.left,
		    rectangle.bottom - rectangle.top, NULL, NULL,
		    GetModuleHandleW(NULL), state);
	}
	if (state->window != NULL) {
		state->icon = create_icon(state->console_display->config.icon);
		if (state->icon != NULL) {
			(void)SendMessageW(state->window, WM_SETICON, ICON_SMALL,
			    (LPARAM)state->icon);
			(void)SendMessageW(state->window, WM_SETICON, ICON_BIG,
			    (LPARAM)state->icon);
		}
	}
	(void)st_mutex_lock(&state->mutex);
	state->start_status = state->window != NULL ? 0 : IO_ERROR_IO;
	state->started = true;
	(void)st_condition_broadcast(&state->changed);
	(void)st_mutex_unlock(&state->mutex);
	if (state->window == NULL)
		return 1;
	while (GetMessageW(&message, NULL, 0, 0) > 0) {
		(void)TranslateMessage(&message);
		(void)DispatchMessageW(&message);
	}
	if (state->icon != NULL)
		(void)DestroyIcon(state->icon);
	return 0;
}

static int
send_command(struct gdi_state *state, struct command *command)
{
	(void)SendMessageW(state->window, GDI_COMMAND, 0, (LPARAM)command);
	return command->result;
}

static int
gdi_activate(st_console_t *console)
{
	return send_command(console->display->backend_state, &(struct command){
		.type = COMMAND_ACTIVATE,
		.data.console = console,
	});
}

static int
gdi_present(st_console_t *console)
{
	return send_command(console->display->backend_state,
	    &(struct command){.type = COMMAND_PRESENT});
}

static int
gdi_resize(st_console_t *console, uint32_t columns, uint32_t rows)
{
	uint32_t width;
	uint32_t height;

	if (columns > (UINT32_MAX - console->pixel_extra_width)
	    / console->cell_width
	    || rows > (UINT32_MAX - console->pixel_extra_height)
	    / console->cell_height)
		return IO_ERROR_OUT_OF_RANGE;
	width = columns * console->cell_width + console->pixel_extra_width;
	height = rows * console->cell_height + console->pixel_extra_height;
	if (width > LONG_MAX || height > LONG_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	return send_command(console->display->backend_state, &(struct command){
		.type = COMMAND_RESIZE,
		.data.size = {width, height},
	});
}

static int
gdi_set_title(st_console_display_t *display, const char *title)
{
	return send_command(display->backend_state, &(struct command){
		.type = COMMAND_TITLE,
		.data.text = title,
	});
}

static int
gdi_set_clipboard(st_console_display_t *display, const char *text)
{
	return send_command(display->backend_state, &(struct command){
		.type = COMMAND_CLIPBOARD_SET,
		.data.text = text,
	});
}

static int
gdi_get_clipboard(st_console_display_t *display, char **result)
{
	return send_command(display->backend_state, &(struct command){
		.type = COMMAND_CLIPBOARD_GET,
		.data.text_result = result,
	});
}

static int
gdi_open_url(st_console_display_t *display, const char *url)
{
	return send_command(display->backend_state, &(struct command){
		.type = COMMAND_OPEN_URL,
		.data.text = url,
	});
}

static int
gdi_set_pointer(st_console_display_t *display, bool visible)
{
	return send_command(display->backend_state, &(struct command){
		.type = COMMAND_POINTER,
		.data.enabled = visible,
	});
}

static int
gdi_set_fullscreen(st_console_display_t *display, bool fullscreen)
{
	return send_command(display->backend_state, &(struct command){
		.type = COMMAND_FULLSCREEN,
		.data.enabled = fullscreen,
	});
}

static void
gdi_destroy(st_console_display_t *display)
{
	struct gdi_state *state = display->backend_state;

	(void)send_command(state, &(struct command){
		.type = COMMAND_DESTROY,
	});
	(void)st_thread_join(state->thread, NULL);
	st_console_bitmap_presenter_destroy(state->presenter);
	free(state->pixels);
	st_condition_destroy(&state->changed);
	st_mutex_destroy(&state->mutex);
	free(state);
	display->backend_state = NULL;
}

static int
gdi_attach(st_console_t *console)
{
	return st_console_bitmap_validate_geometry(console, LONG_MAX);
}

static const struct st_console_backend_ops gdi_ops = {
	.attach = gdi_attach,
	.presents_pixels = true,
	.deferred_present = true,
	.animated_present = true,
	.present = gdi_present,
	.resize = gdi_resize,
	.set_title = gdi_set_title,
	.set_clipboard_text = gdi_set_clipboard,
	.get_clipboard_text = gdi_get_clipboard,
	.open_url = gdi_open_url,
	.set_pointer_visible = gdi_set_pointer,
	.set_fullscreen = gdi_set_fullscreen,
	.activate = gdi_activate,
	.destroy = gdi_destroy,
};

int
st_console_gdi_init(st_console_t *console)
{
	struct gdi_state *state;
	int status;

	status = st_console_bitmap_validate_geometry(console, LONG_MAX);
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
	if (st_mutex_init(&state->mutex, ST_MUTEX_PLAIN) != ST_THREAD_SUCCESS) {
		free(state);
		return IO_ERROR_IO;
	}
	if (st_condition_init(&state->changed) != ST_THREAD_SUCCESS) {
		st_mutex_destroy(&state->mutex);
		free(state);
		return IO_ERROR_IO;
	}
	status = st_console_bitmap_presenter_create(&state->presenter, console);
	if (status == 0 && st_thread_create(&state->thread, worker, state)
	    != ST_THREAD_SUCCESS)
		status = IO_ERROR_IO;
	if (status == 0) {
		(void)st_mutex_lock(&state->mutex);
		while (!state->started)
			(void)st_condition_wait(&state->changed, &state->mutex);
		status = state->start_status;
		(void)st_mutex_unlock(&state->mutex);
		if (status != 0)
			(void)st_thread_join(state->thread, NULL);
	}
	if (status != 0) {
		st_console_bitmap_presenter_destroy(state->presenter);
		st_condition_destroy(&state->changed);
		st_mutex_destroy(&state->mutex);
		free(state);
		return status;
	}
	console->display->backend_state = state;
	console->display->backend_ops = &gdi_ops;
	status = gdi_present(console);
	if (status != 0) {
		gdi_destroy(console->display);
		console->display->backend_ops = NULL;
	}
	return status;
}

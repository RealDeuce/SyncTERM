/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "console_internal.h"

#include "io_error.h"
#include "st_threads.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", \
		    __FILE__, __LINE__, #expression); \
		exit(EXIT_FAILURE); \
	} \
} while (false)

struct output {
	unsigned char data[65536];
	size_t length;
	size_t writes;
	bool fail;
};

static bool
contains(const unsigned char *data, size_t length, const char *needle)
{
	size_t needle_length = strlen(needle);

	if (needle_length > length)
		return false;
	for (size_t i = 0; i <= length - needle_length; ++i)
		if (memcmp(&data[i], needle, needle_length) == 0)
			return true;
	return false;
}

static void
test_control_keys(void)
{
	uint16_t key = UINT16_MAX;

	CHECK(st_console_control_key('a', &key) && key == 1);
	CHECK(st_console_control_key('Z', &key) && key == 26);
	CHECK(st_console_control_key('[', &key) && key == 27);
	CHECK(st_console_control_key('\\', &key) && key == 28);
	CHECK(st_console_control_key(']', &key) && key == 29);
	CHECK(st_console_control_key('^', &key) && key == 30);
	CHECK(st_console_control_key('_', &key) && key == 31);
	CHECK(st_console_control_key(' ', &key) && key == 0);
	CHECK(!st_console_control_key('1', &key));
}

static size_t
count_bytes(const unsigned char *data, size_t length,
	const unsigned char *needle, size_t needle_length)
{
	size_t count = 0;

	if (needle_length == 0 || needle_length > length)
		return 0;
	for (size_t i = 0; i <= length - needle_length; ++i)
		if (memcmp(&data[i], needle, needle_length) == 0)
			++count;
	return count;
}

static int
write_output(void *context, const void *data, size_t size)
{
	struct output *output = context;

	if (output->fail)
		return IO_ERROR_IO;
	if (size > sizeof(output->data) - output->length)
		return IO_ERROR_NO_SPACE;
	memcpy(&output->data[output->length], data, size);
	output->length += size;
	++output->writes;
	return 0;
}

static st_console_t *
create_headless(uint32_t columns, uint32_t rows)
{
	struct st_console_display_config config_display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	struct st_console_config config = {
		.columns = columns,
		.rows = rows,
		.wrap = true,
	};
	st_console_t *console = NULL;
	enum st_console_backend backend;

	CHECK(st_console_create(&console, &config_display, &config) == 0);
	CHECK(console != NULL);
	CHECK(st_console_get_backend(console, &backend) == 0);
	CHECK(backend == ST_CONSOLE_BACKEND_HEADLESS);
	CHECK(strcmp(st_console_backend_name(backend), "Headless") == 0);
	return console;
}

static uint16_t
register_static_font(st_console_t *console, const struct st_console_font *font)
{
	uint16_t identifier = UINT16_MAX;

	CHECK(st_console_register_static_font(console, font, &identifier) == 0);
	return identifier;
}

static void
check_text_state(st_console_t *console, const struct st_console_rect *rect,
	const struct st_console_cell *expected, size_t stride,
	uint32_t expected_x, uint32_t expected_y)
{
	struct st_console_cell *actual;
	uint32_t x;
	uint32_t y;
	size_t count = stride * rect->height;

	actual = malloc(count * sizeof(*actual));
	CHECK(actual != NULL);
	CHECK(st_console_read_cells(console, rect, actual, stride) == 0);
	CHECK(memcmp(actual, expected, count * sizeof(*actual)) == 0);
	free(actual);
	CHECK(st_console_get_position(console, &x, &y) == 0);
	CHECK(x == expected_x && y == expected_y);
}

static void
test_default_cursor_geometry(void)
{
	struct st_console_display_config config_display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	struct st_console_config config = {
		.columns = 1,
		.rows = 1,
		.cell_width = 8,
		.cell_height = 14,
		.default_cursor = {
			.start = 12, .end = 13, .range = 14,
			.visible = true, .blink = false,
		},
	};
	struct st_console_custom_cursor cursor;
	st_console_t *console = NULL;
	bool visible;
	enum st_console_cursor_shape shape;

	CHECK(st_console_create(&console, &config_display, &config) == 0);
	CHECK(st_console_get_default_cursor(console, &cursor) == 0);
	CHECK(cursor.start == 12 && cursor.end == 13 && cursor.range == 14
	    && cursor.visible && !cursor.blink);
	CHECK(st_console_get_custom_cursor(console, &cursor) == 0);
	CHECK(cursor.start == 12 && cursor.end == 13 && cursor.range == 14
	    && !cursor.blink);
	CHECK(st_console_get_cursor(console, &visible, &shape) == 0);
	CHECK(visible && shape == ST_CONSOLE_CURSOR_UNDERLINE);
	CHECK(st_console_set_cursor(console, true,
	    ST_CONSOLE_CURSOR_UNDERLINE) == 0);
	CHECK(st_console_get_custom_cursor(console, &cursor) == 0);
	CHECK(cursor.start == 12 && cursor.end == 13 && cursor.range == 14
	    && cursor.blink);
	CHECK(st_console_set_cursor(console, true, ST_CONSOLE_CURSOR_BLOCK) == 0);
	CHECK(st_console_get_custom_cursor(console, &cursor) == 0);
	CHECK(cursor.start == 0 && cursor.end == 13 && cursor.range == 14);
	st_console_destroy(console);
}

static void
test_video_flags(void)
{
	struct st_console_display_config display_config = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	struct st_console_config config = {
		.columns = 1,
		.rows = 1,
		.video_flags = ST_CONSOLE_VIDEO_BRIGHT_BACKGROUND
		    | ST_CONSOLE_VIDEO_EXPAND,
	};
	struct st_console_cell source = {
		.foreground = 7,
		.background = 1,
		.flags = ST_CONSOLE_CELL_BRIGHT | ST_CONSOLE_CELL_BLINK
		    | ST_CONSOLE_CELL_LEGACY_FOREGROUND
		    | ST_CONSOLE_CELL_LEGACY_BACKGROUND,
	};
	struct st_console_cell effective;
	st_console_snapshot_t *snapshot = NULL;
	st_console_t *console = NULL;
	uint32_t flags;

	CHECK(st_console_create(&console, &display_config, &config) == 0);
	CHECK(st_console_get_video_flags(console, &flags) == 0);
	CHECK(flags == config.video_flags);
	st_console_effective_cell_locked(console, &source, &effective);
	CHECK(effective.foreground == 15 && effective.background == 9);
	CHECK((effective.flags & ST_CONSOLE_CELL_BLINK) != 0);
	CHECK(st_console_snapshot_create(&snapshot, console) == 0);
	flags |= ST_CONSOLE_VIDEO_NO_BRIGHT | ST_CONSOLE_VIDEO_NO_BLINK;
	CHECK(st_console_set_video_flags(console, flags) == 0);
	st_console_effective_cell_locked(console, &source, &effective);
	CHECK(effective.foreground == 7 && effective.background == 9);
	CHECK((effective.flags & ST_CONSOLE_CELL_BLINK) == 0);
	CHECK(st_console_snapshot_restore(console, snapshot) == 0);
	CHECK(st_console_get_video_flags(console, &flags) == 0);
	CHECK(flags == config.video_flags);
	CHECK(st_console_set_video_flags(console,
	    flags & ~ST_CONSOLE_VIDEO_EXPAND) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_console_set_video_flags(console,
	    flags | (UINT32_C(1) << 31)) == IO_ERROR_INVALID_ARGUMENT);
	st_console_snapshot_destroy(snapshot);
	st_console_destroy(console);
}

static struct st_console_cell
read_cell(st_console_t *console, uint32_t x, uint32_t y)
{
	struct st_console_rect rect = {x, y, 1, 1};
	struct st_console_cell cell;

	CHECK(st_console_read_cells(console, &rect, &cell, 1) == 0);
	return cell;
}

static void
test_cell_and_regions(void)
{
	st_console_t *console = create_headless(70000, 2);
	struct st_console_cell source[4] = {
		{.glyph = 'a', .foreground = 1, .background = 2},
		{.glyph = 'b', .foreground = 3, .background = 4,
		    .flags = ST_CONSOLE_CELL_SAA5050
		    | ST_CONSOLE_CELL_SAA5050_CONTROL(0x1d), .font = 2},
		{.glyph = 'c', .foreground = ST_CONSOLE_COLOR_RGB_VALUE(1, 2, 3),
		    .background = 5, .hyperlink = 19},
		{.glyph = 'd', .foreground = 6, .background = 7,
		    .flags = ST_CONSOLE_CELL_DIRTY},
	};
	struct st_console_cell result[6] = {0};
	struct st_console_rect rect = {69998, 0, 2, 2};
	uint32_t columns;
	uint32_t rows;

	CHECK(sizeof(struct st_console_cell) == 24);
	CHECK(offsetof(struct st_console_cell, reserved) == 22);
	source[0].reserved = UINT16_MAX;
	CHECK(st_console_size(console, &columns, &rows) == 0);
	CHECK(columns == 70000 && rows == 2);
	CHECK(st_console_write_cells(console, &rect, source, 2) == 0);
	CHECK(st_console_read_cells(console, &rect, result, 3) == 0);
	CHECK(result[0].glyph == 'a' && result[1].glyph == 'b');
	CHECK(result[0].reserved == 0);
	CHECK(result[3].glyph == 'c' && result[4].glyph == 'd');
	CHECK((result[1].flags & ST_CONSOLE_CELL_SAA5050_CONTROL_MASK)
	    == ST_CONSOLE_CELL_SAA5050_CONTROL(0x1d));
	CHECK(result[1].glyph == 'b');
	CHECK((result[4].flags & ST_CONSOLE_CELL_DIRTY) == 0);
	CHECK(st_console_write_cells(console, &rect, source, 1)
	    == IO_ERROR_OUT_OF_RANGE);
	st_console_destroy(console);
}

static void
test_build_features(void)
{
	uint32_t known = ST_CONSOLE_BUILD_ANSI | ST_CONSOLE_BUILD_NCURSES
	    | ST_CONSOLE_BUILD_WIN32 | ST_CONSOLE_BUILD_GDI
	    | ST_CONSOLE_BUILD_SDL | ST_CONSOLE_BUILD_X11
	    | ST_CONSOLE_BUILD_XRENDER | ST_CONSOLE_BUILD_XRANDR
	    | ST_CONSOLE_BUILD_XINERAMA | ST_CONSOLE_BUILD_WAYLAND
	    | ST_CONSOLE_BUILD_QUARTZ | ST_CONSOLE_BUILD_LIBRETRO;
	uint32_t features = st_console_build_features();

	CHECK((features & ~known) == 0);
	CHECK((features & ST_CONSOLE_BUILD_ANSI) != 0);
	CHECK((features & ST_CONSOLE_BUILD_LIBRETRO) != 0);
	CHECK((features & ST_CONSOLE_BUILD_XRENDER) == 0
	    || (features & ST_CONSOLE_BUILD_X11) != 0);
	CHECK((features & ST_CONSOLE_BUILD_XRANDR) == 0
	    || (features & ST_CONSOLE_BUILD_X11) != 0);
	CHECK((features & ST_CONSOLE_BUILD_XINERAMA) == 0
	    || (features & ST_CONSOLE_BUILD_X11) != 0);
}

static void
test_text_window_and_scroll(void)
{
	st_console_t *console = create_headless(8, 4);
	struct st_console_rect window = {2, 1, 4, 2};
	struct st_console_style style = {
		.foreground = 12,
		.background = ST_CONSOLE_COLOR_RGB_VALUE(5, 6, 7),
		.flags = ST_CONSOLE_CELL_BOLD | ST_CONSOLE_CELL_DIRTY,
		.font = 3,
		.hyperlink = 9,
	};
	struct st_console_cell cell;
	uint32_t x;
	uint32_t y;

	CHECK(st_console_set_window(console, &window) == 0);
	CHECK(st_console_set_style(console, &style) == 0);
	CHECK(st_console_write_utf8(console, "abcde", 5) == 0);
	cell = read_cell(console, 2, 1);
	CHECK(cell.glyph == 'a' && cell.font == 3 && cell.hyperlink == 9);
	CHECK((cell.flags & ST_CONSOLE_CELL_BOLD) != 0);
	CHECK((cell.flags & ST_CONSOLE_CELL_DIRTY) == 0);
	CHECK(read_cell(console, 5, 1).glyph == 'd');
	CHECK(read_cell(console, 2, 2).glyph == 'e');
	CHECK(st_console_get_position(console, &x, &y) == 0);
	CHECK(x == 3 && y == 2);
	CHECK(st_console_write_utf8(console, "\xc0\x80", 2)
	    == IO_ERROR_INVALID_ENCODING);
	CHECK(st_console_set_position(console, 2, 2) == 0);
	CHECK(st_console_insert_lines(console, 1, '.') == 0);
	CHECK(read_cell(console, 2, 2).glyph == '.');
	CHECK(st_console_delete_lines(console, 1, '-') == 0);
	CHECK(read_cell(console, 2, 2).glyph == '-');
	CHECK(st_console_scroll(console, -1, '+') == 0);
	CHECK(read_cell(console, 2, 1).glyph == '+');
	st_console_destroy(console);
}

static void
test_move_and_line_operations(void)
{
	st_console_t *console = create_headless(6, 2);
	struct st_console_cell cells[4];
	const struct st_console_rect source = {0, 0, 4, 1};
	const struct st_console_rect shifted = {1, 0, 4, 1};
	const struct st_console_cell fill = {
		.glyph = '.',
		.foreground = 3,
		.background = 4,
	};

	for (uint32_t i = 0; i < 4; ++i)
		cells[i] = (struct st_console_cell){.glyph = 'a' + i};
	CHECK(st_console_write_cells(console, &source, cells, 4) == 0);
	CHECK(st_console_move_cells(console, &source, 1, 0) == 0);
	CHECK(read_cell(console, 0, 0).glyph == 'a');
	CHECK(read_cell(console, 1, 0).glyph == 'a');
	CHECK(read_cell(console, 4, 0).glyph == 'd');
	CHECK(st_console_move_cells_clear(console, &shifted, 0, 0, &fill) == 0);
	CHECK(read_cell(console, 0, 0).glyph == 'a');
	CHECK(read_cell(console, 1, 0).glyph == 'b');
	CHECK(read_cell(console, 3, 0).glyph == 'd');
	CHECK(read_cell(console, 4, 0).glyph == '.');
	CHECK(st_console_set_position(console, 2, 1) == 0);
	CHECK(st_console_clear_line(console, '-') == 0);
	CHECK(read_cell(console, 1, 1).glyph == ' ');
	CHECK(read_cell(console, 2, 1).glyph == '-');
	CHECK(read_cell(console, 5, 1).glyph == '-');
	CHECK(st_console_set_position(console, 0, 1) == 0);
	CHECK(st_console_put_codepoint(console, UINT32_MAX)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(read_cell(console, 0, 1).glyph == ' ');
	st_console_destroy(console);
}

static void
test_invalid_scalar_mutations(void)
{
	const struct st_console_rect screen = {0, 0, 4, 2};
	struct st_console_cell before[8];
	st_console_t *console = create_headless(4, 2);

	CHECK(st_console_write_utf8(console, "abcdefgh", 8) == 0);
	CHECK(st_console_set_position(console, 2, 1) == 0);
	CHECK(st_console_read_cells(console, &screen, before, 4) == 0);
	CHECK(st_console_put_codepoint(console, UINT32_C(0x110000))
	    == IO_ERROR_INVALID_ARGUMENT);
	check_text_state(console, &screen, before, 4, 2, 1);
	CHECK(st_console_clear_window(console, UINT32_C(0xdfff))
	    == IO_ERROR_INVALID_ARGUMENT);
	check_text_state(console, &screen, before, 4, 2, 1);
	CHECK(st_console_clear_line(console, UINT32_C(0x110000))
	    == IO_ERROR_INVALID_ARGUMENT);
	check_text_state(console, &screen, before, 4, 2, 1);
	CHECK(st_console_scroll(console, 1, UINT32_MAX)
	    == IO_ERROR_INVALID_ARGUMENT);
	check_text_state(console, &screen, before, 4, 2, 1);
	CHECK(st_console_insert_lines(console, 1, UINT32_C(0xd800))
	    == IO_ERROR_INVALID_ARGUMENT);
	check_text_state(console, &screen, before, 4, 2, 1);
	CHECK(st_console_delete_lines(console, 1, UINT32_C(0x110000))
	    == IO_ERROR_INVALID_ARGUMENT);
	check_text_state(console, &screen, before, 4, 2, 1);
	st_console_destroy(console);
}

static void
test_snapshot_drains(void)
{
	static const uint8_t bitmap[128] = {0};
	static const struct st_console_font_mapping mapping[] = {
		{' ', ' '}, {'a', 'a'}, {'b', 'b'}, {'c', 'c'},
	};
	static const struct st_console_font font = {
		.bitmap = bitmap,
		.bitmap_size = sizeof(bitmap),
		.width = 8,
		.height = 1,
		.glyph_count = 128,
		.row_stride = 1,
		.glyph_stride = 1,
		.mapping = mapping,
		.mapping_count = sizeof(mapping) / sizeof(mapping[0]),
	};
	struct output output = {0};
	struct st_console_ansi_io io = {
		.write = write_output,
		.context = &output,
		.indexed_colours = 256,
	};
	struct st_console_display_config config_display = {
		.backend = ST_CONSOLE_BACKEND_ANSI,
		.ansi_io = &io,
	};
	struct st_console_config config = {
		.columns = 4,
		.rows = 1,
		.wrap = true,
	};
	st_console_t *console = NULL;
	st_console_snapshot_t *snapshot = NULL;
	size_t writes;

	CHECK(st_console_create(&console, &config_display, &config) == 0);
	CHECK(register_static_font(console, &font) == 0);
	CHECK(st_console_begin_update(console) == 0);
	CHECK(st_console_begin_update(console) == 0);
	CHECK(st_console_write_utf8(console, "abc", 3) == 0);
	CHECK(output.writes == 0);
	CHECK(st_console_end_update(console) == 0);
	CHECK(output.writes == 0);
	output.fail = true;
	CHECK(st_console_snapshot_create(&snapshot, console) == IO_ERROR_IO);
	CHECK(snapshot == NULL);
	output.fail = false;
	CHECK(st_console_snapshot_create(&snapshot, console) == 0);
	CHECK(output.writes != 0);
	CHECK(contains(output.data, output.length, "\033[38;5;248m"));
	CHECK(contains(output.data, output.length, "\033[48;5;0m"));
	CHECK(contains(output.data, output.length, "abc"));
	writes = output.writes;
	CHECK(st_console_end_update(console) == 0);
	CHECK(output.writes == writes);
	CHECK(st_console_clear_window(console, ' ') == 0);
	CHECK(st_console_snapshot_restore(console, snapshot) == 0);
	CHECK(read_cell(console, 0, 0).glyph == 'a');
	CHECK(read_cell(console, 1, 0).glyph == 'b');
	CHECK(read_cell(console, 2, 0).glyph == 'c');
	CHECK(read_cell(console, 3, 0).glyph == ' ');
	CHECK(st_console_set_cursor(console, false, ST_CONSOLE_CURSOR_BAR) == 0);
	st_console_snapshot_destroy(snapshot);
	st_console_destroy(console);
}

static void
test_snapshot_state(void)
{
	st_console_t *console = create_headless(6, 3);
	st_console_t *other_console = create_headless(6, 3);
	st_console_snapshot_t *snapshot = NULL;
	const struct st_console_style saved_style = {
		.foreground = 42,
		.background = ST_CONSOLE_COLOR_RGB_VALUE(1, 2, 3),
		.flags = ST_CONSOLE_CELL_BOLD | ST_CONSOLE_CELL_UNDERLINE,
		.font = 7,
		.hyperlink = 9,
	};
	const struct st_console_rect saved_window = {1, 1, 4, 2};
	const struct st_console_style other_style = {
		.foreground = 1,
		.background = 2,
	};
	const struct st_console_rect other_window = {0, 0, 6, 3};
	const uint32_t saved_colour = UINT32_C(0x123456);
	const uint32_t other_colour = UINT32_C(0xabcdef);
	struct st_console_style style;
	struct st_console_rect window;
	uint32_t colour;
	uint32_t x;
	uint32_t y;
	bool visible;
	enum st_console_cursor_shape shape;

	CHECK(st_console_set_palette(console, 42, &saved_colour, 1) == 0);
	CHECK(st_console_set_style(console, &saved_style) == 0);
	CHECK(st_console_set_window(console, &saved_window) == 0);
	CHECK(st_console_set_position(console, 2, 1) == 0);
	CHECK(st_console_set_cursor(console, false, ST_CONSOLE_CURSOR_BAR) == 0);
	CHECK(st_console_set_wrap(console, true) == 0);
	CHECK(st_console_snapshot_create(&snapshot, console) == 0);
	CHECK(st_console_snapshot_restore(other_console, snapshot)
	    == IO_ERROR_OUT_OF_RANGE);

	CHECK(st_console_set_palette(console, 42, &other_colour, 1) == 0);
	CHECK(st_console_set_style(console, &other_style) == 0);
	CHECK(st_console_set_window(console, &other_window) == 0);
	CHECK(st_console_set_position(console, 5, 2) == 0);
	CHECK(st_console_set_cursor(console, true, ST_CONSOLE_CURSOR_BLOCK) == 0);
	CHECK(st_console_set_wrap(console, false) == 0);
	CHECK(st_console_snapshot_restore(console, snapshot) == 0);

	CHECK(st_console_get_palette(console, 42, &colour, 1) == 0);
	CHECK(colour == saved_colour);
	CHECK(st_console_get_style(console, &style) == 0);
	CHECK(memcmp(&style, &saved_style, sizeof(style)) == 0);
	CHECK(st_console_get_window(console, &window) == 0);
	CHECK(memcmp(&window, &saved_window, sizeof(window)) == 0);
	CHECK(st_console_get_position(console, &x, &y) == 0);
	CHECK(x == 2 && y == 1);
	CHECK(st_console_get_cursor(console, &visible, &shape) == 0);
	CHECK(!visible && shape == ST_CONSOLE_CURSOR_BAR);
	CHECK(st_console_set_position(console, 4, 1) == 0);
	CHECK(st_console_put_codepoint(console, 'x') == 0);
	CHECK(st_console_get_position(console, &x, &y) == 0);
	CHECK(x == 1 && y == 2);

	st_console_snapshot_destroy(snapshot);
	st_console_destroy(other_console);
	st_console_destroy(console);
}

static void
test_event_order(void)
{
	st_console_t *console = create_headless(1, 1);
	struct st_console_event first = {
		.type = ST_CONSOLE_EVENT_KEY_CODE,
		.data.key = {.code = 0x1234},
	};
	struct st_console_event second = {
		.type = ST_CONSOLE_EVENT_TEXT,
		.data.text = {.codepoint = 0x20ac},
	};
	struct st_console_event physical = {
		.type = ST_CONSOLE_EVENT_PHYSICAL_KEY,
		.data.physical_key = {.evdev = 30, .pressed = true},
	};
	struct st_console_event doorway = {
		.type = ST_CONSOLE_EVENT_DOORWAY_KEY,
		.data.doorway_key = {.scan_code = 0x1c, .enhanced = true},
	};
	struct st_console_event focus_lost = {
		.type = ST_CONSOLE_EVENT_FOCUS,
		.data.focus = {.focused = false},
	};
	struct st_console_event result;
	struct st_console_event invalid = {
		.type = ST_CONSOLE_EVENT_TEXT,
		.data.text = {.codepoint = UINT32_C(0xd800)},
	};
	bool pressed;

	CHECK(st_console_post_event(console, &invalid)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_console_event_try(console, &result) == IO_ERROR_WOULD_BLOCK);
	CHECK(st_console_post_event(console, &first) == 0);
	CHECK(st_console_post_event(console, &second) == 0);
	CHECK(st_console_event_wait(console, &result) == 0);
	CHECK(result.type == ST_CONSOLE_EVENT_KEY_CODE);
	CHECK(result.data.key.code == 0x1234);
	CHECK(st_console_event_try(console, &result) == 0);
	CHECK(result.type == ST_CONSOLE_EVENT_TEXT);
	CHECK(result.data.text.codepoint == 0x20ac);
	CHECK(st_console_event_try(console, &result) == IO_ERROR_WOULD_BLOCK);
	CHECK(st_console_post_event(console, &doorway) == 0);
	CHECK(st_console_event_try(console, &result) == 0);
	CHECK(result.type == ST_CONSOLE_EVENT_DOORWAY_KEY);
	CHECK(result.data.doorway_key.scan_code == 0x1c);
	CHECK(result.data.doorway_key.enhanced);
	CHECK(st_console_post_event(console, &physical) == 0);
	CHECK(st_console_physical_key_state(console, 30, &pressed) == 0);
	CHECK(pressed);
	CHECK(st_console_post_event(console, &focus_lost) == 0);
	CHECK(st_console_physical_key_state(console, 30, &pressed) == 0);
	CHECK(!pressed);
	CHECK(st_console_event_try(console, &result) == 0);
	CHECK(result.type == ST_CONSOLE_EVENT_PHYSICAL_KEY);
	CHECK(result.data.physical_key.evdev == 30);
	CHECK(result.data.physical_key.pressed);
	CHECK(st_console_event_try(console, &result) == 0);
	CHECK(result.type == ST_CONSOLE_EVENT_PHYSICAL_KEY);
	CHECK(result.data.physical_key.evdev == 30);
	CHECK(!result.data.physical_key.pressed);
	CHECK(st_console_event_try(console, &result) == 0);
	CHECK(result.type == ST_CONSOLE_EVENT_FOCUS);
	CHECK(!result.data.focus.focused);
	CHECK(st_console_synthesize_physical_key(console, 42, true) == 0);
	CHECK(st_console_synthesize_physical_key(console, 42, true) == 0);
	CHECK(st_console_physical_key_state(console, 42, &pressed) == 0);
	CHECK(pressed);
	CHECK(st_console_event_try(console, &result) == 0);
	CHECK(result.type == ST_CONSOLE_EVENT_PHYSICAL_KEY);
	CHECK(result.data.physical_key.evdev == 42);
	CHECK(result.data.physical_key.pressed);
	CHECK(!result.data.physical_key.repeat);
	CHECK(result.data.physical_key.modifiers == 0);
	CHECK(st_console_event_try(console, &result) == IO_ERROR_WOULD_BLOCK);
	CHECK(st_console_synthesize_physical_key(console, 42, false) == 0);
	CHECK(st_console_synthesize_physical_key(console, 42, false) == 0);
	CHECK(st_console_physical_key_state(console, 42, &pressed) == 0);
	CHECK(!pressed);
	CHECK(st_console_event_try(console, &result) == 0);
	CHECK(result.type == ST_CONSOLE_EVENT_PHYSICAL_KEY);
	CHECK(result.data.physical_key.evdev == 42);
	CHECK(!result.data.physical_key.pressed);
	CHECK(st_console_event_try(console, &result) == IO_ERROR_WOULD_BLOCK);
	st_console_destroy(console);
}

static void
test_resize_palette_and_errors(void)
{
	st_console_t *console = create_headless(2, 2);
	struct st_console_display_config invalid_config_display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	struct st_console_config invalid_config = {
		.columns = 2,
		.rows = 2,
		.cell_width = 8,
	};
	st_console_t *unchanged = console;
	uint32_t set[2] = {0x123456, 0xfedcba};
	uint32_t get[2] = {0};
	uint32_t *full_set;
	uint32_t *full_get;
	static const uint8_t glyphs[16] = {0};
	const struct st_console_font font = {
		.bitmap = glyphs,
		.bitmap_size = sizeof(glyphs),
		.width = 8,
		.height = 8,
		.glyph_count = 2,
		.row_stride = 1,
		.glyph_stride = 8,
	};
	struct st_console_font invalid_font = font;
	bool registered;
	uint16_t identifier;
	char *clipboard = NULL;
	uint32_t pixel_width = 17;
	uint32_t pixel_height = 19;

	CHECK(st_console_create(&unchanged, &invalid_config_display, &invalid_config)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(unchanged == console);
	CHECK(st_console_pixel_size(console, &pixel_width, &pixel_height)
	    == IO_ERROR_NOT_SUPPORTED);
	CHECK(pixel_width == 17 && pixel_height == 19);
	{
		static const struct {
			uint16_t index;
			uint32_t rgb;
		} defaults[] = {
			{1, UINT32_C(0xa80000)},
			{16, UINT32_C(0x000000)},
			{17, UINT32_C(0x00005f)},
			{255, UINT32_C(0xeeeeee)},
			{257, UINT32_C(0xffffff)},
			{272, UINT32_C(0x005181)},
			{274, UINT32_C(0xb6b6b6)},
			{UINT16_MAX, 0},
		};

		for (size_t i = 0; i < sizeof(defaults) / sizeof(defaults[0]);
		    ++i) {
			uint32_t colour;

			CHECK(st_console_get_palette(console, defaults[i].index,
			    &colour, 1) == 0);
			CHECK(colour == defaults[i].rgb);
		}
	}

	CHECK(st_console_put_codepoint(console, 'x') == 0);
	CHECK(st_console_resize(console, 3, 3) == 0);
	CHECK(read_cell(console, 0, 0).glyph == 'x');
	CHECK(read_cell(console, 2, 2).glyph == ' ');
	CHECK(st_console_set_palette(console, 254, set, 2) == 0);
	CHECK(st_console_get_palette(console, 254, get, 2) == 0);
	CHECK(memcmp(set, get, sizeof(set)) == 0);
	CHECK(st_console_set_palette(console, 255, set, 2) == 0);
	CHECK(st_console_get_palette(console, 255, get, 2) == 0);
	CHECK(memcmp(set, get, sizeof(set)) == 0);
	CHECK(st_console_set_palette(console, UINT16_MAX, set, 1) == 0);
	CHECK(st_console_set_palette(console, UINT16_MAX, set, 2)
	    == IO_ERROR_INVALID_ARGUMENT);
	full_set = malloc((UINT16_MAX + 1U) * sizeof(*full_set));
	full_get = malloc((UINT16_MAX + 1U) * sizeof(*full_get));
	CHECK(full_set != NULL && full_get != NULL);
	for (uint32_t i = 0; i <= UINT16_MAX; ++i)
		full_set[i] = (i * UINT32_C(0x010101)) & UINT32_C(0xffffff);
	CHECK(st_console_set_palette(console, 0, full_set,
	    UINT16_MAX + UINT32_C(1)) == 0);
	CHECK(st_console_get_palette(console, 0, full_get,
	    UINT16_MAX + UINT32_C(1)) == 0);
	CHECK(memcmp(full_set, full_get,
	    (UINT16_MAX + 1U) * sizeof(*full_set)) == 0);
	free(full_get);
	free(full_set);
	identifier = register_static_font(console, &font);
	CHECK(st_console_font_registered(console, identifier, &registered) == 0);
	CHECK(registered);
	invalid_font.bitmap_size--;
	CHECK(st_console_register_static_font(console, &invalid_font, &identifier)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_console_set_title(console, "title") == IO_ERROR_NOT_SUPPORTED);
	CHECK(st_console_set_clipboard_text(console, "text")
	    == IO_ERROR_NOT_SUPPORTED);
	CHECK(st_console_get_clipboard_text(console, &clipboard)
	    == IO_ERROR_NOT_SUPPORTED);
	CHECK(clipboard == NULL);
	CHECK(st_console_open_url(console, "https://example.invalid/")
	    == IO_ERROR_NOT_SUPPORTED);
	CHECK(st_console_set_pointer_visible(console, false)
	    == IO_ERROR_NOT_SUPPORTED);
	CHECK(st_console_set_pointer_shape(console, ST_CONSOLE_POINTER_LINK)
	    == IO_ERROR_NOT_SUPPORTED);
	CHECK(st_console_set_pointer_shape(console,
	    (enum st_console_pointer_shape)-1) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_console_set_fullscreen(console, true)
	    == IO_ERROR_NOT_SUPPORTED);
	{
		double scaling = 17.0;

		CHECK(st_console_display_get_scaling(console->display, &scaling)
		    == IO_ERROR_NOT_SUPPORTED);
		CHECK(scaling == 17.0);
	}
	CHECK(st_console_end_update(console) == IO_ERROR_INVALID_ARGUMENT);
	st_console_destroy(console);
}

static void
test_pixels_and_font_rendering(void)
{
	static const uint8_t glyphs[] = {
		0x00, 0x00,
		0x80, 0x01,
	};
	static const struct st_console_font_mapping mapping[] = {
		{0, 0},
		{1, 1},
	};
	const struct st_console_font font = {
		.bitmap = glyphs,
		.bitmap_size = sizeof(glyphs),
		.width = 8,
		.height = 2,
		.glyph_count = 2,
		.row_stride = 1,
		.glyph_stride = 2,
		.mapping = mapping,
		.mapping_count = sizeof(mapping) / sizeof(mapping[0]),
	};
	struct st_console_display_config config_display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	struct st_console_config config = {
		.columns = 2,
		.rows = 2,
		.cell_width = 8,
		.cell_height = 2,
		.wrap = true,
	};
	struct st_console_cell cell = {
		.glyph = 1,
		.foreground = ST_CONSOLE_COLOR_RGB_VALUE(255, 0, 0),
		.background = 0,
		.flags = ST_CONSOLE_CELL_BLINK,
		.font = 3,
	};
	const struct st_console_rect cell_rect = {0, 0, 1, 1};
	const struct st_console_rect pixel_rect = {0, 0, 8, 2};
	st_console_t *console = NULL;
	st_console_snapshot_t *snapshot = NULL;
	struct st_pixel_buffer *pixels = NULL;
	uint32_t width;
	uint32_t height;
	uint16_t font_identifier;
	const uint32_t red = ST_PIXEL_COLOR_RGB_VALUE(255, 0, 0);
	const uint32_t black = 0;
	const uint32_t overlay = ST_PIXEL_COLOR_RGB_VALUE(0xab, 0xcd, 0xef);
	const uint32_t changed_palette = UINT32_C(0x010203);
	const struct st_console_rect second_cell = {1, 0, 1, 1};
	const struct st_console_rect second_pixels = {8, 0, 8, 2};

	CHECK(st_console_create(&console, &config_display, &config) == 0);
	CHECK(st_console_pixel_size(console, &width, &height) == 0);
	CHECK(width == 16 && height == 4);
	font_identifier = register_static_font(console, &font);
	cell.font = font_identifier;
	CHECK(st_console_write_cells(console, &cell_rect, &cell, 1) == 0);
	CHECK(st_console_get_pixels(&pixels, console, &pixel_rect, false) == 0);
	CHECK(pixels->primary[0] == red && pixels->alternate[0] == black);
	CHECK(pixels->primary[7] == black);
	CHECK(pixels->primary[15] == red && pixels->alternate[15] == black);
	st_pixel_buffer_destroy(pixels);
	pixels = NULL;
	cell.foreground = 42;
	cell.flags = 0;
	CHECK(st_console_write_cells(console, &second_cell, &cell, 1) == 0);
	CHECK(st_console_set_palette(console, 42, &changed_palette, 1) == 0);
	CHECK(st_console_get_pixels(&pixels, console, &second_pixels, false) == 0);
	CHECK(pixels->primary[0] == 42);
	st_pixel_buffer_destroy(pixels);
	pixels = NULL;

	CHECK(st_console_set_pixel(console, 2, 0, overlay) == 0);
	CHECK((read_cell(console, 0, 0).flags
	    & ST_CONSOLE_CELL_PIXEL_GRAPHICS) != 0);
	CHECK(st_console_snapshot_create(&snapshot, console) == 0);
	CHECK(st_console_get_pixels(&pixels, console, &pixel_rect, false) == 0);
	CHECK(pixels->primary[2] == overlay && pixels->alternate[2] == overlay);
	st_pixel_buffer_destroy(pixels);
	pixels = NULL;
	/* Rewriting an identical cell must replace a newer pixel overlay. */
	CHECK(st_console_write_cells(console, &cell_rect, &cell, 1) == 0);
	CHECK(st_console_get_pixels(&pixels, console, &pixel_rect, false) == 0);
	CHECK(pixels->primary[2] == black);
	st_pixel_buffer_destroy(pixels);
	pixels = NULL;
	CHECK(st_console_snapshot_restore(console, snapshot) == 0);

	cell.foreground = ST_CONSOLE_COLOR_RGB_VALUE(0, 255, 0);
	cell.flags = 0;
	CHECK(st_console_write_cells(console, &cell_rect, &cell, 1) == 0);
	CHECK(st_console_get_pixels(&pixels, console, &pixel_rect, false) == 0);
	CHECK(pixels->primary[2] == black);
	st_pixel_buffer_destroy(pixels);
	pixels = NULL;
	CHECK(st_console_snapshot_restore(console, snapshot) == 0);
	CHECK(st_console_get_pixels(&pixels, console, &pixel_rect, false) == 0);
	CHECK(pixels->primary[2] == overlay);
	st_pixel_buffer_destroy(pixels);
	pixels = NULL;
	CHECK(st_console_get_pixels(&pixels, console, &pixel_rect, true) == 0);
	CHECK(pixels->primary[2] == black);

	st_pixel_buffer_destroy(pixels);
	st_console_snapshot_destroy(snapshot);
	CHECK(st_console_resize(console, 3, 1) == 0);
	CHECK(st_console_pixel_size(console, &width, &height) == 0);
	CHECK(width == 24 && height == 2);
	st_console_destroy(console);
}

static void
test_fast_font_attributes(void)
{
	static const uint8_t glyphs[] = {
		0x00, 0x11, 0x00, 0x22,
		0x80, 0x33, 0x01, 0x44,
	};
	static const struct st_console_font_mapping mapping[] = {
		{0, 0},
		{1, 1},
	};
	const struct st_console_font font = {
		.bitmap = glyphs,
		.bitmap_size = sizeof(glyphs),
		.width = 8,
		.height = 2,
		.glyph_count = 2,
		.row_stride = 2,
		.glyph_stride = 4,
		.mapping = mapping,
		.mapping_count = sizeof(mapping) / sizeof(mapping[0]),
	};
	struct st_console_display_config config_display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	struct st_console_config config = {
		.columns = 5,
		.rows = 1,
		.cell_width = 8,
		.cell_height = 2,
	};
	const uint32_t red = ST_PIXEL_COLOR_RGB_VALUE(255, 0, 0);
	const uint32_t green = ST_PIXEL_COLOR_RGB_VALUE(0, 255, 0);
	struct st_console_cell cells[5];
	const struct st_console_rect cells_rect = {0, 0, 5, 1};
	const struct st_console_rect pixels_rect = {0, 0, 40, 2};
	st_console_t *console = NULL;
	struct st_pixel_buffer *pixels = NULL;
	uint16_t font_identifier;

	for (size_t i = 0; i < 5; ++i) {
		cells[i] = (struct st_console_cell){
			.glyph = 1,
			.foreground = ST_CONSOLE_COLOR_RGB_VALUE(255, 0, 0),
			.background = ST_CONSOLE_COLOR_RGB_VALUE(0, 255, 0),
			.font = 1,
		};
	}
	cells[0].flags = ST_CONSOLE_CELL_BOLD;
	cells[1].flags = ST_CONSOLE_CELL_UNDERLINE;
	cells[2].hyperlink = 1;
	cells[3].flags = ST_CONSOLE_CELL_REVERSE;
	cells[4].flags = ST_CONSOLE_CELL_CONCEAL;
	CHECK(st_console_create(&console, &config_display, &config) == 0);
	font_identifier = register_static_font(console, &font);
	for (size_t i = 0; i < 5; ++i)
		cells[i].font = font_identifier;
	CHECK(st_console_write_cells(console, &cells_rect, cells, 5) == 0);
	CHECK(st_console_get_pixels(&pixels, console, &pixels_rect, false) == 0);
	CHECK(pixels->primary[0] == red && pixels->primary[1] == red
	    && pixels->primary[2] == green);
	CHECK(pixels->primary[40 + 7] == red);
	CHECK(pixels->primary[40 + 8] == red
	    && pixels->primary[40 + 15] == red);
	CHECK(pixels->primary[40 + 16] == red
	    && pixels->primary[40 + 17] == green
	    && pixels->primary[40 + 20] == red
	    && pixels->primary[40 + 23] == red);
	CHECK(pixels->primary[24] == green && pixels->primary[25] == red);
	for (size_t y = 0; y < 2; ++y)
		for (size_t x = 32; x < 40; ++x)
			CHECK(pixels->primary[y * 40 + x] == green);
	st_pixel_buffer_destroy(pixels);
	st_console_destroy(console);
}

static void
test_raster_margin_and_scroll(void)
{
	static const uint8_t glyphs[] = {0x00, 0x00, 0xc0, 0xc0};
	static const struct st_console_font_mapping mapping[] = {
		{1, 1},
		{' ', 0},
	};
	const struct st_console_font font = {
		.bitmap = glyphs,
		.bitmap_size = sizeof(glyphs),
		.width = 2,
		.height = 2,
		.glyph_count = 2,
		.row_stride = 1,
		.glyph_stride = 2,
		.mapping = mapping,
		.mapping_count = sizeof(mapping) / sizeof(mapping[0]),
	};
	struct st_console_display_config config_display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	struct st_console_config config = {
		.columns = 2,
		.rows = 3,
		.cell_width = 2,
		.cell_height = 2,
		.pixel_width = 5,
		.pixel_height = 8,
	};
	struct st_console_cell cells[6];
	const struct st_console_rect grid = {0, 0, 2, 3};
	const struct st_console_rect raster = {0, 0, 5, 8};
	const uint32_t overlay = ST_PIXEL_COLOR_RGB_VALUE(0x12, 0x34, 0x56);
	const uint32_t partial_overlay = ST_PIXEL_COLOR_RGB_VALUE(0x45, 0x67,
	    0x89);
	const uint32_t right_margin = ST_PIXEL_COLOR_RGB_VALUE(0x23, 0x45,
	    0x67);
	const uint32_t bottom_margin = ST_PIXEL_COLOR_RGB_VALUE(0x34, 0x56,
	    0x78);
	const uint32_t blit_colour = ST_PIXEL_COLOR_RGB_VALUE(0x56, 0x78,
	    0x9a);
	const struct st_console_rect partial_window = {0, 0, 1, 2};
	st_console_t *console = NULL;
	st_console_snapshot_t *snapshot = NULL;
	struct st_pixel_buffer *pixels = NULL;
	struct st_pixel_buffer *blit_pixels = NULL;
	const struct st_pixel_blit blit = {
		.width = 1,
		.height = 1,
		.destination_x = 1,
		.scale_x = 1,
		.scale_y = 1,
	};
	uint32_t width;
	uint32_t height;
	uint16_t font_identifier;

	for (uint32_t y = 0; y < 3; ++y) {
		for (uint32_t x = 0; x < 2; ++x) {
			cells[(size_t)y * 2 + x] = (struct st_console_cell){
				.glyph = 1,
				.foreground = ST_CONSOLE_COLOR_RGB_VALUE(
				    16U * (y + 1), 0, 0),
				.background = 0,
				.font = 1,
			};
		}
	}
	CHECK(st_console_create(&console, &config_display, &config) == 0);
	CHECK(st_console_pixel_size(console, &width, &height) == 0);
	CHECK(width == 5 && height == 8);
	font_identifier = register_static_font(console, &font);
	for (size_t i = 0; i < 6; ++i)
		cells[i].font = font_identifier;
	CHECK(st_console_write_cells(console, &grid, cells, 2) == 0);
	CHECK(st_console_set_pixel(console, 0, 2, overlay) == 0);
	CHECK(st_console_set_pixel(console, 4, 2, right_margin) == 0);
	CHECK(st_console_set_pixel(console, 0, 6, bottom_margin) == 0);
	CHECK(st_console_scroll(console, 1, ' ') == 0);
	CHECK(st_console_get_pixels(&pixels, console, &raster, false) == 0);
	CHECK(pixels->primary[0] == overlay);
	CHECK(pixels->primary[(size_t)2 * 5 + 4] == right_margin);
	CHECK(pixels->primary[(size_t)6 * 5] == bottom_margin);
	st_pixel_buffer_destroy(pixels);
	pixels = NULL;

	/* A partial-window move remains overlap-safe after the full-screen ring. */
	CHECK(st_console_set_window(console, &partial_window) == 0);
	CHECK(st_console_set_pixel(console, 0, 2, partial_overlay) == 0);
	CHECK(st_console_scroll(console, 1, ' ') == 0);
	CHECK(st_console_get_pixels(&pixels, console, &raster, false) == 0);
	CHECK(pixels->primary[0] == partial_overlay);
	CHECK(pixels->primary[(size_t)2 * 5 + 4] == right_margin);
	CHECK(pixels->primary[(size_t)6 * 5] == bottom_margin);
	st_pixel_buffer_destroy(pixels);
	pixels = NULL;

	CHECK(st_console_set_window(console, &grid) == 0);
	CHECK(st_console_scroll(console, -1, ' ') == 0);
	CHECK(st_console_get_pixels(&pixels, console, &raster, false) == 0);
	CHECK(pixels->primary[(size_t)2 * 5] == partial_overlay);
	CHECK(pixels->primary[(size_t)2 * 5 + 4] == right_margin);
	CHECK(pixels->primary[(size_t)6 * 5] == bottom_margin);
	st_pixel_buffer_destroy(pixels);
	pixels = NULL;

	/* Blitting normalizes circular text rows without moving raster margins. */
	CHECK(st_console_scroll(console, 1, ' ') == 0);
	CHECK(st_pixel_buffer_create(&blit_pixels, 1, 1, false) == 0);
	blit_pixels->primary[0] = blit_colour;
	CHECK(st_console_blit_pixels(console, blit_pixels, NULL, &blit) == 0);
	st_pixel_buffer_destroy(blit_pixels);
	CHECK(st_console_snapshot_create(&snapshot, console) == 0);
	CHECK(st_console_set_pixel(console, 1, 0, 0) == 0);
	CHECK(st_console_snapshot_restore(console, snapshot) == 0);
	st_console_snapshot_destroy(snapshot);
	CHECK(st_console_get_pixels(&pixels, console, &raster, false) == 0);
	CHECK(pixels->primary[1] == blit_colour);
	CHECK(pixels->primary[(size_t)2 * 5 + 4] == right_margin);
	CHECK(pixels->primary[(size_t)6 * 5] == bottom_margin);
	st_pixel_buffer_destroy(pixels);
	pixels = NULL;
	CHECK(st_console_resize(console, 3, 2) == 0);
	CHECK(st_console_pixel_size(console, &width, &height) == 0);
	CHECK(width == 7 && height == 6);
	st_console_destroy(console);

	console = NULL;
	config.pixel_width = 0;
	CHECK(st_console_create(&console, &config_display, &config) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(console == NULL);
}

static void
test_partial_full_width_raster_scroll(void)
{
	const uint8_t bitmap[] = {UINT8_C(0x80)};
	const struct st_console_font font = {
		.bitmap = bitmap,
		.bitmap_size = sizeof(bitmap),
		.width = 1,
		.height = 1,
		.glyph_count = 1,
		.row_stride = 1,
		.glyph_stride = 1,
	};
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config config = {
		.columns = 1,
		.rows = 5,
		.cell_width = 1,
		.cell_height = 1,
	};
	const struct st_console_rect screen = {0, 0, 1, 5};
	const struct st_console_rect pixels_rect = {0, 0, 1, 5};
	const struct st_console_rect scroll_up = {0, 1, 1, 3};
	const struct st_console_rect scroll_down = {0, 0, 1, 3};
	struct st_console_cell cells[5];
	struct st_console_cell fill;
	struct st_pixel_buffer *pixels = NULL;
	struct st_pixel_buffer *forced = NULL;
	st_console_t *console = NULL;
	uint16_t font_identifier;

	CHECK(st_console_create(&console, &display, &config) == 0);
	font_identifier = register_static_font(console, &font);
	for (uint32_t y = 0; y < 5; ++y) {
		cells[y] = (struct st_console_cell){
			.foreground = y + 1,
			.background = 0,
			.font = font_identifier,
			.glyph_font = font_identifier,
		};
	}
	fill = cells[0];
	fill.foreground = 9;
	CHECK(st_console_write_complete_cells(console, &screen, cells, 1) == 0);
	CHECK(st_console_move_cells_clear(console, &scroll_up, 0, 0,
	    &fill) == 0);
	/* Rotate the text raster, correcting the reserved fifth row in place. */
	CHECK(console->pixel_top == 1);
	CHECK(st_console_get_pixels(&pixels, console, &pixels_rect, false) == 0);
	CHECK(pixels->primary[0] == 2);
	CHECK(pixels->primary[1] == 3);
	CHECK(pixels->primary[2] == 4);
	CHECK(pixels->primary[3] == 9);
	CHECK(pixels->primary[4] == 5);
	CHECK(st_console_get_pixels(&forced, console, &pixels_rect, true) == 0);
	CHECK(memcmp(pixels->primary, forced->primary,
	    (size_t)pixels->width * pixels->height
	    * sizeof(pixels->primary[0])) == 0);
	st_pixel_buffer_destroy(forced);
	forced = NULL;
	st_pixel_buffer_destroy(pixels);
	pixels = NULL;

	CHECK(st_console_write_complete_cells(console, &screen, cells, 1) == 0);
	fill.foreground = 8;
	CHECK(st_console_move_cells_clear(console, &scroll_down, 0, 1,
	    &fill) == 0);
	CHECK(console->pixel_top == 0);
	CHECK(st_console_get_pixels(&pixels, console, &pixels_rect, false) == 0);
	CHECK(pixels->primary[0] == 8);
	CHECK(pixels->primary[1] == 1);
	CHECK(pixels->primary[2] == 2);
	CHECK(pixels->primary[3] == 3);
	CHECK(pixels->primary[4] == 5);
	CHECK(st_console_get_pixels(&forced, console, &pixels_rect, true) == 0);
	CHECK(memcmp(pixels->primary, forced->primary,
	    (size_t)pixels->width * pixels->height
	    * sizeof(pixels->primary[0])) == 0);
	st_pixel_buffer_destroy(forced);
	st_pixel_buffer_destroy(pixels);
	st_console_destroy(console);
}

static void
test_saa5050_double_height_raster(void)
{
	static const uint8_t glyphs[] = {0x00, 0x00, 0x80, 0x01};
	static const struct st_console_font_mapping mapping[] = {
		{0, 0},
		{1, 1},
	};
	const struct st_console_font font = {
		.bitmap = glyphs,
		.bitmap_size = sizeof(glyphs),
		.width = 8,
		.height = 2,
		.glyph_count = 2,
		.row_stride = 1,
		.glyph_stride = 2,
		.mapping = mapping,
		.mapping_count = sizeof(mapping) / sizeof(mapping[0]),
	};
	struct st_console_display_config config_display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	struct st_console_config config = {
		.columns = 1,
		.rows = 2,
		.cell_width = 8,
		.cell_height = 2,
	};
	struct st_console_cell cells[2] = {
		{
			.glyph = 1,
			.foreground = 7,
			.background = 0,
			.flags = ST_CONSOLE_CELL_SAA5050
			    | ST_CONSOLE_CELL_DOUBLE_HEIGHT,
			.font = 1,
		},
		{
			.glyph = 0,
			.foreground = 7,
			.background = 0,
			.flags = ST_CONSOLE_CELL_SAA5050
			    | ST_CONSOLE_CELL_TERMINAL_FONT,
			.font = 1,
		},
	};
	const struct st_console_rect cells_rect = {0, 0, 1, 2};
	const struct st_console_rect pixels_rect = {0, 0, 8, 4};
	st_console_t *console = NULL;
	struct st_pixel_buffer *pixels = NULL;
	struct st_pixel_buffer *forced = NULL;
	const uint32_t foreground = 7;
	uint16_t font_identifier;

	CHECK(st_console_create(&console, &config_display, &config) == 0);
	font_identifier = register_static_font(console, &font);
	cells[0].font = font_identifier;
	cells[1].font = font_identifier;
	CHECK(st_console_write_cells(console, &cells_rect, cells, 1) == 0);
	CHECK(st_console_get_pixels(&pixels, console, &pixels_rect, false) == 0);
	CHECK(pixels->primary[0] == foreground);
	CHECK(pixels->primary[8] == foreground);
	CHECK(pixels->primary[16 + 7] == foreground);
	CHECK(pixels->primary[24 + 7] == foreground);
	st_pixel_buffer_destroy(pixels);
	st_console_cell_at(console, 0)->flags &= ~ST_CONSOLE_CELL_DIRTY;
	st_console_cell_at(console, 1)->flags &= ~ST_CONSOLE_CELL_DIRTY;
	st_console_cell_at(console, 0)->flags &= ~ST_CONSOLE_CELL_DOUBLE_HEIGHT;
	st_console_drawn_at(console, 0)->flags |= ST_CONSOLE_CELL_DIRTY;
	CHECK(st_console_rasterize_locked(console, false) == 0);
	CHECK((st_console_cell_at(console, 0)->flags & ST_CONSOLE_CELL_DIRTY) != 0);
	CHECK((st_console_cell_at(console, 1)->flags & ST_CONSOLE_CELL_DIRTY) != 0);
	CHECK(st_console_scroll(console, 1, ' ') == 0);
	CHECK(st_console_get_pixels(&pixels, console, &pixels_rect, false) == 0);
	CHECK(st_console_get_pixels(&forced, console, &pixels_rect, true) == 0);
	CHECK(memcmp(pixels->primary, forced->primary,
	    (size_t)pixels->width * pixels->height
	    * sizeof(pixels->primary[0])) == 0);
	CHECK(memcmp(pixels->alternate, forced->alternate,
	    (size_t)pixels->width * pixels->height
	    * sizeof(pixels->alternate[0])) == 0);
	st_pixel_buffer_destroy(forced);
	st_pixel_buffer_destroy(pixels);
	st_console_destroy(console);
}

static void
test_ncurses_when_available(void)
{
	struct st_console_display_config config_display = {
		.backend = ST_CONSOLE_BACKEND_NCURSES,
	};
	struct st_console_config config = {
		.columns = 8,
		.rows = 3,
		.wrap = true,
	};
	st_console_t *console = NULL;
	st_console_t *second = NULL;
	st_console_snapshot_t *snapshot = NULL;
	int result = st_console_create(&console, &config_display, &config);

	if (result == IO_ERROR_NOT_FOUND || result == IO_ERROR_NOT_SUPPORTED
	    || result == IO_ERROR_OUT_OF_RANGE)
		return;
	CHECK(result == 0);
	CHECK(st_console_create(&second, &config_display, &config) == IO_ERROR_BUSY);
	CHECK(second == NULL);
	CHECK(st_console_begin_update(console) == 0);
	CHECK(st_console_write_utf8(console, "curses", 6) == 0);
	CHECK(st_console_set_position(console, 1, 1) == 0);
	CHECK(st_console_end_update(console) == 0);
	CHECK(st_console_snapshot_create(&snapshot, console) == 0);
	CHECK(st_console_resize(console, 7, 2) == 0);
	st_console_snapshot_destroy(snapshot);
	st_console_destroy(console);
}

struct writer_context {
	st_console_t *console;
	uint32_t row;
	uint32_t codepoint;
	bool succeeded;
};

static int
write_row(void *argument)
{
	struct writer_context *context = argument;
	struct st_console_rect row = {0, context->row, 64, 1};
	struct st_console_cell cell = {
		.glyph = context->codepoint,
		.foreground = context->row,
		.background = 0,
	};

	context->succeeded = true;
	for (unsigned int i = 0; i < 200; ++i) {
		if (st_console_fill(context->console, &row, &cell) != 0) {
			context->succeeded = false;
			break;
		}
	}
	return 0;
}

static void
test_concurrent_writers(void)
{
	st_console_t *console = create_headless(64, 2);
	struct writer_context contexts[2] = {
		{console, 0, 'x', false},
		{console, 1, 'y', false},
	};
	st_thread_t threads[2];

	CHECK(st_thread_create(&threads[0], write_row, &contexts[0])
	    == ST_THREAD_SUCCESS);
	CHECK(st_thread_create(&threads[1], write_row, &contexts[1])
	    == ST_THREAD_SUCCESS);
	CHECK(st_thread_join(threads[0], NULL) == ST_THREAD_SUCCESS);
	CHECK(st_thread_join(threads[1], NULL) == ST_THREAD_SUCCESS);
	CHECK(contexts[0].succeeded && contexts[1].succeeded);
	for (uint32_t x = 0; x < 64; ++x) {
		CHECK(read_cell(console, x, 0).glyph == 'x');
		CHECK(read_cell(console, x, 1).glyph == 'y');
	}
	st_console_destroy(console);
}

struct deferred_present_state {
	st_mutex_t mutex;
	st_condition_t changed;
	unsigned int calls;
};

static int
record_deferred_present(st_console_t *console)
{
	struct deferred_present_state *state = console->display->backend_state;

	(void)st_mutex_lock(&state->mutex);
	++state->calls;
	(void)st_condition_signal(&state->changed);
	(void)st_mutex_unlock(&state->mutex);
	return 0;
}

static const struct st_console_backend_ops deferred_present_ops = {
	.presents_pixels = true,
	.deferred_present = true,
	.present = record_deferred_present,
};

static void
test_deferred_presentation(void)
{
	st_console_t *console = create_headless(2, 1);
	struct deferred_present_state state;
	const struct st_console_rect destination = {0, 0, 1, 1};
	struct st_console_cell cell = {
		.foreground = 7,
		.background = 0,
	};
	struct timespec deadline;
	unsigned int calls;

	CHECK(st_console_present(console) == 0);
	CHECK(st_mutex_init(&state.mutex, ST_MUTEX_PLAIN) == ST_THREAD_SUCCESS);
	CHECK(st_condition_init(&state.changed) == ST_THREAD_SUCCESS);
	state.calls = 0;
	console->display->backend_ops = &deferred_present_ops;
	console->display->backend_state = &state;
	console->display->backend_ops = &deferred_present_ops;
	console->display->backend_state = &state;
	CHECK(st_console_start_presentation_worker(console) == 0);
	for (unsigned int i = 0; i < 200; ++i) {
		cell.glyph = (i & 1U) == 0 ? 'a' : 'b';
		CHECK(st_console_write_cells(console, &destination, &cell, 1) == 0);
	}
	CHECK(timespec_get(&deadline, TIME_UTC) == TIME_UTC);
	++deadline.tv_sec;
	(void)st_mutex_lock(&state.mutex);
	while (state.calls == 0) {
		int status = st_condition_timedwait(&state.changed, &state.mutex,
		    &deadline);

		CHECK(status == ST_THREAD_SUCCESS);
	}
	calls = state.calls;
	(void)st_mutex_unlock(&state.mutex);
	CHECK(calls < 10);
	CHECK(st_console_present(console) == 0);
	(void)st_mutex_lock(&state.mutex);
	CHECK(state.calls == calls + 1);
	(void)st_mutex_unlock(&state.mutex);
	st_console_destroy(console);
	st_condition_destroy(&state.changed);
	st_mutex_destroy(&state.mutex);
}

static void
test_unicode_wide_cells(void)
{
	static const uint8_t narrow_bitmap[4] = {0, 0, 0xff, 0xff};
	static const uint8_t wide_bitmap[4] = {0xff, 0xff, 0x80, 0x01};
	static const struct st_console_font_mapping mapping[] = {
		{'?', 0},
		{'A', 1},
		{UINT32_C(0x4e00), 2},
	};
	static const struct st_console_font font = {
		.bitmap = narrow_bitmap,
		.bitmap_size = sizeof(narrow_bitmap),
		.width = 8,
		.height = 2,
		.glyph_count = 2,
		.row_stride = 1,
		.glyph_stride = 2,
		.wide_bitmap = wide_bitmap,
		.wide_bitmap_size = sizeof(wide_bitmap),
		.wide_glyph_count = 1,
		.wide_row_stride = 2,
		.wide_glyph_stride = 4,
		.mapping = mapping,
		.mapping_count = sizeof(mapping) / sizeof(mapping[0]),
		.fallback_glyph = 0,
	};
	const struct st_console_display_config config_display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config config = {
		.columns = 4,
		.rows = 2,
		.cell_width = 8,
		.cell_height = 2,
	};
	const struct st_console_rect row = {0, 0, 4, 1};
	const struct st_console_rect single = {1, 0, 1, 1};
	struct st_console_cell cells[4];
	struct st_console_cell before[8];
	struct st_console_cell value = {.glyph = 1};
	struct st_console_cell wide = {.glyph = 2};
	st_console_t *console = NULL;
	uint16_t identifier;
	uint32_t x;
	uint32_t y;

	CHECK(st_console_create(&console, &config_display, &config) == 0);
	identifier = register_static_font(console, &font);
	CHECK(identifier == 0);
	CHECK(st_console_put_codepoint(console, UINT32_C(0x4e00)) == 0);
	CHECK(st_console_read_cells(console, &row, cells, 4) == 0);
	CHECK(cells[0].glyph == 2 && cells[0].glyph_font == 0);
	CHECK((cells[0].flags & ST_CONSOLE_CELL_CONTINUATION) == 0);
	CHECK(cells[1].glyph == 2 && cells[1].glyph_font == 0);
	CHECK((cells[1].flags & ST_CONSOLE_CELL_CONTINUATION) != 0);
	CHECK(st_console_get_position(console, &x, &y) == 0);
	CHECK(x == 2 && y == 0);
	CHECK(st_console_write_cells(console, &single, &value, 1) == 0);
	CHECK(read_cell(console, 0, 0).glyph == 0);
	CHECK(read_cell(console, 1, 0).glyph == 1);
	CHECK(st_console_write_cells(console, &single, &wide, 1)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_console_fill(console, &single, &wide)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_console_read_cells(console,
	    &(const struct st_console_rect){0, 0, 4, 2}, before, 4) == 0);
	CHECK(st_console_move_cells_clear(console,
	    &(const struct st_console_rect){0, 0, 1, 1}, 2, 0, &wide)
	    == IO_ERROR_INVALID_ARGUMENT);
	check_text_state(console,
	    &(const struct st_console_rect){0, 0, 4, 2}, before, 4, 2, 0);
	CHECK(st_console_clear_window(console, UINT32_C(0x4e00))
	    == IO_ERROR_INVALID_ARGUMENT);
	check_text_state(console,
	    &(const struct st_console_rect){0, 0, 4, 2}, before, 4, 2, 0);
	CHECK(st_console_clear_line(console, UINT32_C(0x4e00))
	    == IO_ERROR_INVALID_ARGUMENT);
	check_text_state(console,
	    &(const struct st_console_rect){0, 0, 4, 2}, before, 4, 2, 0);
	CHECK(st_console_scroll(console, 1, UINT32_C(0x4e00))
	    == IO_ERROR_INVALID_ARGUMENT);
	check_text_state(console,
	    &(const struct st_console_rect){0, 0, 4, 2}, before, 4, 2, 0);
	CHECK(st_console_insert_lines(console, 1, UINT32_C(0x4e00))
	    == IO_ERROR_INVALID_ARGUMENT);
	check_text_state(console,
	    &(const struct st_console_rect){0, 0, 4, 2}, before, 4, 2, 0);
	CHECK(st_console_delete_lines(console, 1, UINT32_C(0x4e00))
	    == IO_ERROR_INVALID_ARGUMENT);
	check_text_state(console,
	    &(const struct st_console_rect){0, 0, 4, 2}, before, 4, 2, 0);
	CHECK(st_console_set_position(console, 3, 0) == 0);
	CHECK(st_console_set_wrap(console, false) == 0);
	CHECK(st_console_put_codepoint(console, UINT32_C(0x4e00)) == 0);
	CHECK(read_cell(console, 3, 0).glyph == ' ');
	CHECK(st_console_get_position(console, &x, &y) == 0);
	CHECK(x == 3 && y == 0);
	CHECK(st_console_set_wrap(console, true) == 0);
	CHECK(st_console_put_codepoint(console, UINT32_C(0x4e00)) == 0);
	{
		const struct st_console_rect pair = {0, 1, 2, 1};

		CHECK(st_console_read_cells(console, &pair, cells, 2) == 0);
		CHECK(cells[0].glyph == 2);
		CHECK((cells[1].flags & ST_CONSOLE_CELL_CONTINUATION) != 0);
	}
	CHECK(st_console_get_position(console, &x, &y) == 0);
	CHECK(x == 2 && y == 1);
	st_console_destroy(console);

	console = create_headless(4, 2);
	identifier = register_static_font(console, &font);
	CHECK(identifier == 0);
	{
		const struct st_console_rect narrow_window = {1, 0, 1, 2};
		const struct st_console_rect marker = {1, 1, 1, 1};
		const struct st_console_cell letter = {.glyph = 1};

		CHECK(st_console_set_window(console, &narrow_window) == 0);
		CHECK(st_console_set_wrap(console, false) == 0);
		CHECK(st_console_put_codepoint(console, UINT32_C(0x4e00)) == 0);
		CHECK(st_console_get_position(console, &x, &y) == 0);
		CHECK(x == 1 && y == 0);
		CHECK(read_cell(console, 1, 0).glyph == ' ');
		CHECK(read_cell(console, 2, 0).glyph == ' ');

		CHECK(st_console_set_wrap(console, true) == 0);
		CHECK(st_console_put_codepoint(console, UINT32_C(0x4e00)) == 0);
		CHECK(st_console_get_position(console, &x, &y) == 0);
		CHECK(x == 1 && y == 1);
		CHECK(read_cell(console, 1, 1).glyph == ' ');
		CHECK(read_cell(console, 2, 1).glyph == ' ');

		CHECK(st_console_write_cells(console, &marker, &letter, 1) == 0);
		CHECK(st_console_set_position(console, 1, 1) == 0);
		CHECK(st_console_put_codepoint(console, UINT32_C(0x4e00)) == 0);
		CHECK(st_console_get_position(console, &x, &y) == 0);
		CHECK(x == 1 && y == 1);
		CHECK(read_cell(console, 1, 0).glyph == 1);
		CHECK(read_cell(console, 1, 1).glyph == 0);
		CHECK(read_cell(console, 2, 0).glyph == ' ');
		CHECK(read_cell(console, 2, 1).glyph == ' ');
	}
	st_console_destroy(console);

	console = create_headless(1, 2);
	identifier = register_static_font(console, &font);
	CHECK(identifier == 0);
	CHECK(st_console_put_codepoint(console, UINT32_C(0x4e00)) == 0);
	CHECK(st_console_get_position(console, &x, &y) == 0);
	CHECK(x == 0 && y == 1);
	CHECK(read_cell(console, 0, 0).glyph == ' ');
	CHECK(read_cell(console, 0, 1).glyph == ' ');
	st_console_destroy(console);
}

enum wide_window_operation {
	WIDE_WINDOW_SCROLL_UP,
	WIDE_WINDOW_SCROLL_DOWN,
	WIDE_WINDOW_INSERT,
	WIDE_WINDOW_DELETE,
};

static st_console_t *
create_wide_boundary_console(bool pixels)
{
	static const uint8_t narrow_bitmap[] = {0x00, 0xff};
	static const uint8_t wide_bitmap[] = {0xff, 0xff};
	static const struct st_console_font_mapping mapping[] = {
		{' ', 0},
		{'A', 1},
		{UINT32_C(0x4e00), 2},
	};
	static const struct st_console_font font = {
		.bitmap = narrow_bitmap,
		.bitmap_size = sizeof(narrow_bitmap),
		.width = 8,
		.height = 1,
		.glyph_count = 2,
		.row_stride = 1,
		.glyph_stride = 1,
		.wide_bitmap = wide_bitmap,
		.wide_bitmap_size = sizeof(wide_bitmap),
		.wide_glyph_count = 1,
		.wide_row_stride = 2,
		.wide_glyph_stride = 2,
		.mapping = mapping,
		.mapping_count = sizeof(mapping) / sizeof(mapping[0]),
		.fallback_glyph = 0,
	};
	const struct st_console_display_config display_config = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config config = {
		.columns = 6,
		.rows = 4,
		.cell_width = pixels ? 8 : 0,
		.cell_height = pixels ? 1 : 0,
		.pixel_width = pixels ? 49 : 0,
		.pixel_height = pixels ? 5 : 0,
	};
	st_console_t *console = NULL;

	CHECK(st_console_create(&console, &display_config, &config) == 0);
	CHECK(register_static_font(console, &font) == 0);
	CHECK(st_console_set_wrap(console, false) == 0);
	for (uint32_t y = 0; y < 4; ++y) {
		CHECK(st_console_set_position(console, 0, y) == 0);
		CHECK(st_console_put_codepoint(console, UINT32_C(0x4e00)) == 0);
		CHECK(st_console_set_position(console, 4, y) == 0);
		CHECK(st_console_put_codepoint(console, UINT32_C(0x4e00)) == 0);
	}
	return console;
}

static void
check_wide_boundary_rows(st_console_t *console, uint32_t first_cleared)
{
	const struct st_console_rect row = {0, 0, 6, 1};
	struct st_console_cell cells[6];

	for (uint32_t y = 0; y < 4; ++y) {
		struct st_console_rect current = row;

		current.y = y;
		CHECK(st_console_read_cells(console, &current, cells, 6) == 0);
		for (uint32_t x = 0; x < 6; ++x) {
			bool continuation = (cells[x].flags
			    & ST_CONSOLE_CELL_CONTINUATION) != 0;

			if (continuation) {
				CHECK(x > 0);
				CHECK(cells[x - 1].glyph == 2);
				CHECK((cells[x - 1].flags
				    & ST_CONSOLE_CELL_CONTINUATION) == 0);
			}
			else if (cells[x].glyph == 2) {
				CHECK(x + 1 < 6);
				CHECK((cells[x + 1].flags
				    & ST_CONSOLE_CELL_CONTINUATION) != 0);
			}
		}
		if (y >= first_cleared) {
			CHECK(cells[0].glyph == 0);
			CHECK(cells[5].glyph == 0);
		}
		else {
			CHECK(cells[0].glyph == 2);
			CHECK((cells[1].flags
			    & ST_CONSOLE_CELL_CONTINUATION) != 0);
			CHECK(cells[4].glyph == 2);
			CHECK((cells[5].flags
			    & ST_CONSOLE_CELL_CONTINUATION) != 0);
		}
	}
}

static void
test_wide_window_row_operations(void)
{
	const struct st_console_rect window = {1, 0, 4, 4};

	for (enum wide_window_operation operation = WIDE_WINDOW_SCROLL_UP;
	    operation <= WIDE_WINDOW_DELETE; ++operation) {
		st_console_t *console = create_wide_boundary_console(false);
		uint32_t first_cleared = 0;

		CHECK(st_console_set_window(console, &window) == 0);
		if (operation >= WIDE_WINDOW_INSERT) {
			first_cleared = 1;
			CHECK(st_console_set_position(console, 1, 1) == 0);
		}
		switch (operation) {
		case WIDE_WINDOW_SCROLL_UP:
			CHECK(st_console_scroll(console, 1, ' ') == 0);
			break;
		case WIDE_WINDOW_SCROLL_DOWN:
			CHECK(st_console_scroll(console, -1, ' ') == 0);
			break;
		case WIDE_WINDOW_INSERT:
			CHECK(st_console_insert_lines(console, 1, ' ') == 0);
			break;
		case WIDE_WINDOW_DELETE:
			CHECK(st_console_delete_lines(console, 1, ' ') == 0);
			break;
		}
		check_wide_boundary_rows(console, first_cleared);
		st_console_destroy(console);
	}
}

static void
test_wide_window_cached_scroll(void)
{
	const struct st_console_rect window = {1, 0, 4, 4};
	const struct st_console_rect raster = {0, 0, 49, 5};
	const uint32_t red = ST_CONSOLE_COLOR_RGB_VALUE(0xff, 0, 0);
	const uint32_t green = ST_CONSOLE_COLOR_RGB_VALUE(0, 0xff, 0);
	const uint32_t blue = ST_CONSOLE_COLOR_RGB_VALUE(0, 0, 0xff);
	struct st_pixel_buffer *pixels = NULL;
	st_console_t *console = create_wide_boundary_console(true);

	CHECK(st_console_set_window(console, &window) == 0);
	CHECK(st_console_set_pixel(console, 16, 1, red) == 0);
	CHECK(st_console_set_pixel(console, 48, 1, green) == 0);
	CHECK(st_console_set_pixel(console, 0, 4, blue) == 0);
	CHECK(st_console_scroll(console, 1, ' ') == 0);
	CHECK(st_console_get_pixels(&pixels, console, &raster, false) == 0);
	CHECK(pixels->primary[16] == red);
	CHECK(pixels->primary[0] == 0);
	CHECK(pixels->primary[40] == 0);
	CHECK(pixels->primary[49 + 48] == green);
	CHECK(pixels->primary[(size_t)4 * 49] == blue);
	st_pixel_buffer_destroy(pixels);
	check_wide_boundary_rows(console, 0);
	st_console_destroy(console);
}

static void
test_ansi_reveal(void)
{
	static const uint8_t bitmap[24] = {0};
	static const struct st_console_font_mapping mapping[] = {
		{'A', 0}, {'B', 1}, {'C', 2},
	};
	const struct st_console_font font = {
		.bitmap = bitmap, .bitmap_size = sizeof(bitmap),
		.width = 8, .height = 8, .glyph_count = 3,
		.row_stride = 1, .glyph_stride = 8,
		.mapping = mapping, .mapping_count = 3,
	};
	struct output output = {0};
	const struct st_console_ansi_io io = {.write = write_output, .context = &output};
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_ANSI, .ansi_io = &io,
	};
	const struct st_console_config config = {
		.columns = 3, .rows = 1, .cell_width = 8, .cell_height = 8,
	};
	const struct st_console_rect area = {0, 0, 3, 1};
	struct st_console_cell cells[3], readback[3];
	st_console_t *console = NULL;
	uint16_t identifier;

	CHECK(st_console_create(&console, &display, &config) == 0);
	CHECK(st_console_register_static_font(console, &font, &identifier) == 0);
	CHECK(st_console_present(console) == 0);
	output.length = 0;
	for (unsigned i = 0; i < 3; ++i)
		cells[i] = (struct st_console_cell){
			.font = identifier, .glyph_font = identifier, .glyph = i,
			.foreground = 7, .flags = ST_CONSOLE_CELL_CONCEAL
			    | (i == 1 ? ST_CONSOLE_CELL_REVEAL : 0),
		};
	CHECK(st_console_write_complete_cells(console, &area, cells, 3) == 0);
	CHECK(st_console_present(console) == 0);
	CHECK(count_bytes(output.data, output.length, (const unsigned char *)"\x1b[8m", 4) == 2);
	CHECK(st_console_read_cells(console, &area, readback, 3) == 0);
	CHECK((readback[1].flags & (ST_CONSOLE_CELL_CONCEAL | ST_CONSOLE_CELL_REVEAL))
	    == (ST_CONSOLE_CELL_CONCEAL | ST_CONSOLE_CELL_REVEAL));
	st_console_destroy(console);
}

static void
test_ansi_wide_cell_output(void)
{
	static const uint8_t narrow_bitmap[1] = {0};
	static const uint8_t wide_bitmap[2] = {0xff, 0xff};
	static const struct st_console_font_mapping mapping[] = {
		{'?', 0},
		{UINT32_C(0x4e00), 1},
	};
	static const struct st_console_font font = {
		.bitmap = narrow_bitmap,
		.bitmap_size = sizeof(narrow_bitmap),
		.width = 8,
		.height = 1,
		.glyph_count = 1,
		.row_stride = 1,
		.glyph_stride = 1,
		.wide_bitmap = wide_bitmap,
		.wide_bitmap_size = sizeof(wide_bitmap),
		.wide_glyph_count = 1,
		.wide_row_stride = 2,
		.wide_glyph_stride = 2,
		.mapping = mapping,
		.mapping_count = sizeof(mapping) / sizeof(mapping[0]),
		.fallback_glyph = 0,
	};
	static const unsigned char encoded[] = {0xe4, 0xb8, 0x80};
	struct output output = {0};
	struct st_console_ansi_io io = {
		.write = write_output,
		.context = &output,
		.indexed_colours = 256,
	};
	struct st_console_display_config config_display = {
		.backend = ST_CONSOLE_BACKEND_ANSI,
		.ansi_io = &io,
	};
	struct st_console_config config = {
		.columns = 2,
		.rows = 1,
	};
	const struct st_console_rect row = {0, 0, 2, 1};
	struct st_console_cell cells[2] = {
		{.glyph = 1},
		{.glyph = 1,
		    .flags = ST_CONSOLE_CELL_CONTINUATION},
	};
	st_console_t *console = NULL;
	uint16_t identifier;

	CHECK(st_console_create(&console, &config_display, &config) == 0);
	identifier = register_static_font(console, &font);
	cells[0].font = identifier;
	cells[1].font = identifier;
	cells[0].glyph_font = identifier;
	cells[1].glyph_font = identifier;
	CHECK(st_console_write_cells(console, &row, cells, 2) == 0);
	CHECK(count_bytes(output.data, output.length, encoded, sizeof(encoded))
	    == 1);
	st_console_destroy(console);
}

static void
test_ansi_output_modes(void)
{
	static const char *const foregrounds[] = {
		"\033[91m", "\033[38;5;203m", "\033[38;2;255;84;84m",
		"\033[38;2;255;84;84m",
	};
	static const char *const rgb_foregrounds[] = {
		"\033[96m", "\033[38;5;43m", "\033[38;2;0;215;175m",
		"\033[38;2;0;215;175m",
	};
	static const uint16_t indexed_colours[] = {16, 256, 16, 256};
	static const bool direct_colour[] = {false, false, true, true};
	static const char scroll[] = "\033[1;3r\033[1S\033[r";

	for (size_t mode = 0; mode < sizeof(indexed_colours)
	    / sizeof(indexed_colours[0]); ++mode) {
		struct output output = {0};
		struct st_console_ansi_io io = {
			.write = write_output,
			.context = &output,
			.indexed_colours = indexed_colours[mode],
			.direct_colour = direct_colour[mode],
		};
		struct st_console_display_config config_display = {
			.backend = ST_CONSOLE_BACKEND_ANSI,
			.ansi_io = &io,
		};
		struct st_console_config config = {
			.columns = 3,
			.rows = 3,
		};
		struct st_console_cell cells[9];
		const struct st_console_rect screen = {0, 0, 3, 3};
		st_console_t *console = NULL;
		uint32_t capabilities;

		CHECK(st_console_create(&console, &config_display, &config) == 0);
		CHECK(st_console_capabilities(console, &capabilities) == 0);
		CHECK(((capabilities & ST_CONSOLE_CAP_PALETTE_CHANGE) != 0)
		    == direct_colour[mode]);
		CHECK(((capabilities & ST_CONSOLE_CAP_EXTENDED_PALETTE) != 0)
		    == direct_colour[mode]);
		for (size_t i = 0; i < 9; ++i) {
			cells[i] = (struct st_console_cell){
				.glyph = (uint32_t)('a' + i / 3),
				.foreground = 9,
			};
		}
		cells[1].foreground = ST_CONSOLE_COLOR_RGB_VALUE(0, 215, 175);
		CHECK(st_console_begin_update(console) == 0);
		CHECK(st_console_write_complete_cells(console, &screen, cells, 3)
		    == 0);
		CHECK(st_console_end_update(console) == 0);
		CHECK(contains(output.data, output.length, foregrounds[mode]));
		CHECK(contains(output.data, output.length, rgb_foregrounds[mode]));
		output.length = 0;
		for (size_t i = 0; i < 6; ++i)
			cells[i] = cells[i + 3];
		for (size_t i = 6; i < 9; ++i) {
			cells[i].glyph = 'd';
		}
		CHECK(st_console_begin_update(console) == 0);
		CHECK(st_console_write_complete_cells(console, &screen, cells, 3)
		    == 0);
		CHECK(st_console_end_update(console) == 0);
		CHECK(contains(output.data, output.length, scroll));
		st_console_destroy(console);
	}
}

static void
test_ansi_cp437_output(void)
{
	static const uint8_t bitmap[] = {0x00, 0xff};
	static const struct st_console_font_mapping mapping[] = {
		{'?', 0},
		{UINT32_C(0x00e9), 1},
	};
	static const struct st_console_font font = {
		.bitmap = bitmap,
		.bitmap_size = sizeof(bitmap),
		.width = 8,
		.height = 1,
		.glyph_count = 2,
		.row_stride = 1,
		.glyph_stride = 1,
		.mapping = mapping,
		.mapping_count = sizeof(mapping) / sizeof(mapping[0]),
		.fallback_glyph = 0,
	};
	const unsigned char expected = 0x82;
	static const char restore_cursor[] = "\033[?25h\033[0 q";
	struct output output = {0};
	struct st_console_ansi_io io = {
		.write = write_output,
		.context = &output,
		.indexed_colours = 256,
	};
	struct st_console_display_config config_display = {
		.backend = ST_CONSOLE_BACKEND_ANSI,
		.text_presentation = ST_CONSOLE_TEXT_CP437,
		.ansi_io = &io,
	};
	struct st_console_config config = {
		.columns = 1,
		.rows = 1,
	};
	const struct st_console_rect cell_rect = {0, 0, 1, 1};
	struct st_console_cell cell = {
		.glyph = 1,
		.foreground = 7,
	};
	st_console_t *console = NULL;
	uint16_t identifier;

	CHECK(st_console_create(&console, &config_display, &config) == 0);
	identifier = register_static_font(console, &font);
	CHECK(identifier == 0);
	cell.font = identifier;
	cell.glyph_font = identifier;
	output.length = 0;
	CHECK(st_console_write_cells(console, &cell_rect, &cell, 1) == 0);
	CHECK(count_bytes(output.data, output.length, &expected, 1) == 1);
	cell.glyph = expected;
	output.length = 0;
	CHECK(st_console_write_complete_cells(console, &cell_rect, &cell, 1)
	    == 0);
	CHECK(count_bytes(output.data, output.length, &expected, 1) == 0);
	st_console_destroy(console);
	CHECK(contains(output.data, output.length, restore_cursor));
}

static void
test_ascii_character_identity(void)
{
	static const uint8_t bitmap[3] = {0};
	static const struct st_console_font_mapping mapping[] = {
		{UINT32_C(0x001e), 2},
		{UINT32_C(0x2556), 0},
		{UINT32_C(0x1f642), 1},
	};
	static const struct st_console_font font = {
		.bitmap = bitmap,
		.bitmap_size = sizeof(bitmap),
		.width = 8,
		.height = 1,
		.glyph_count = 3,
		.row_stride = 1,
		.glyph_stride = 1,
		.mapping = mapping,
		.mapping_count = sizeof(mapping) / sizeof(mapping[0]),
	};
	st_console_t *console = create_headless(1, 1);
	struct st_console_cell cell = {0};
	bool alternate;

	CHECK(register_static_font(console, &font) == 0);
	CHECK(st_console_ascii_character(console, &cell, &alternate) == 'k');
	CHECK(alternate);
	cell.glyph = 1;
	CHECK(st_console_ascii_character(console, &cell, &alternate) == '?');
	CHECK(!alternate);
	cell.glyph = 2;
	CHECK(st_console_ascii_character(console, &cell, &alternate) == '^');
	CHECK(!alternate);
	CHECK(st_console_cp437_character(UINT32_C(0x00e9)) == 0x82);
	CHECK(st_console_cp437_character(UINT32_C(0x1f642)) == '?');
	st_console_destroy(console);
}

static void
test_font_resolution_and_complete_cells(void)
{
	static const uint8_t fallback_bitmap[] = {0x00, 0x40, 0x20};
	static const struct st_console_font_mapping fallback_mapping[] = {
		{'?', 0},
		{'A', 1},
		{'B', 2},
	};
	static const struct st_console_font fallback_font = {
		.bitmap = fallback_bitmap,
		.bitmap_size = sizeof(fallback_bitmap),
		.width = 8,
		.height = 1,
		.glyph_count = 3,
		.row_stride = 1,
		.glyph_stride = 1,
		.mapping = fallback_mapping,
		.mapping_count = sizeof(fallback_mapping)
		    / sizeof(fallback_mapping[0]),
	};
	uint8_t selected_bitmap[] = {0x00, 0x10, 0x08};
	struct st_console_font_mapping selected_mapping[] = {{'A', 2}};
	struct st_console_font selected_font = {
		.bitmap = selected_bitmap,
		.bitmap_size = sizeof(selected_bitmap),
		.width = 8,
		.height = 1,
		.glyph_count = 3,
		.row_stride = 1,
		.glyph_stride = 1,
		.mapping = selected_mapping,
		.mapping_count = sizeof(selected_mapping)
		    / sizeof(selected_mapping[0]),
	};
	static const uint8_t mismatched_bitmap[] = {0x00, 0x00};
	static const struct st_console_font mismatched_font = {
		.bitmap = mismatched_bitmap,
		.bitmap_size = sizeof(mismatched_bitmap),
		.width = 8,
		.height = 2,
		.glyph_count = 1,
		.row_stride = 1,
		.glyph_stride = 2,
		.fallback_glyph = 0,
	};
	const struct st_console_font_selection selection = {
		.normal = 10,
		.bright = 11,
		.blink = 12,
		.bright_blink = 13,
		.bright_selects_font = true,
		.blink_selects_font = true,
	};
	const struct st_console_font_selection style_only = {
		.normal = 20,
		.bright = 21,
		.blink = 22,
		.bright_blink = 23,
	};
	const struct st_console_display_config config_display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config config = {
		.columns = 2,
		.rows = 1,
		.cell_width = 8,
		.cell_height = 1,
	};
	const struct st_console_rect first = {0, 0, 1, 1};
	struct st_console_cell cell = {0};
	struct st_console_cell read;
	st_console_t *console = NULL;
	uint16_t fallback;
	uint16_t selected;
	uint16_t mismatched;
	uint16_t glyph_font;
	uint32_t glyph;
	uint8_t columns;

	CHECK(st_console_create(&console, &config_display, &config) == 0);
	fallback = register_static_font(console, &fallback_font);
	CHECK(fallback == 0);
	CHECK(st_console_register_font_copy(console, &selected_font, &selected)
	    == 0);
	CHECK(selected == 1);
	selected_mapping[0].glyph = 1;
	selected_bitmap[2] = 0xff;
	CHECK(st_console_font_lookup(console, selected, 'A', &glyph_font,
	    &glyph, &columns) == 0);
	CHECK(glyph_font == selected && glyph == 2 && columns == 1);
	CHECK(st_console_font_lookup(console, selected, 'B', &glyph_font,
	    &glyph, &columns) == 0);
	CHECK(glyph_font == fallback && glyph == 2 && columns == 1);
	CHECK(st_console_font_lookup(console, selected, 1, &glyph_font,
	    &glyph, &columns) == 0);
	CHECK(glyph_font == fallback && glyph == 0 && columns == 1);
	CHECK(st_console_register_static_font(console, &mismatched_font,
	    &mismatched) == 0);
	CHECK(st_console_font_lookup(console, mismatched, 'B', &glyph_font,
	    &glyph, &columns) == 0);
	CHECK(glyph_font == mismatched && glyph == 0 && columns == 1);

	CHECK(st_console_font_for_flags(&selection, 0) == 10);
	CHECK(st_console_font_for_flags(&selection, ST_CONSOLE_CELL_BRIGHT) == 11);
	CHECK(st_console_font_for_flags(&selection, ST_CONSOLE_CELL_BLINK) == 12);
	CHECK(st_console_font_for_flags(&selection,
	    ST_CONSOLE_CELL_BRIGHT | ST_CONSOLE_CELL_BLINK) == 13);
	CHECK(st_console_font_for_flags(&style_only,
	    ST_CONSOLE_CELL_BRIGHT | ST_CONSOLE_CELL_BLINK) == 20);
	CHECK(st_console_font_for_flags(NULL, ST_CONSOLE_CELL_BOLD) == 0);

	cell.glyph = 2;
	cell.font = selected;
	cell.glyph_font = selected;
	CHECK(st_console_write_cells(console, &first, &cell, 1) == 0);
	read = read_cell(console, 0, 0);
	CHECK(read.font == selected && read.glyph_font == selected
	    && read.glyph == 2);
	cell.glyph = 2;
	cell.glyph_font = fallback;
	CHECK(st_console_write_cells(console, &first, &cell, 1) == 0);
	read = read_cell(console, 0, 0);
	CHECK(read.font == selected && read.glyph_font == fallback
	    && read.glyph == 2);
	CHECK(st_console_write_complete_cells(console, &first, &read, 1) == 0);
	read = read_cell(console, 0, 0);
	CHECK(read.font == selected && read.glyph_font == fallback
	    && read.glyph == 2);
	cell.glyph = 2;
	cell.glyph_font = selected;
	CHECK(st_console_write_cells(console, &first, &cell, 1) == 0);
	read = read_cell(console, 0, 0);
	CHECK(read.font == selected && read.glyph_font == selected
	    && read.glyph == 2);
	cell = read;
	cell.glyph = 1;
	CHECK(st_console_write_complete_cells(console, &first, &cell, 1) == 0);
	read = read_cell(console, 0, 0);
	CHECK(read.glyph == 1 && read.font == selected
	    && read.glyph_font == selected);
	st_console_destroy(console);
}

static void
test_zero_width_text(void)
{
	static const uint8_t bitmap[4] = {0, 0x80, 0x40, 0xff};
	static const uint8_t wide_bitmap[2] = {0xff, 0xff};
	static const struct st_console_font_mapping mapping[] = {
		{0, 0}, {' ', 0}, {'?', 3}, {'A', 1}, {'B', 2},
		{0x2060, 4}, {0xfeff, 4},
	};
	static const struct st_console_font font = {
		.bitmap = bitmap, .bitmap_size = sizeof(bitmap),
		.width = 8, .height = 1, .glyph_count = 4,
		.row_stride = 1, .glyph_stride = 1,
		.wide_bitmap = wide_bitmap, .wide_bitmap_size = sizeof(wide_bitmap),
		.wide_glyph_count = 1, .wide_row_stride = 2, .wide_glyph_stride = 2,
		.mapping = mapping, .mapping_count = sizeof(mapping) / sizeof(mapping[0]),
		.fallback_glyph = 3, .wide_fallback_glyph = 4,
	};
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config config = {
		.columns = 4, .rows = 2, .cell_width = 8, .cell_height = 1, .wrap = true,
	};
	const struct st_console_rect full = {0, 0, 4, 2};
	struct st_console_cell before[8], after[8];
	st_console_t *console = NULL;
	uint16_t owner;
	uint32_t glyph, x, y;
	uint8_t columns;

	CHECK(st_console_create(&console, &display, &config) == 0);
	owner = 123;
	glyph = 456;
	CHECK(st_console_font_lookup_fallback(console, 0, 1, &owner, &glyph)
	    == IO_ERROR_NOT_FOUND);
	CHECK(owner == 123 && glyph == 456);
	CHECK(register_static_font(console, &font) == 0);
	CHECK(st_console_clear_window(console, ' ') == 0);
	CHECK(st_console_font_lookup(console, 0, 0x2060, &owner, &glyph, &columns) == 0);
	CHECK(columns == 2 && glyph == 4);
	CHECK(st_console_font_lookup_fallback(console, 0, 1, &owner, &glyph) == 0);
	CHECK(owner == 0 && glyph == 3);
	CHECK(st_console_font_lookup_fallback(console, 0, 2, &owner, &glyph) == 0);
	CHECK(glyph == 4);
	owner = 123;
	glyph = 456;
	CHECK(st_console_font_lookup_fallback(console, 0, 0, &owner, &glyph)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(owner == 123 && glyph == 456);
	CHECK(st_console_write_utf8(console, "A\xe2\x81\xa0\xef\xbb\xbf" "B", 8) == 0);
	CHECK(st_console_get_position(console, &x, &y) == 0 && x == 2 && y == 0);
	CHECK(st_console_read_cells(console, &full, before, 4) == 0);
	CHECK(before[0].glyph == 1 && before[1].glyph == 2);
	for (size_t i = 0; i < 8; ++i)
		CHECK((before[i].flags & ST_CONSOLE_CELL_CONTINUATION) == 0);
	CHECK(st_console_set_position(console, 3, 1) == 0);
	CHECK(st_console_write_utf8(console, "\xe2\x81\xa0\xef\xbb\xbf\0", 7) == 0);
	CHECK(st_console_put_codepoint(console, 0x2060) == 0);
	CHECK(st_console_get_position(console, &x, &y) == 0 && x == 3 && y == 1);
	CHECK(st_console_read_cells(console, &full, after, 4) == 0);
	CHECK(memcmp(before, after, sizeof(before)) == 0);
	CHECK(st_console_font_lookup(console, 0, 0, &owner, &glyph, &columns) == 0);
	CHECK(glyph == 0 && columns == 1);
	CHECK(st_console_set_position(console, 2, 0) == 0);
	CHECK(st_console_write_utf8(console, "\xcc\x81", 2) == 0);
	CHECK(st_console_get_position(console, &x, &y) == 0 && x == 3 && y == 0);
	CHECK(read_cell(console, 2, 0).glyph == 3);
	CHECK(st_console_set_position(console, 0, 1) == 0);
	CHECK(st_console_put_codepoint(console, 0x301) == 0);
	CHECK(st_console_get_position(console, &x, &y) == 0 && x == 1 && y == 1);
	CHECK(st_console_set_position(console, 2, 0) == 0);
	CHECK(st_console_write_utf8(console, "\r\nB", 3) == 0);
	CHECK(st_console_get_position(console, &x, &y) == 0 && x == 1 && y == 1);
	CHECK(read_cell(console, 0, 1).glyph == 2);
	{
		static const uint8_t tall_bitmap[8] = {0};
		static const uint8_t tall_wide[4] = {0};
		struct st_console_font tall = font;
		tall.height = 2;
		tall.bitmap = tall_bitmap;
		tall.bitmap_size = sizeof(tall_bitmap);
		tall.glyph_stride = 2;
		tall.wide_bitmap = tall_wide;
		tall.wide_bitmap_size = sizeof(tall_wide);
		tall.wide_glyph_stride = 4;
		tall.wide_fallback_glyph = ST_CONSOLE_FONT_NO_WIDE_FALLBACK;
		uint16_t identifier = register_static_font(console, &tall);
		CHECK(identifier != 0);
		CHECK(st_console_font_lookup_fallback(console, identifier, 1, &owner, &glyph) == 0);
		CHECK(owner == identifier && glyph == 3);
		owner = 123;
		glyph = 456;
		CHECK(st_console_font_lookup_fallback(console, identifier, 2, &owner, &glyph)
		    == IO_ERROR_NOT_FOUND);
		CHECK(owner == 123 && glyph == 456);
		/* Borrowed registration remains live until console destruction. */
		st_console_destroy(console);
	}
}

static void
test_cluster_font_mapping(void)
{
	static const char waving_hand_tone[] =
	    "\xf0\x9f\x91\x8b\xf0\x9f\x8f\xbd";
	static const uint8_t bitmap[2] = {0};
	static const uint8_t wide_bitmap[2] = {0};
	static const struct st_console_font_mapping mapping[] = {
		{'?', 0}, {'A', 1},
	};
	static const struct st_console_font_cluster_mapping clusters[] = {
		{"A", 1, 0},
		{"B", 1, 1},
		{waving_hand_tone, sizeof(waving_hand_tone) - 1, 2},
	};
	static const struct st_console_font font = {
		.bitmap = bitmap,
		.bitmap_size = sizeof(bitmap),
		.width = 8,
		.height = 1,
		.glyph_count = 2,
		.row_stride = 1,
		.glyph_stride = 1,
		.wide_bitmap = wide_bitmap,
		.wide_bitmap_size = sizeof(wide_bitmap),
		.wide_glyph_count = 1,
		.wide_row_stride = 2,
		.wide_glyph_stride = 2,
		.mapping = mapping,
		.mapping_count = sizeof(mapping) / sizeof(mapping[0]),
		.cluster_mapping = clusters,
		.cluster_mapping_count = sizeof(clusters) / sizeof(clusters[0]),
	};
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config config = {.columns = 2, .rows = 1};
	const struct st_console_rect row = {0, 0, 2, 1};
	struct st_console_cell cells[2];
	char copied[sizeof(waving_hand_tone)];
	size_t copied_size;
	st_console_t *console = NULL;
	uint16_t owner;
	uint16_t identifier;
	uint32_t glyph;
	uint8_t columns;

	CHECK(st_console_create(&console, &display, &config) == 0);
	identifier = register_static_font(console, &font);
	CHECK(identifier == 0);
	/* Scalar mappings win over single-scalar cluster mappings, but a
	 * cluster mapping remains usable when the scalar table has no entry. */
	CHECK(st_console_font_lookup(console, identifier, 'A', &owner,
	    &glyph, &columns) == 0);
	CHECK(owner == identifier && glyph == 1 && columns == 1);
	CHECK(st_console_font_lookup(console, identifier, 'B', &owner,
	    &glyph, &columns) == 0);
	CHECK(owner == identifier && glyph == 1 && columns == 1);
	CHECK(st_console_font_lookup_cluster(console, identifier,
	    waving_hand_tone, sizeof(waving_hand_tone) - 1, 2, &owner,
	    &glyph) == 0);
	CHECK(owner == identifier && glyph == 2);
	cells[0] = (struct st_console_cell){
		.glyph = glyph,
		.font = identifier,
		.glyph_font = owner,
	};
	cells[1] = cells[0];
	cells[1].flags = ST_CONSOLE_CELL_CONTINUATION;
	CHECK(st_console_write_cells(console, &row, cells, 2) == 0);
	CHECK(st_console_read_cells(console, &row, cells, 2) == 0);
	CHECK(cells[0].glyph == glyph && cells[0].glyph_font == owner);
	CHECK((cells[1].flags & ST_CONSOLE_CELL_CONTINUATION) != 0);
	CHECK(st_console_write_utf8(console, waving_hand_tone,
	    sizeof(waving_hand_tone) - 1) == 0);
	CHECK(st_console_read_cells(console, &row, cells, 2) == 0);
	CHECK(cells[0].glyph == 2 && cells[0].glyph_font == identifier);
	CHECK((cells[0].flags & ST_CONSOLE_CELL_CONTINUATION) == 0);
	CHECK(cells[1].glyph == 2 && cells[1].glyph_font == identifier);
	CHECK((cells[1].flags & ST_CONSOLE_CELL_CONTINUATION) != 0);
	CHECK(st_console_font_glyph_text(console, identifier, glyph, copied,
	    sizeof(copied), &copied_size) == 0);
	CHECK(copied_size == sizeof(waving_hand_tone) - 1
	    && memcmp(copied, waving_hand_tone, copied_size) == 0);
	st_console_destroy(console);
}

static void
test_circular_row_moves(void)
{
	enum { WIDTH = 6, HEIGHT = 7, COUNT = WIDTH * HEIGHT };
	static const uint8_t bitmap[256] = {0, 0x80, 0xff};
	static const uint8_t wide_bitmap[2] = {0xaa, 0x55};
	const struct st_console_font font = {
		.bitmap = bitmap, .bitmap_size = sizeof(bitmap), .width = 8,
		.height = 1, .glyph_count = 256, .row_stride = 1, .glyph_stride = 1,
		.wide_bitmap = wide_bitmap, .wide_bitmap_size = sizeof(wide_bitmap),
		.wide_glyph_count = 1, .wide_row_stride = 2, .wide_glyph_stride = 2,
	};
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config config = {
		.columns = WIDTH, .rows = HEIGHT, .cell_width = 8, .cell_height = 1,
	};
	const struct st_console_rect screen = {0, 0, WIDTH, HEIGHT};
	const struct st_console_rect raster = {0, 0, WIDTH * 8, HEIGHT};
	struct st_console_cell before[COUNT], expected[COUNT], actual[COUNT];
	struct st_console_cell fill = {.glyph = ' ', .foreground = 7};
	st_console_t *console = NULL;
	uint32_t random = 17;

	CHECK(st_console_create(&console, &display, &config) == 0);
	CHECK(register_static_font(console, &font) == 0);
	for (size_t i = 0; i < COUNT; ++i) {
		before[i] = (struct st_console_cell){
			.glyph = (uint32_t)(i % 3), .foreground = (uint32_t)(i % 16),
			.background = (uint32_t)(i / WIDTH),
			.flags = i % 2 == 0 ? ST_CONSOLE_CELL_BLINK : 0,
		};
	}
	for (size_t y = 0; y < HEIGHT; ++y) {
		before[y * WIDTH + 2].glyph = 256;
		before[y * WIDTH + 3] = before[y * WIDTH + 2];
		before[y * WIDTH + 3].flags |= ST_CONSOLE_CELL_CONTINUATION;
	}
	CHECK(st_console_write_complete_cells(console, &screen, before, WIDTH) == 0);
	for (unsigned pass = 0; pass < 350; ++pass) {
		struct st_console_rect source = {0, 1, WIDTH, HEIGHT - 2};
		uint32_t to = 0;
		bool clear = pass % 4 != 0;
		if (pass % 3 == 0) {
			random = random * UINT32_C(1664525) + UINT32_C(1013904223);
			source.height = 1 + random % (HEIGHT - 1);
			source.y = (random >> 8) % (HEIGHT - source.height + 1);
			to = (random >> 16) % (HEIGHT - source.height + 1);
		} else if (pass % 3 == 1) {
			source.y = 0;
			to = 1;
		}
		CHECK(st_console_read_cells(console, &screen, before, WIDTH) == 0);
		memcpy(expected, before, sizeof(expected));
		memcpy(expected + to * WIDTH, before + source.y * WIDTH,
		    source.height * WIDTH * sizeof(before[0]));
		if (clear) {
			for (uint32_t y = source.y; y < source.y + source.height; ++y)
				if (y < to || y >= to + source.height)
					for (uint32_t x = 0; x < WIDTH; ++x)
						expected[y * WIDTH + x] = fill;
			CHECK(st_console_move_cells_clear(console, &source, 0, to, &fill) == 0);
		} else {
			CHECK(st_console_move_cells(console, &source, 0, to) == 0);
		}
		CHECK(st_console_read_cells(console, &screen, actual, WIDTH) == 0);
		for (size_t i = 0; i < COUNT; ++i)
			CHECK(st_console_cells_equal(&expected[i], &actual[i]));
		struct st_pixel_buffer *cached = NULL, *forced = NULL;
		CHECK(st_console_get_pixels(&cached, console, &raster, false) == 0);
		CHECK(st_console_get_pixels(&forced, console, &raster, true) == 0);
		CHECK(memcmp(cached->primary, forced->primary,
		    WIDTH * 8 * HEIGHT * sizeof(uint32_t)) == 0);
		CHECK(memcmp(cached->alternate, forced->alternate,
		    WIDTH * 8 * HEIGHT * sizeof(uint32_t)) == 0);
		st_pixel_buffer_destroy(cached);
		st_pixel_buffer_destroy(forced);
		/* Re-seed a row so repeated scrolling cannot turn this into a
		 * vacuous comparison of all-blank screens. */
		before[0].glyph = pass % 3;
		before[0].background = pass % 16;
		CHECK(st_console_write_complete_cells(console,
		    &(const struct st_console_rect){0, HEIGHT - 2, 1, 1}, before, 1) == 0);
	}
	st_console_snapshot_t *snapshot = NULL;
	CHECK(st_console_read_cells(console, &screen, before, WIDTH) == 0);
	CHECK(st_console_snapshot_create(&snapshot, console) == 0);
	CHECK(st_console_scroll(console, 2, ' ') == 0);
	CHECK(st_console_snapshot_restore(console, snapshot) == 0);
	CHECK(st_console_read_cells(console, &screen, actual, WIDTH) == 0);
	CHECK(memcmp(before, actual, sizeof(before)) == 0);
	st_console_snapshot_destroy(snapshot);
	CHECK(st_console_set_window(console,
	    &(const struct st_console_rect){0, 1, WIDTH, HEIGHT - 2}) == 0);
	for (unsigned i = 0; i < HEIGHT * 3; ++i)
		CHECK(st_console_scroll(console, i % 3 == 0 ? -1 : 1, ' ') == 0);
	CHECK(st_console_read_cells(console, &screen, before, WIDTH) == 0);
	CHECK(st_console_resize(console, WIDTH + 1, HEIGHT + 1) == 0);
	CHECK(st_console_read_cells(console, &screen, actual, WIDTH) == 0);
	CHECK(memcmp(before, actual, sizeof(before)) == 0);
	st_console_destroy(console);
}

static void
test_pixel_row_rotation_preserves_outside(void)
{
	static const uint8_t bitmap[2] = {0};
	const struct st_console_font font = {
		.bitmap = bitmap, .bitmap_size = 2, .width = 2, .height = 2,
		.glyph_count = 1, .row_stride = 1, .glyph_stride = 2,
	};
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config config = {
		.columns = 4, .rows = 7, .cell_width = 2, .cell_height = 2,
		.pixel_width = 10, .pixel_height = 17,
	};
	const struct st_console_rect full = {0, 0, 10, 17};
	const struct st_console_cell fill = {
		.background = ST_CONSOLE_COLOR_RGB_VALUE(0x12, 0x34, 0x56),
	};
	const struct st_pixel_blit blit = {.width = 10, .height = 17,
		.scale_x = 1, .scale_y = 1};
	struct st_pixel_buffer *input = NULL;
	st_console_t *console = NULL;
	CHECK(st_console_create(&console, &display, &config) == 0);
	CHECK(register_static_font(console, &font) == 0);
	CHECK(st_pixel_buffer_create(&input, 10, 17, true) == 0);
	for (unsigned pass = 0; pass < 40; ++pass) {
		bool down = pass % 2 != 0;
		struct st_console_rect source = {0, down ? 1 : 2, 4, 4};
		uint32_t target = down ? 2 : 1;
		struct st_pixel_buffer *before = NULL, *after = NULL;
		uint32_t expected[170];
		/* Both pixel-origin policies, including extra right/bottom margins. */
		CHECK(st_console_set_full_surface_scroll(console, pass >= 20) == 0);
		for (size_t i = 0; i < 170; ++i) {
			input->primary[i] = ST_CONSOLE_COLOR_RGB_VALUE(0, pass, i);
			input->alternate[i] = ST_CONSOLE_COLOR_RGB_VALUE(pass, i, 0);
		}
		CHECK(st_console_blit_pixels(console, input, NULL, &blit) == 0);
		CHECK(st_console_get_pixels(&before, console, &full, false) == 0);
		CHECK(st_console_move_cells_clear(console, &source, 0, target, &fill) == 0);
		CHECK(st_console_get_pixels(&after, console, &full, false) == 0);
		for (unsigned plane = 0; plane < 2; ++plane) {
			const uint32_t *old = plane == 0 ? before->primary : before->alternate;
			const uint32_t *actual = plane == 0 ? after->primary : after->alternate;
			memcpy(expected, old, sizeof(expected));
			for (uint32_t y = 0; y < 8; ++y)
				memcpy(expected + (target * 2 + y) * 10,
				    old + (source.y * 2 + y) * 10, 8 * sizeof(uint32_t));
			uint32_t cleared = down ? source.y : source.y + source.height - 1;
			for (uint32_t y = cleared * 2; y < cleared * 2 + 2; ++y)
				for (uint32_t x = 0; x < 8; ++x)
					expected[y * 10 + x] = fill.background;
			CHECK(memcmp(expected, actual, sizeof(expected)) == 0);
		}
		st_pixel_buffer_destroy(before);
		st_pixel_buffer_destroy(after);
	}
	st_pixel_buffer_destroy(input);
	st_console_destroy(console);
}

static void
test_font_registration_preserves_cells(void)
{
	static const uint8_t bitmap[] = {0x00, 0x80};
	static const uint8_t wide_bitmap[] = {0xff, 0xff};
	static const struct st_console_font_mapping mapping[] = {
		{'A', 1},
		{UINT32_C(0x4e00), 2},
	};
	static const struct st_console_font font = {
		.bitmap = bitmap,
		.bitmap_size = sizeof(bitmap),
		.width = 8,
		.height = 1,
		.glyph_count = 2,
		.row_stride = 1,
		.glyph_stride = 1,
		.wide_bitmap = wide_bitmap,
		.wide_bitmap_size = sizeof(wide_bitmap),
		.wide_glyph_count = 1,
		.wide_row_stride = 2,
		.wide_glyph_stride = 2,
		.mapping = mapping,
		.mapping_count = sizeof(mapping) / sizeof(mapping[0]),
	};
	static const struct st_console_font_mapping selected_mapping[] = {
		{'A', 0},
	};
	static const struct st_console_font selected_font = {
		.bitmap = bitmap,
		.bitmap_size = sizeof(bitmap),
		.width = 8,
		.height = 1,
		.glyph_count = 2,
		.row_stride = 1,
		.glyph_stride = 1,
		.mapping = selected_mapping,
		.mapping_count = sizeof(selected_mapping)
		    / sizeof(selected_mapping[0]),
	};
	const struct st_console_display_config config_display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config config = {
		.columns = 2,
		.rows = 1,
		.cell_width = 8,
		.cell_height = 1,
	};
	const struct st_console_rect first = {0, 0, 1, 1};
	const struct st_console_rect row = {0, 0, 2, 1};
	struct st_console_cell cell = {0};
	struct st_console_cell cells[2];
	st_console_t *console = NULL;
	uint16_t identifier;
	uint16_t glyph_font;
	uint32_t glyph;
	uint8_t columns;

	CHECK(st_console_create(&console, &config_display, &config) == 0);
	CHECK(st_console_font_lookup(console, 0, UINT32_C(0x4e00), &glyph_font,
	    &glyph, &columns) == IO_ERROR_NOT_FOUND);
	CHECK(read_cell(console, 0, 0).glyph == ' ');
	identifier = register_static_font(console, &font);
	CHECK(identifier == 0);
	CHECK(read_cell(console, 0, 0).glyph == ' ');
	CHECK((read_cell(console, 1, 0).flags
	    & ST_CONSOLE_CELL_CONTINUATION) == 0);
	CHECK(st_console_set_position(console, 0, 0) == 0);
	CHECK(st_console_put_codepoint(console, UINT32_C(0x4e00)) == 0);
	CHECK(st_console_read_cells(console, &row, cells, 2) == 0);
	CHECK(cells[0].glyph == 2);
	CHECK(cells[0].glyph_font == 0);
	CHECK((cells[1].flags
	    & ST_CONSOLE_CELL_CONTINUATION) != 0);

	cell.glyph = 1;
	cell.font = 1;
	cell.glyph_font = 0;
	CHECK(st_console_write_cells(console, &first, &cell, 1) == 0);
	CHECK(read_cell(console, 0, 0).font == 1);
	CHECK(read_cell(console, 0, 0).glyph == 1);
	CHECK(read_cell(console, 0, 0).glyph_font == 0);
	identifier = register_static_font(console, &selected_font);
	CHECK(identifier == 1);
	CHECK(read_cell(console, 0, 0).font == 1);
	CHECK(read_cell(console, 0, 0).glyph == 1);
	CHECK(read_cell(console, 0, 0).glyph_font == 0);
	cell.glyph = 0;
	cell.glyph_font = 1;
	CHECK(st_console_write_cells(console, &first, &cell, 1) == 0);
	CHECK(read_cell(console, 0, 0).glyph == 0);
	CHECK(read_cell(console, 0, 0).glyph_font == 1);
	st_console_destroy(console);
}

static void
test_font_registry_lru_replacement(void)
{
	static const uint8_t one_bitmap[1] = {0};
	static const uint8_t two_bitmap[2] = {0, 0xff};
	static const struct st_console_font_mapping one_mapping[] = {{0, 0}};
	static const struct st_console_font_mapping two_mapping[] = {
		{0, 0},
		{1, 1},
	};
	static const struct st_console_font one_font = {
		.bitmap = one_bitmap,
		.bitmap_size = sizeof(one_bitmap),
		.width = 8,
		.height = 1,
		.glyph_count = 1,
		.row_stride = 1,
		.glyph_stride = 1,
		.mapping = one_mapping,
		.mapping_count = sizeof(one_mapping) / sizeof(one_mapping[0]),
	};
	static const struct st_console_font two_font = {
		.bitmap = two_bitmap,
		.bitmap_size = sizeof(two_bitmap),
		.width = 8,
		.height = 1,
		.glyph_count = 2,
		.row_stride = 1,
		.glyph_stride = 1,
		.mapping = two_mapping,
		.mapping_count = sizeof(two_mapping) / sizeof(two_mapping[0]),
	};
	st_console_t *console = create_headless(1, 1);
	uint16_t identifier = 0;
	uint16_t glyph_font;
	uint32_t glyph;
	uint8_t columns;

	for (uint32_t i = 0; i <= UINT16_MAX; ++i) {
		CHECK(st_console_register_static_font(console, &one_font,
		    &identifier) == 0);
		CHECK(identifier == (uint16_t)i);
	}
	CHECK(st_console_font_lookup(console, 0, 0, &glyph_font, &glyph,
	    &columns) == 0);
	CHECK(st_console_register_static_font(console, &two_font,
	    &identifier) == 0);
	CHECK(identifier == 1);
	CHECK(st_console_font_lookup(console, identifier, 1, &glyph_font,
	    &glyph, &columns) == 0);
	CHECK(glyph_font == identifier && glyph == 1 && columns == 1);
	st_console_destroy(console);
}

struct multi_display_state {
	st_console_t *bound;
	st_console_t *presented;
	unsigned int presents;
	unsigned int resizes;
	unsigned int palettes;
	uint16_t palette_first;
	uint32_t palette_count;
	const uint32_t *palette_colours;
	unsigned int titles;
	unsigned int pointers;
	bool pointer_visible;
	unsigned int pointer_shapes;
	enum st_console_pointer_shape pointer_shape;
	char title[16];
	uint32_t columns;
	uint32_t rows;
	st_console_t *fail_console;
	unsigned int fail_stage;
	unsigned int failures_left;
	bool inject_input;
};

static int
multi_fail(struct multi_display_state *state, st_console_t *console,
	unsigned int stage)
{
	if (state->failures_left != 0 && state->fail_stage == stage
	    && (state->fail_console == NULL || state->fail_console == console)) {
		--state->failures_left;
		return IO_ERROR_IO;
	}
	return 0;
}

static int
multi_activate(st_console_t *console)
{
	struct multi_display_state *state = console->display->backend_state;
	int status = multi_fail(state, console, 6);

	if (status == 0)
		state->bound = console;
	return status;
}

static void
multi_destroy(st_console_display_t *display)
{
	struct multi_display_state *state = display->backend_state;

	/* A backend's last retained console remains valid until shutdown joins. */
	if (state->bound != NULL)
		CHECK(state->bound->rows != 0);
	state->bound = NULL;
}

static int
multi_present(st_console_t *console)
{
	struct multi_display_state *state = console->display->backend_state;

	state->presented = console;
	++state->presents;
	CHECK(state->columns == console->columns && state->rows == console->rows);
	return multi_fail(state, console, 5);
}

static int
multi_resize(st_console_t *console, uint32_t columns, uint32_t rows)
{
	struct multi_display_state *state = console->display->backend_state;

	state->columns = columns;
	state->rows = rows;
	++state->resizes;
	if (state->inject_input) {
		const struct st_console_event input = {
			.type = ST_CONSOLE_EVENT_KEY_CODE, .data.key.code = '?',
		};

		state->inject_input = false;
		CHECK(st_console_display_post_event(console->display, &input) == 0);
	}
	return multi_fail(state, console, 1);
}

static void
multi_palette(st_console_t *console, uint16_t first,
	const uint32_t *colours, uint32_t count)
{
	struct multi_display_state *state = console->display->backend_state;

	CHECK(atomic_load(&console->display->active) == console);
	CHECK(count != 0 && count <= UINT16_MAX + UINT32_C(1) - first);
	CHECK(colours == &console->palette[first]);
	state->palette_first = first;
	state->palette_count = count;
	state->palette_colours = colours;
	++state->palettes;
}

static int
multi_title(st_console_display_t *display, const char *title)
{
	struct multi_display_state *state = display->backend_state;

	const char *expected = atomic_load(&display->active)->title;
	CHECK(strcmp(expected == NULL ? "SyncTERM" : expected, title) == 0);
	++state->titles;
	(void)snprintf(state->title, sizeof(state->title), "%s", title);
	return multi_fail(state, atomic_load(&display->active), 3);
}

static int
multi_pointer(st_console_display_t *display, bool visible)
{
	struct multi_display_state *state = display->backend_state;

	CHECK(atomic_load(&display->active)->pointer_visible == visible);
	++state->pointers;
	state->pointer_visible = visible;
	return multi_fail(state, atomic_load(&display->active), 4);
}

static int
multi_pointer_shape(st_console_display_t *display,
	enum st_console_pointer_shape shape)
{
	struct multi_display_state *state = display->backend_state;

	CHECK(atomic_load(&display->active)->pointer_shape == shape);
	++state->pointer_shapes;
	state->pointer_shape = shape;
	return 0;
}

static const struct st_console_backend_ops multi_ops = {
	.deferred_present = true,
	.activate = multi_activate,
	.destroy = multi_destroy,
	.present = multi_present,
	.resize = multi_resize,
	.palette_changed = multi_palette,
	.set_title = multi_title,
	.set_pointer_visible = multi_pointer,
	.set_pointer_shape = multi_pointer_shape,
};

static int
multi_metadata_writer(void *argument)
{
	st_console_t *console = argument;
	st_console_snapshot_t *snapshot = NULL;

	CHECK(st_console_snapshot_create(&snapshot, console) == 0);
	for (unsigned int i = 0; i < 1000; ++i) {
		CHECK(st_console_set_title(console, "Racing") == 0);
		CHECK(st_console_set_pointer_visible(console, (i & 1) != 0) == 0);
		CHECK(st_console_snapshot_restore(console, snapshot) == 0);
	}
	st_console_snapshot_destroy(snapshot);
	return 0;
}

static int
multi_destroy_console(void *argument)
{
	st_console_destroy(argument);
	return 0;
}

static int
multi_activate_loop(void *argument)
{
	st_console_t *console = argument;

	for (unsigned int i = 0; i < 1000; ++i)
		CHECK(st_console_display_activate(console->display, console) == 0);
	return 0;
}

static void
test_multiple_consoles_one_display(void)
{
	const struct st_console_display_config display_config = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
		.text_presentation = ST_CONSOLE_TEXT_UNICODE,
	};
	const struct st_console_config first_config = {
		.columns = 4, .rows = 2,
		.wrap = true,
	};
	const struct st_console_config second_config = {
		.columns = 7, .rows = 3,
		.wrap = true,
	};
	const struct st_console_rect cell_rect = {0, 0, 1, 1};
	const struct st_console_cell cell = {.glyph = 'B'};
	const struct st_console_event first_event = {
		.type = ST_CONSOLE_EVENT_KEY_CODE, .data.key.code = 'A',
	};
	const struct st_console_event second_event = {
		.type = ST_CONSOLE_EVENT_KEY_CODE, .data.key.code = 'B',
	};
	struct st_console_event event;
	static const uint8_t bitmap[1] = {0};
	static const struct st_console_font_mapping mapping[1] = {{' ', 0}};
	static const struct st_console_font font = {
		.bitmap = bitmap, .bitmap_size = sizeof(bitmap),
		.width = 8, .height = 1, .glyph_count = 1,
		.row_stride = 1, .glyph_stride = 1,
		.mapping = mapping, .mapping_count = 1,
	};
	struct multi_display_state state = {0};
	uint16_t identifier;
	bool registered;
	st_console_display_t *display = NULL;
	st_console_t *first = NULL;
	st_console_t *second = NULL;
	st_console_t *active = NULL;

	CHECK(st_console_display_create(&display, &display_config) == 0);
	CHECK(st_console_create_on_display(&first, display, &first_config) == 0);
	CHECK(st_console_create_on_display(&second, display, &second_config) == 0);
	CHECK(st_console_register_static_font(first, &font, &identifier) == 0);
	CHECK(identifier == 0);
	CHECK(st_console_font_registered(second, identifier, &registered) == 0);
	CHECK(!registered);
	display->backend_ops = &multi_ops;
	display->backend_state = &state;
	CHECK(st_console_write_cells(second, &cell_rect, &cell, 1) == 0);
	CHECK(st_console_set_title(second, "Second") == 0);
	CHECK(st_console_set_pointer_visible(second, false) == 0);
	CHECK(st_console_set_pointer_shape(second, ST_CONSOLE_POINTER_LINK) == 0);
	CHECK(st_console_present(second) == 0);
	CHECK(state.presents == 0 && state.resizes == 0 && state.palettes == 0
	    && state.titles == 0 && state.pointers == 0
	    && state.pointer_shapes == 0);
	CHECK(st_console_post_event(second, &second_event) == 0);
	CHECK(st_console_event_try(first, &event) == IO_ERROR_WOULD_BLOCK);
	CHECK(st_console_event_try(second, &event) == 0);
	CHECK(event.data.key.code == 'B');
	CHECK(st_console_display_post_event(display, &first_event) == 0);
	CHECK(st_console_display_activate(display, second) == 0);
	CHECK(state.presented == second && state.presents == 1);
	CHECK(state.resizes == 1 && state.palettes == 1);
	CHECK(state.palette_first == 0
	    && state.palette_count == UINT16_MAX + UINT32_C(1)
	    && state.palette_colours == second->palette);
	CHECK(state.titles == 1 && strcmp(state.title, "Second") == 0);
	CHECK(state.pointers == 1 && !state.pointer_visible);
	CHECK(state.pointer_shapes == 1
	    && state.pointer_shape == ST_CONSOLE_POINTER_LINK);
	{
		const uint32_t colours[3] = {1, 0, 2};
		unsigned int before = state.palettes;

		CHECK(st_console_begin_update(second) == 0);
		CHECK(st_console_set_palette(second, 60000, colours, 3) == 0);
		CHECK(state.palettes == before + 1);
		CHECK(state.palette_first == 60000 && state.palette_count == 3);
		CHECK(state.palette_colours == &second->palette[60000]);
		CHECK(st_console_set_palette(second, 60000, colours, 3) == 0);
		CHECK(state.palettes == before + 1);
	}
	CHECK(st_console_display_post_event(display, &second_event) == 0);
	CHECK(st_console_event_try(first, &event) == 0);
	CHECK(event.data.key.code == 'A');
	CHECK(st_console_event_try(second, &event) == 0);
	CHECK(event.data.key.code == 'B');
	CHECK(st_console_event_try(first, &event) == IO_ERROR_WOULD_BLOCK);
	CHECK(st_console_event_try(second, &event) == IO_ERROR_WOULD_BLOCK);
	{
		unsigned int before = state.presents;

		CHECK(st_console_display_activate(display, first) == 0);
		CHECK(state.presented == first && state.presents == before + 1);
	}
	CHECK(st_console_end_update(second) == 0);
	{
		st_thread_t switchers[2];
		unsigned int before = state.presents;

		CHECK(st_thread_create(&switchers[0], multi_activate_loop, first)
		    == ST_THREAD_SUCCESS);
		CHECK(st_thread_create(&switchers[1], multi_activate_loop, second)
		    == ST_THREAD_SUCCESS);
		CHECK(st_thread_join(switchers[0], NULL) == ST_THREAD_SUCCESS);
		CHECK(st_thread_join(switchers[1], NULL) == ST_THREAD_SUCCESS);
		/* Each successful switch includes its own completed presentation. */
		CHECK(state.presents == before + 2000);
	}
	{
		st_thread_t writer;

		CHECK(st_thread_create(&writer, multi_metadata_writer, first)
		    == ST_THREAD_SUCCESS);
		for (unsigned int i = 0; i < 1000; ++i) {
			CHECK(st_console_display_activate(display, second) == 0);
			CHECK(st_console_display_activate(display, first) == 0);
		}
		CHECK(st_thread_join(writer, NULL) == ST_THREAD_SUCCESS);
	}
	static const unsigned int failing_stages[] = {1, 3, 4, 5};
	for (size_t failure = 0; failure < sizeof(failing_stages)
	    / sizeof(failing_stages[0]); ++failure) {
		unsigned int stage = failing_stages[failure];
		state.fail_console = second;
		state.fail_stage = stage;
		state.failures_left = 1;
		state.inject_input = true;
		CHECK(st_console_display_activate(display, second) == IO_ERROR_IO);
		CHECK(st_console_event_try(first, &event) == 0);
		CHECK(event.data.key.code == '?');
		CHECK(st_console_event_try(second, &event) == IO_ERROR_WOULD_BLOCK);
		CHECK(st_console_display_active(display, &active) == 0);
		CHECK(active == first && !display->backend_failed);
		CHECK(state.presented == first);
		CHECK(state.columns == first_config.columns);
		CHECK(state.rows == first_config.rows);
		CHECK(st_console_present(first) == 0);
	}
	state.fail_console = NULL;
	state.fail_stage = 1;
	state.failures_left = 2;
	CHECK(st_console_display_activate(display, second) == IO_ERROR_IO);
	CHECK(display->backend_failed);
	{
		unsigned int before = state.presents;

		CHECK(st_console_present(first) == IO_ERROR_IO);
		CHECK(state.presents == before);
	}
	CHECK(st_console_display_activate(display, first) == 0);
	CHECK(!display->backend_failed);
	{
		st_thread_t destroyer;
		st_thread_t presenter;
		const struct timespec pause = {.tv_nsec = 1000000};
		bool pinned = false;

		CHECK(st_rwlock_write_lock(&first->state_lock) == ST_THREAD_SUCCESS);
		CHECK(st_console_start_presentation_worker(first) == 0);
		presenter = display->presentation_thread;
		for (unsigned int i = 0; i < 5000 && !pinned; ++i) {
			CHECK(st_mutex_lock(&display->presentation_mutex)
			    == ST_THREAD_SUCCESS);
			pinned = display->presentation_active == first;
			CHECK(st_mutex_unlock(&display->presentation_mutex)
			    == ST_THREAD_SUCCESS);
			if (!pinned)
				(void)st_thread_sleep(&pause, NULL);
		}
		CHECK(pinned);
		CHECK(st_thread_create(&destroyer, multi_destroy_console, first)
		    == ST_THREAD_SUCCESS);
		CHECK(st_rwlock_unlock(&first->state_lock) == ST_THREAD_SUCCESS);
		CHECK(st_thread_join(destroyer, NULL) == ST_THREAD_SUCCESS);
		CHECK(st_thread_equal(presenter, display->presentation_thread));
	}
	CHECK(st_console_display_active(display, &active) == 0);
	CHECK(active == second);
	CHECK(display->presentation_thread_started);
	st_console_destroy(second);
	st_console_display_destroy(display);
}

static void
test_failed_detachment_lifetime(void)
{
	const struct st_console_display_config output = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config config = {.columns = 4, .rows = 2};

	for (unsigned int recover = 0; recover < 2; ++recover) {
		st_console_display_t *display = NULL;
		st_console_t *first = NULL;
		st_console_t *second = NULL;
		st_console_t *selected = NULL;
		struct multi_display_state state = {0};

		CHECK(st_console_display_create(&display, &output) == 0);
		CHECK(st_console_create_on_display(&first, display, &config) == 0);
		CHECK(st_console_create_on_display(&second, display, &config) == 0);
		display->backend_ops = &multi_ops;
		display->backend_state = &state;
		CHECK(st_console_display_activate(display, second) == 0);
		CHECK(state.bound == second);
		state.fail_stage = 6;
		state.failures_left = 3;
		st_console_destroy(second);
		CHECK(display->console_count == 1);
		CHECK(st_console_display_active(display, &selected) == 0);
		CHECK(selected == first && display->backend_failed);
		CHECK(display->retired_consoles == state.bound);
		CHECK(state.bound->rows == config.rows);
		if (recover) {
			CHECK(st_console_display_activate(display, first) == 0);
			CHECK(display->retired_consoles == NULL);
			CHECK(state.bound == first);
		}
		st_console_destroy(first);
		CHECK(state.bound == NULL);
		CHECK(display->retired_consoles == NULL);
		st_console_display_destroy(display);
	}
}

static void
test_display_copies_ansi_options(void)
{
	struct output output = {0};
	struct st_console_ansi_io io = {.write = write_output, .context = &output};
	const struct st_console_display_config config = {
		.backend = ST_CONSOLE_BACKEND_ANSI, .ansi_io = &io,
	};
	const struct st_console_config logical = {.columns = 2, .rows = 1};
	st_console_display_t *display = NULL;
	st_console_t *console = NULL;

	CHECK(st_console_display_create(&display, &config) == 0);
	io.write = NULL;
	for (unsigned int i = 0; i < 2; ++i) {
		CHECK(st_console_create_on_display(&console, display, &logical) == 0);
		CHECK(st_console_write_utf8(console, "x", 1) == 0);
		CHECK(st_console_present(console) == 0);
		st_console_destroy(console);
	}
	st_console_display_destroy(display);
}

static void
test_default_palette_contract(void)
{
	uint32_t palette[4];

	CHECK(st_console_get_default_palette(0, palette, 2) == 0);
	CHECK(palette[0] == 0x000000 && palette[1] == 0xa80000);
	CHECK(st_console_get_default_palette(256, palette, 4) == 0);
	CHECK(palette[0] == 0x000000 && palette[1] == 0xffffff
	    && palette[2] == 0x813338 && palette[3] == 0x75cec8);
	CHECK(st_console_get_default_palette(UINT16_MAX, palette, 2)
	    == IO_ERROR_INVALID_ARGUMENT);
}

int
main(void)
{
	test_control_keys();
	test_build_features();
	test_default_cursor_geometry();
	test_video_flags();
	test_cell_and_regions();
	test_text_window_and_scroll();
	test_move_and_line_operations();
	test_invalid_scalar_mutations();
	test_snapshot_drains();
	test_snapshot_state();
	test_event_order();
	test_resize_palette_and_errors();
	test_pixels_and_font_rendering();
	test_fast_font_attributes();
	test_raster_margin_and_scroll();
	test_partial_full_width_raster_scroll();
	test_saa5050_double_height_raster();
	test_concurrent_writers();
	test_deferred_presentation();
	test_unicode_wide_cells();
	test_wide_window_row_operations();
	test_wide_window_cached_scroll();
	test_ansi_wide_cell_output();
	test_ansi_reveal();
	test_ansi_output_modes();
	test_ansi_cp437_output();
	test_ascii_character_identity();
	test_font_resolution_and_complete_cells();
	test_cluster_font_mapping();
	test_circular_row_moves();
	test_pixel_row_rotation_preserves_outside();
	test_zero_width_text();
	test_font_registration_preserves_cells();
	test_font_registry_lru_replacement();
	test_multiple_consoles_one_display();
	test_failed_detachment_lifetime();
	test_display_copies_ansi_options();
	test_default_palette_contract();
	test_ncurses_when_available();
	return EXIT_SUCCESS;
}

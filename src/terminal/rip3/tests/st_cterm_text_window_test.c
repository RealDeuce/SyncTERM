/* Literal ANSI continuations and tab positions, independent of stored fields. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdio.h>
#include <string.h>

#include "cterm.h"
#include "io_error.h"
#include "st_cterm.h"
#include "st_rip3_native_window.h"

#define CHECK(x)                                                        \
	do {                                                            \
		if (!(x)) {                                             \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x); \
			return 1;                                       \
		}                                                       \
	} while (0)

extern bool   st_cterm_window_test_tracking;
extern size_t st_cterm_window_test_attempts, st_cterm_window_test_live, st_cterm_window_test_fail_at;

struct fixture {
	st_console_t      *console;
	st_font_catalog_t *fonts;
	st_cterm_t        *text;
};

static int
create(struct fixture *f, unsigned columns)
{
	const struct st_console_display_config display  = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config         geometry = {.columns = columns,
	    .rows                                                   = 5,
	    .cell_width                                             = 8,
	    .cell_height                                            = 8};
	CHECK(st_console_create(&f->console, &display, &geometry) == 0
	      && st_font_catalog_create(&f->fonts, f->console) == 0);
	const struct st_cterm_config config = {.console = f->console,
	    .fonts                                      = f->fonts,
	    .emulation                                  = ST_CTERM_ANSI_BBS,
	    .encoding                                   = ST_CTERM_ENCODING_CODEPAGE,
	    .codepage                                   = ST_CODEPAGE_CP437,
	    .quiet                                      = true,
	    .graphics_mode                              = ST_CTERM_GRAPHICS_RIP_3,
	    .windowed_ansi                              = true};
	CHECK(st_cterm_create(&f->text, &config) == 0 && st_cterm_start_text(f->text) == 0);
	return 0;
}

static void
destroy(struct fixture *f)
{
	st_cterm_destroy(&f->text);
	st_font_catalog_destroy(f->fonts);
	st_console_destroy(f->console);
}

static int
feed(struct fixture *f, const char *text, size_t length)
{
	size_t used = 0;
	CHECK(st_cterm_write(f->text, text, length, &used) == 0 && used == length);
	return 0;
}

static int
position(struct fixture *f, int x, int y)
{
	struct st_cterm_state state;
	CHECK(st_cterm_state(f->text, &state) == 0 && state.x == x && state.y == y);
	return 0;
}

static int
continuations(void)
{
	static const char sequence[] = "\x1b[38;2;12;34;56m";
	for (size_t split = 0; split < sizeof(sequence) - 1; split++) {
		struct fixture source = {0}, next = {0};
		CHECK(create(&source, 17) == 0 && create(&next, 23) == 0);
		CHECK(feed(&source, sequence, split) == 0);
		CHECK(st_cterm_inherit_window_interpreter(next.text, source.text, true) == 0);
		destroy(&source); /* No inherited pointer may still alias this object. */
		CHECK(
		    feed(&next, sequence + split, sizeof(sequence) - 1 - split) == 0 && feed(&next, "F", 1) == 0);
		struct st_cterm_state state;
		CHECK(st_cterm_state(next.text, &state) == 0 && state.fg_color == 0x0c2238 && state.attr == 7);
		CHECK(position(&next, 2, 1) == 0);
		destroy(&next);
	}
	return 0;
}

static int
scene_input_continuation(void)
{
	struct fixture caller = {0}, replacement = {0};
	CHECK(create(&caller, 17) == 0 && create(&replacement, 23) == 0);
	static const char prefix[] = "\x1b]0;caller";
	CHECK(feed(&caller, prefix, sizeof(prefix) - 1) == 0);
	struct st_cterm_suspended_input *saved = NULL;
	CHECK(st_cterm_suspend_input(caller.text, &saved) == 0 && saved != NULL);
	CHECK(caller.text->accumulator == NULL && caller.text->strbuf == NULL);
	destroy(&caller);

	static const char child[] = "\x1b[31m\x1bPq~";
	CHECK(feed(&replacement, child, sizeof(child) - 1) == 0);
	CHECK(replacement.text->sixel == SIXEL_STARTED);
	st_cterm_resume_input(replacement.text, saved);
	CHECK(replacement.text->sixel == SIXEL_INACTIVE && replacement.text->sx_pixels == NULL);
	CHECK(replacement.text->string == CTERM_STRING_OSC && replacement.text->strbuflen == 8);
	CHECK(memcmp(replacement.text->strbuf, "0;caller", 8) == 0);
	CHECK(feed(&replacement, "\x1b\\Z", 3) == 0 && position(&replacement, 2, 1) == 0);
	struct st_cterm_state state;
	CHECK(st_cterm_state(replacement.text, &state) == 0 && state.attr == 4);
	destroy(&replacement);
	return 0;
}

static int
tabs_and_saved_cursor(void)
{
	struct fixture source = {0}, small = {0}, large = {0};
	CHECK(create(&source, 17) == 0 && create(&small, 4) == 0 && create(&large, 23) == 0);
	const char setup[] = "\x1b[3g\x1b[1;6H\x1bH\x1b[3;3H\x1b[s\x1b[31;44m";
	CHECK(feed(&source, setup, sizeof(setup) - 1) == 0);
	CHECK(st_cterm_inherit_window_interpreter(small.text, source.text, true) == 0);
	CHECK(st_cterm_inherit_window_interpreter(large.text, small.text, true) == 0);
	destroy(&source);
	destroy(&small);
	CHECK(feed(&large, "\t", 1) == 0 && position(&large, 6, 1) == 0);
	const char restore[] = "\x1b[2;2H\x1b[u";
	CHECK(feed(&large, restore, sizeof(restore) - 1) == 0 && position(&large, 1, 1) == 0);
	struct st_cterm_state state;
	CHECK(st_cterm_state(large.text, &state) == 0 && state.attr == 7 && state.top_margin == 1
	      && state.bottom_margin == 5);
	destroy(&large);
	/* Unmodified default stops extend when the new window grows. */
	CHECK(create(&source, 4) == 0 && create(&large, 23) == 0);
	CHECK(st_cterm_inherit_window_interpreter(large.text, source.text, true) == 0);
	destroy(&source);
	CHECK(feed(&large, "\t\t", 2) == 0 && position(&large, 17, 1) == 0);
	destroy(&large);
	return 0;
}

static int
failures(void)
{
	struct fixture source = {0}, next = {0};
	CHECK(create(&source, 17) == 0 && create(&next, 23) == 0);
	const char setup[] = "\x1b[3g\x1b[1;6H\x1bH\x1b[3;1H\x1bJ\x1b[H\x1b]0;partial";
	CHECK(feed(&source, setup, sizeof(setup) - 1) == 0 && source.text->tab_count == 1
	      && source.text->vtab_count == 1);
	int  *old_tabs                = next.text->tabs;
	char *old_string              = next.text->strbuf;
	st_cterm_window_test_tracking = true;
	for (size_t i = 0; i < 3; i++) {
		st_cterm_window_test_attempts = 0;
		st_cterm_window_test_fail_at  = i;
		CHECK(st_cterm_inherit_window_interpreter(next.text, source.text, true) == IO_ERROR_OUT_OF_MEMORY
		      && st_cterm_window_test_live == 0 && next.text->tabs == old_tabs
		      && next.text->strbuf == old_string && next.text->string == 0 && next.text->save_xpos == 0);
	}
	st_cterm_window_test_tracking = false;
	st_cterm_window_test_fail_at  = SIZE_MAX;
	CHECK(st_cterm_inherit_window_interpreter(next.text, source.text, true) == 0
	      && next.text->strbuf != source.text->strbuf && strcmp(next.text->strbuf, "0;partial") == 0);
	destroy(&source);
	CHECK(feed(&next, " title\x07", 7) == 0 && feed(&next, "\t", 1) == 0 && position(&next, 6, 1) == 0);
	/* ANSI-BBS renders the CP437 VT byte as a glyph; CSI Y addresses the
	 * inherited vertical tab stop without depending on that byte policy. */
	CHECK(feed(&next, "\x1b[Y", 3) == 0 && position(&next, 6, 3) == 0);
	CHECK(st_cterm_inherit_window_interpreter(next.text, next.text, true) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_cterm_inherit_window_interpreter(NULL, next.text, true) == IO_ERROR_INVALID_ARGUMENT);
	destroy(&next);
	CHECK(create(&source, 17) == 0 && create(&next, 23) == 0);
	old_tabs                   = next.text->tabs;
	source.text->graphics_mode = ST_CTERM_GRAPHICS_RIP_1;
	CHECK(st_cterm_inherit_window_interpreter(next.text, source.text, true) == IO_ERROR_NOT_SUPPORTED
	      && next.text->tabs == old_tabs);
	source.text->graphics_mode = ST_CTERM_GRAPHICS_RIP_3;
	const char dcs[]           = "\x1bPq";
	CHECK(feed(&source, dcs, sizeof(dcs) - 1) == 0);
	CHECK(st_cterm_inherit_window_interpreter(next.text, source.text, true) == IO_ERROR_NOT_SUPPORTED
	      && next.text->tabs == old_tabs);
	destroy(&source);
	destroy(&next);
	return 0;
}

int
extended_tabs(void)
{
	struct fixture source = {0}, next = {0};
	CHECK(create(&source, 17) == 0 && create(&next, 23) == 0);
	const char setup[] = "\x1b[3g\x1b[1;6H\x1bH\x1b[2;2H\x1b[3";
	CHECK(feed(&source, setup, sizeof(setup) - 1) == 0);
	int *fresh_tabs = next.text->tabs;
	CHECK(st_cterm_inherit_window_interpreter(next.text, source.text, false) == 0
	      && next.text->tabs == fresh_tabs);
	CHECK(feed(&next, "1mA", 3) == 0 && position(&next, 2, 1) == 0 && next.text->attr == 4);
	CHECK(feed(&next, "\t", 1) == 0 && position(&next, 9, 1) == 0);
	const char saved[] = "\x1b[u";
	CHECK(feed(&next, saved, sizeof(saved) - 1) == 0 && position(&next, 1, 1) == 0);
	CHECK(feed(&source, "2mB", 3) == 0 && position(&source, 3, 2) == 0 && source.text->attr == 2);
	destroy(&source);
	destroy(&next);
	return 0;
}

static int
window_copy(void)
{
	struct fixture source = {0}, next = {0};
	CHECK(create(&source, 17) == 0 && create(&next, 17) == 0);
	uint16_t copied = 123;
	CHECK(
	    st_console_copy_font(NULL, source.console, 0, &copied) == IO_ERROR_INVALID_ARGUMENT && copied == 123);
	CHECK(st_console_copy_font(next.console, source.console, UINT16_MAX, &copied) == IO_ERROR_NOT_FOUND
	      && copied == 123);
	CHECK(st_console_copy_font(source.console, source.console, 0, &copied) == 0 && copied == 0);
	st_cterm_destroy(&source.text);
	uint8_t bitmap[256 * 8] = {0};
	for (unsigned y = 0; y < 8; y++)
		bitmap['A' * 8 + y] = (uint8_t)(0x80u >> y);
	const struct st_console_font_mapping mapping[] = {
	    {'A', 'A'}
        };
	const struct st_console_font font = {.bitmap = bitmap,
	    .bitmap_size                             = sizeof(bitmap),
	    .width                                   = 8,
	    .height                                  = 8,
	    .glyph_count                             = 256,
	    .row_stride                              = 1,
	    .glyph_stride                            = 8,
	    .mapping                                 = mapping,
	    .mapping_count                           = 1};
	CHECK(st_font_catalog_load_bitmap(source.fonts, 46, &font, ST_CODEPAGE_CP437) == 0);
	const struct st_cterm_config config = {.console = source.console,
	    .fonts                                      = source.fonts,
	    .emulation                                  = ST_CTERM_ANSI_BBS,
	    .encoding                                   = ST_CTERM_ENCODING_CODEPAGE,
	    .codepage                                   = ST_CODEPAGE_CP437,
	    .quiet                                      = true,
	    .font_selector                              = 46,
	    .graphics_mode                              = ST_CTERM_GRAPHICS_RIP_3,
	    .windowed_ansi                              = true};
	CHECK(st_cterm_create(&source.text, &config) == 0 && st_cterm_start_text(source.text) == 0);
	const char setup[] =
	    "\x1b[3g\x1b[1;6H\x1bH\x1b[2;4r\x1b[?6h\x1b[2;3H\x1b[s\x1b[38;2;18;52;86;48;2;0;0;0m\x1b[?25l";
	CHECK(feed(&source, setup, sizeof(setup) - 1) == 0);
	CHECK(st_cterm_copy_window_state(next.text, source.text) == 0 && position(&next, 3, 3) == 0);
	struct st_cterm_state state;
	CHECK(st_cterm_state(next.text, &state) == 0 && state.fg_color == 0x123456 && state.bg_color == 0
	      && state.ext_attr.origin_mode && state.top_margin == 2 && state.bottom_margin == 4);
	CHECK(next.text->tabs != source.text->tabs && next.text->fg_tc_str != source.text->fg_tc_str);
	destroy(&source);
	memset(bitmap, 0, sizeof(bitmap));
	CHECK(st_cterm_position_text(next.text, 0, 0) == 0 && feed(&next, "A", 1) == 0
	      && st_cterm_text_boundary(next.text) == 0);
	struct st_pixel_buffer      *pixels = NULL;
	const struct st_console_rect cell   = {0, 0, 8, 8};
	CHECK(st_console_get_pixels(&pixels, next.console, &cell, false) == 0);
	for (unsigned y = 0; y < 8; y++)
		for (unsigned x = 0; x < 8; x++) {
			if ((pixels->primary[y * 8 + x] & 0xffffff) != (x == y ? 0x123456 : 0))
				fprintf(stderr, "copied glyph %u,%u raw=%08x\n", x, y,
				    (unsigned)pixels->primary[y * 8 + x]);
			CHECK((pixels->primary[y * 8 + x] & 0xffffff) == (x == y ? 0x123456 : 0));
		}
	st_pixel_buffer_destroy(pixels);
	const char restore[] = "\x1b[u";
	CHECK(feed(&next, restore, sizeof(restore) - 1) == 0 && position(&next, 3, 3) == 0);
	CHECK(st_cterm_position_text(next.text, 0, 0) == 0 && feed(&next, "\t", 1) == 0
	      && position(&next, 6, 1) == 0);
	bool visible = true;
	CHECK(st_cterm_get_text_cursor_visible(next.text, &visible) == 0 && !visible);
	CHECK(create(&source, 17) == 0);
	CHECK(feed(&source, "\x1b[3", 3) == 0 && st_cterm_select_window_interpreter(next.text, source.text) == 0
	      && position(&next, 6, 1) == 0);
	CHECK(st_cterm_state(next.text, &state) == 0 && state.fg_color == 0x123456 && next.text->save_xpos == 3);
	CHECK(feed(&next, "1m", 2) == 0 && next.text->attr == 4 && position(&next, 6, 1) == 0);
	destroy(&source);
	destroy(&next);
	return 0;
}

static int
selection_continuations(void)
{
	static const char *const sequences[] = {"\x1b[31m", "\x1b]0;partial title\x07"};
	for (unsigned copying = 0; copying < 2; copying++)
		for (size_t n = 0; n < 2; n++) {
			const char *sequence = sequences[n];
			size_t      length   = strlen(sequence);
			for (size_t split = 1; split < length; split++) {
				struct fixture source = {0}, next = {0};
				CHECK(create(&source, 17) == 0 && create(&next, 17) == 0);
				const char setup[] = "\x1b[32m\x1b[3;4H";
				CHECK(feed(&source, setup, sizeof(setup) - 1) == 0
				      && feed(&next, setup, sizeof(setup) - 1) == 0);
				CHECK(feed(&source, sequence, split) == 0);
				CHECK((copying ? st_cterm_copy_window_state(next.text, source.text)
				               : st_cterm_select_window_interpreter(next.text, source.text))
				      == 0);
				destroy(&source);
				CHECK(feed(&next, sequence + split, length - split) == 0
				      && feed(&next, "F", 1) == 0 && position(&next, 5, 3) == 0);
				CHECK(next.text->attr == (n == 0 ? 4 : 2) && next.text->sequence == 0
				      && next.text->string == 0);
				destroy(&next);
			}
		}
	return 0;
}

static int
default_window(void)
{
	struct fixture source = {0}, next = {0};
	CHECK(create(&source, 17) == 0 && create(&next, 23) == 0);
	const char setup[] = "\x1b[3g\x1b[1;6H\x1bH\x1b[2;4r\x1b[?6h\x1b[2;3H\x1b[32;44m\x1b[3";
	CHECK(feed(&source, setup, sizeof(setup) - 1) == 0);
	CHECK(st_cterm_default_window_interpreter(next.text, source.text) == 0 && position(&next, 1, 1) == 0);
	CHECK(next.text->tab_count == 0 && next.text->vtab_count == 0 && !next.text->default_tabs
	      && next.text->save_xpos == 1 && next.text->save_ypos == 1 && next.text->attr == 7
	      && !(next.text->extattr & CTERM_EXTATTR_ORIGINMODE) && next.text->top_margin == 1
	      && next.text->bottom_margin == 5);
	destroy(&source);
	/* Existing ANSI-BBS HT wraps when no later explicit stop is present. */
	CHECK(feed(&next, "1m\t", 3) == 0 && next.text->attr == 4 && position(&next, 1, 2) == 0);
	destroy(&next);
	return 0;
}

static int
soft_ansi_reset(void)
{
	for (unsigned active = 0; active < 2; active++) {
		struct fixture f = {0};
		CHECK(create(&f, 17) == 0);
		const char setup[] =
		    "\x1b[3g\x1b[1;6H\x1bH\x1b[2;4r\x1b[?6h\x1b[?7l\x1b[?25l\x1b[2;3H\x1b[s\x1b[7;32;44m\x1b[3";
		CHECK(feed(&f, setup, sizeof(setup) - 1) == 0);
		CHECK(st_cterm_rip3_ansi_reset(f.text, active != 0) == 0
		      && position(&f, active ? 1 : 3, active ? 1 : 3) == 0);
		CHECK(f.text->attr == 7 && !f.text->negative && f.text->save_xpos == 1 && f.text->save_ypos == 1
		      && f.text->top_margin == 1 && f.text->bottom_margin == 5 && f.text->default_tabs
		      && (f.text->extattr & CTERM_EXTATTR_ORIGINMODE)
		      && !(f.text->extattr & CTERM_EXTATTR_AUTOWRAP));
		bool visible = true;
		CHECK(st_cterm_get_text_cursor_visible(f.text, &visible) == 0 && !visible);
		CHECK(feed(&f, "1m", 2) == 0 && f.text->attr == 4);
		CHECK(st_cterm_position_text(f.text, 0, 0) == 0 && feed(&f, "\t", 1) == 0
		      && position(&f, 9, 1) == 0);
		destroy(&f);
	}
	return 0;
}

static int
definition_backups(void)
{
	struct fixture source = {0}, target = {0};
	CHECK(create(&source, 17) == 0);
	const char setup[] =
	    "\x1b[3g\x1b[1;6H\x1bH\x1b[3;1H\x1bJ\x1b[2;4r\x1b[?7l\x1b[?25l\x1b[2;3H\x1b[s\x1b[38;2;12;34;56m\x1b[48;2;65;43;21m";
	CHECK(feed(&source, setup, sizeof(setup) - 1) == 0);
	CHECK(source.text->tab_count == 1 && source.text->vtab_count == 1 && source.text->fg_tc_str != NULL
	      && source.text->bg_tc_str != NULL);
	struct st_cterm_window_definition *saved = NULL;
	CHECK(st_cterm_window_definition_create(&saved, source.text) == 0);
	extern size_t st_cterm_definition_test_fonts;
	extern size_t st_cterm_definition_test_resources(const struct st_cterm_window_definition *);
	size_t        resources = st_cterm_definition_test_resources(saved);
	CHECK(resources != 0 && st_cterm_definition_test_fonts == resources);
	for (size_t fail = 0; fail < 5 + resources; fail++) {
		struct st_cterm_window_definition *failed = NULL;
		st_cterm_window_test_tracking             = true;
		st_cterm_window_test_attempts             = 0;
		st_cterm_window_test_fail_at              = fail;
		CHECK(st_cterm_window_definition_create(&failed, source.text) == IO_ERROR_OUT_OF_MEMORY
		      && failed == NULL);
		CHECK(st_cterm_window_test_live == 0 && st_cterm_definition_test_fonts == resources);
		st_cterm_window_test_tracking = false;
		st_cterm_window_test_fail_at  = SIZE_MAX;
	}
	for (size_t fail = 0; fail < 4 + resources; fail++) {
		CHECK(create(&target, 17) == 0);
		st_cterm_window_test_tracking = true;
		st_cterm_window_test_attempts = 0;
		st_cterm_window_test_fail_at  = fail;
		CHECK(st_cterm_window_definition_restore(target.text, saved) == IO_ERROR_OUT_OF_MEMORY);
		CHECK(st_cterm_window_test_live == 0 && st_cterm_definition_test_fonts == resources
		      && target.text->attr == 7);
		st_cterm_window_test_tracking = false;
		st_cterm_window_test_fail_at  = SIZE_MAX;
		destroy(&target);
	}
	destroy(&source); /* Saved resources and pointers must now be independent. */
	for (unsigned pass = 0; pass < 2; pass++) {
		CHECK(create(&target, 17) == 0);
		CHECK(feed(&target, "B\x1b[3", 4) == 0); /* Live parser differs from saved state. */
		CHECK(st_cterm_window_definition_restore(target.text, saved) == 0 && position(&target, 3, 2) == 0);
		CHECK(target.text->top_margin == 2 && target.text->bottom_margin == 4
		      && target.text->save_xpos == 3 && target.text->save_ypos == 2
		      && target.text->fg_color == UINT32_C(0x800c2238)
		      && target.text->bg_color == UINT32_C(0x80412b15)
		      && !(target.text->extattr & CTERM_EXTATTR_AUTOWRAP));
		bool visible = true;
		CHECK(st_cterm_get_text_cursor_visible(target.text, &visible) == 0 && !visible);
		CHECK(feed(&target, "1mF", 3) == 0
		      && target.text->attr == 4); /* Continue live CSI, never saved syntax. */
		CHECK(st_cterm_position_text(target.text, 0, 0) == 0 && feed(&target, "\t", 1) == 0
		      && position(&target, 6, 1) == 0);
		CHECK(feed(&target, "\x1b[u", 3) == 0 && position(&target, 3, 2) == 0);
		destroy(&target);
	}
	st_cterm_window_definition_destroy(saved);
	CHECK(st_cterm_definition_test_fonts == 0);
	return 0;
}

static int
character_sets(void)
{
	static const char finals[] = {'A', 'B', '0'};
	for (unsigned bank = 0; bank < 2; bank++)
		for (unsigned choice = 0; choice < 3; choice++)
			for (size_t split = 0; split <= 3; split++) {
				struct fixture source = {0}, next = {0};
				CHECK(create(&source, 17) == 0 && create(&next, 23) == 0);
				char sequence[] = {27, bank ? ')' : '(', finals[choice]};
				CHECK(feed(&source, sequence, split) == 0
				      && st_cterm_inherit_window_interpreter(next.text, source.text, true) == 0);
				destroy(&source);
				CHECK(feed(&next, sequence + split, 3 - split) == 0 && position(&next, 1, 1) == 0);
				CHECK(next.text->g0_designation == (bank ? 'B' : finals[choice])
				      && next.text->g1_designation == (bank ? finals[choice] : 'B'));
				CHECK(feed(&next, "\x1b(x\x1b)y", 6) == 0 && position(&next, 1, 1) == 0);
				CHECK(next.text->g0_designation == (bank ? 'B' : finals[choice])
				      && next.text->g1_designation == (bank ? finals[choice] : 'B'));
				destroy(&next);
			}
	struct fixture source = {0}, next = {0};
	CHECK(create(&source, 17) == 0 && create(&next, 17) == 0);
	const char vector[] =
	    "\x1b(A#\x0e"
	    "q\x0f#\xb3";
	CHECK(feed(&source, vector, sizeof(vector) - 1) == 0 && position(&source, 7, 1) == 0);
	CHECK(source.text->g0_designation == 'A' && source.text->g1_designation == 'B');
	const char *const            glyphs[] = {"#", "\xe2\x99\xab", "q", "\xe2\x98\xbc", "#", "\xe2\x94\x82"};
	struct st_console_cell       cells[6];
	const struct st_console_rect row = {0, 0, 6, 1};
	CHECK(st_console_read_cells(source.console, &row, cells, 6) == 0);
	for (size_t i = 0; i < 6; i++) {
		char   text[8];
		size_t required;
		CHECK(st_console_font_glyph_text(source.console, cells[i].glyph_font, cells[i].glyph, text,
			  sizeof(text), &required)
		      == 0);
		CHECK(required == strlen(glyphs[i]) && memcmp(text, glyphs[i], required) == 0);
	}
	/* CSI coordinates, ESC c and the preliminary outer reset preserve sets. */
	const char coordinates[] = "\x1b)0\x1b[s\x1b(B\x1b[u";
	CHECK(feed(&source, coordinates, sizeof(coordinates) - 1) == 0 && source.text->g0_designation == 'B'
	      && source.text->g1_designation == '0');
	CHECK(feed(&source,
		  "\x1b(A\x1b"
		  "c",
		  5)
	          == 0
	      && source.text->g0_designation == 'A' && source.text->g1_designation == '0');
	CHECK(st_cterm_rip3_ansi_reset(source.text, true) == 0 && source.text->g0_designation == 'A'
	      && source.text->g1_designation == '0');
	CHECK(st_cterm_select_window_interpreter(next.text, source.text) == 0 && next.text->g0_designation == 'B'
	      && next.text->g1_designation == 'B');
	CHECK(st_cterm_copy_window_state(next.text, source.text) == 0 && next.text->g0_designation == 'A'
	      && next.text->g1_designation == '0');
	struct st_cterm_window_definition *saved = NULL;
	CHECK(st_cterm_window_definition_create(&saved, source.text) == 0);
	destroy(&source);
	destroy(&next);
	CHECK(create(&next, 17) == 0);
	CHECK(st_cterm_window_definition_restore(next.text, saved) == 0 && next.text->g0_designation == 'A'
	      && next.text->g1_designation == '0');
	st_cterm_window_definition_destroy(saved);
	destroy(&next);
	CHECK(create(&source, 17) == 0 && create(&next, 17) == 0);
	CHECK(feed(&source, "\x1b(A\x1b)0", 6) == 0
	      && st_cterm_default_window_interpreter(next.text, source.text) == 0);
	CHECK(next.text->g0_designation == 'B' && next.text->g1_designation == 'B');
	destroy(&source);
	destroy(&next);
	/* These declarations do not add charset interpretation to other routes. */
	for (unsigned mode = 0; mode < 2; mode++) {
		CHECK(create(&source, 17) == 0);
		source.text->windowed_ansi      = false;
		source.text->graphics_mode      = mode ? ST_CTERM_GRAPHICS_RIP_1 : ST_CTERM_GRAPHICS_OFF;
		CHECK(feed(&source, "\x1b(A\x1b)0", 6) == 0 && source.text->g0_designation == 'B'
		      && source.text->g1_designation == 'B');
		destroy(&source);
	}
	return 0;
}

static int
ansi_window_save(void)
{
	struct fixture source = {0}, target = {0};
	CHECK(create(&source, 17) == 0 && create(&target, 17) == 0);
	/* CSI's old save remains independent of the live cursor captured by ESC7. */
	const char setup[] =
	    "\x1b[1;1H\x1b[s\x1b[3g\x1b[1;6H\x1bH\x1b[3;1H\x1bJ\x1b[2;4H\x1b(A\x1b)0\x1b[31;44m\x1b[38;2;12;34;56m\x1b[48;2;65;43;21m";
	CHECK(feed(&source, setup, sizeof(setup) - 1) == 0);
	static const char save_sequence[] =
	    "\x1b"
	    "7";
	for (size_t split = 0; split <= 2; split++) {
		size_t used = 0;
		CHECK(st_cterm_write(source.text, save_sequence, split, &used) == 0 && used == split);
		CHECK(st_cterm_write(source.text, save_sequence + split, 3 - split, &used) == 0
		      && used == 2 - split);
		CHECK(st_cterm_take_window_action(source.text) == ST_CTERM_WINDOW_SAVE);
		CHECK(source.text->save_xpos == 1 && source.text->save_ypos == 1 && position(&source, 4, 2) == 0);
	}
	struct st_cterm_window_definition *saved = NULL;
	extern size_t st_cterm_definition_test_resources(const struct st_cterm_window_definition *);
	CHECK(st_cterm_window_ansi_create(&saved, source.text) == 0
	      && st_cterm_definition_test_resources(saved) == 0);
	for (size_t fail = 0; fail < 5; fail++) {
		struct st_cterm_window_definition *failed = saved;
		st_cterm_window_test_tracking             = true;
		st_cterm_window_test_attempts             = 0;
		st_cterm_window_test_fail_at              = fail;
		CHECK(st_cterm_window_ansi_create(&failed, source.text) == IO_ERROR_OUT_OF_MEMORY
		      && failed == saved);
		CHECK(st_cterm_window_test_live == 0);
		st_cterm_window_test_tracking = false;
		st_cterm_window_test_fail_at  = SIZE_MAX;
	}
	for (size_t fail = 0; fail < 9; fail++) {
		st_cterm_window_test_tracking = true;
		st_cterm_window_test_attempts = 0;
		st_cterm_window_test_fail_at  = fail;
		CHECK(st_cterm_window_ansi_restore(target.text, saved, target.text) == IO_ERROR_OUT_OF_MEMORY
		      && target.text->attr == 7);
		CHECK(st_cterm_window_test_live == 0);
		st_cterm_window_test_tracking = false;
		st_cterm_window_test_fail_at  = SIZE_MAX;
	}
	/* Font resources stay live: resolve the logical selector, or fail without
	 * reviving saved glyph resources. The snapshot itself survives the source. */
	destroy(&source);
	bool valid                        = target.text->screen.font_valid[0];
	target.text->screen.font_valid[0] = false;
	CHECK(st_cterm_window_ansi_restore(target.text, saved, target.text) == IO_ERROR_NOT_FOUND
	      && target.text->attr == 7);
	target.text->screen.font_valid[0] = valid;
	target.text->wire_encoding        = ST_CTERM_ENCODING_UTF8;
	CHECK(st_cterm_window_ansi_restore(target.text, saved, target.text) == 0 && position(&target, 4, 2) == 0);
	CHECK(target.text->attr == 0x14 && target.text->g0_designation == 'A' && target.text->g1_designation == '0'
	      && target.text->wire_encoding == ST_CTERM_ENCODING_UTF8
	      && target.text->fg_color == UINT32_C(0x800c2238) && target.text->bg_color == UINT32_C(0x80412b15));
	CHECK(feed(&target, "\x1b[H\x1b[u", 6) == 0 && position(&target, 4, 2) == 0);
	CHECK(feed(&target, "\x1b[H\t", 4) == 0 && position(&target, 6, 1) == 0);
	size_t used = 0;
	CHECK(st_cterm_write(target.text,
		  "\x1b"
		  "8Z",
		  3, &used)
	          == 0
	      && used == 2);
	CHECK(st_cterm_take_window_action(target.text) == ST_CTERM_WINDOW_RESTORE);
	CHECK(st_cterm_take_window_action(target.text) == ST_CTERM_WINDOW_NONE);
	st_cterm_window_definition_destroy(saved);
	static const char origin[] = "\x1b[2;4r\x1b[?6h\x1b[2;4H";
	CHECK(feed(&target, origin, sizeof(origin) - 1) == 0 && position(&target, 4, 3) == 0);
	CHECK(st_cterm_window_ansi_create(&saved, target.text) == 0 && create(&source, 17) == 0);
	CHECK(st_cterm_window_ansi_restore(source.text, saved, source.text) == 0 && position(&source, 4, 3) == 0);
	CHECK(feed(&source, "\x1b[H\x1b[u", 6) == 0 && position(&source, 4, 3) == 0);
	st_cterm_window_definition_destroy(saved);
	destroy(&source);
	destroy(&target);
	for (unsigned mode = 0; mode < 3; mode++) {
		CHECK(create(&source, 17) == 0);
		source.text->graphics_mode = mode == 0   ? ST_CTERM_GRAPHICS_OFF
		                             : mode == 1 ? ST_CTERM_GRAPHICS_RIP_1
		                                         : ST_CTERM_GRAPHICS_RIP_3;
		source.text->windowed_ansi = false;
		static const char legacy[] =
		    "\x1b[2;4H\x1b"
		    "7\x1b[H\x1b"
		    "8X";
		CHECK(feed(&source, legacy, sizeof(legacy) - 1) == 0 && position(&source, 5, 2) == 0);
		CHECK(st_cterm_take_window_action(source.text) == ST_CTERM_WINDOW_NONE);
		destroy(&source);
	}
	return 0;
}

static int
keyboard_action_mode(void)
{
	struct fixture source = {0}, clone = {0}, fresh = {0};
	CHECK(create(&source, 17) == 0 && create(&clone, 17) == 0 && create(&fresh, 17) == 0);
	static const char set[] = "\x1b[2h";
	for (size_t split = 0; split < sizeof(set) - 1; ++split) {
		CHECK(feed(&source, "\x1b[2l", 4) == 0);
		CHECK(feed(&source, set, split) == 0 && !st_cterm_rip3_keyboard_locked(source.text));
		CHECK(feed(&source, set + split, sizeof(set) - 1 - split) == 0);
		CHECK(st_cterm_rip3_keyboard_locked(source.text));
	}
	static const char ignored[] = "\x1b[4;2l\x1b[2:l\x1b[?2l\x1b[2:0l";
	CHECK(feed(&source, ignored, sizeof(ignored) - 1) == 0);
	CHECK(st_cterm_rip3_ansi_reset(source.text, true) == 0 && st_cterm_rip3_keyboard_locked(source.text));
	CHECK(st_cterm_copy_window_state(clone.text, source.text) == 0);
	CHECK(st_cterm_default_window_interpreter(fresh.text, source.text) == 0);
	CHECK(st_cterm_rip3_keyboard_locked(clone.text) && !st_cterm_rip3_keyboard_locked(fresh.text));
	CHECK(feed(&source,
		  "\x1b"
		  "c",
		  2)
	          == 0
	      && !st_cterm_rip3_keyboard_locked(source.text));
	CHECK(st_cterm_rip3_keyboard_locked(clone.text));

	/* Ordinary ANSI and legacy RIP keep their existing multi-mode handling. */
	for (unsigned mode = 0; mode < 2; ++mode) {
		fresh.text->windowed_ansi      = false;
		fresh.text->graphics_mode      = mode == 0 ? ST_CTERM_GRAPHICS_OFF : ST_CTERM_GRAPHICS_RIP_1;
		static const char compound[]   = "\x1b[2;14h";
		CHECK(feed(&fresh, "\x1b[14l", 5) == 0);
		CHECK(feed(&fresh, compound, sizeof(compound) - 1) == 0 && fresh.text->fetm == 1);
		CHECK(feed(&fresh, set, sizeof(set) - 1) == 0 && !st_cterm_rip3_keyboard_locked(fresh.text));
	}
	destroy(&fresh);
	destroy(&clone);
	destroy(&source);
	return 0;
}

int
main(void)
{
	CHECK(scene_input_continuation() == 0 && keyboard_action_mode() == 0);
	CHECK(ansi_window_save() == 0 && character_sets() == 0 && definition_backups() == 0 && continuations() == 0
	      && tabs_and_saved_cursor() == 0 && failures() == 0 && extended_tabs() == 0 && window_copy() == 0
	      && selection_continuations() == 0 && default_window() == 0 && soft_ansi_reset() == 0);
	return 0;
}

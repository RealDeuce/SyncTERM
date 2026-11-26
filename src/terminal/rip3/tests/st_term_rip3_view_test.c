/* Positioned native text, clipped drawing and circular parent raster mapping. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdio.h>
#include <string.h>

#include "console_internal.h"
#include "io_error.h"
#include "st_term_rip3_view.h"

#define CHECK(x)                                                        \
	do {                                                            \
		if (!(x)) {                                             \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x); \
			return 1;                                       \
		}                                                       \
	} while (0)

static int
write_text(st_cterm_t *text, const char *bytes)
{
	size_t used = 0, length = strlen(bytes);
	CHECK(st_cterm_write(text, bytes, length, &used) == 0 && used == length);
	return 0;
}

static int
check_flags(st_console_t *console, uint32_t flags, struct st_console_rect expected)
{
	CHECK(st_rwlock_read_lock(st_console_state_lock(console)) == ST_THREAD_SUCCESS);
	for (uint32_t y = 0; y < console->rows; y++)
		for (uint32_t x = 0; x < console->columns; x++) {
			bool dirty = x >= expected.x && y >= expected.y && x - expected.x < expected.width
			             && y - expected.y < expected.height;
			CHECK(((st_console_cell_at(console, (size_t)y * console->columns + x)->flags & flags) != 0)
			      == dirty);
		}
	CHECK(st_rwlock_unlock(st_console_state_lock(console)) == ST_THREAD_SUCCESS);
	return 0;
}

static int
shared_raster(void)
{
	st_console_t                          *parent  = NULL;
	st_font_catalog_t                     *fonts   = NULL;
	struct st_term_rip3_view              *view    = NULL;
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry = {.columns = 8, .rows = 6, .cell_width = 8, .cell_height = 8};
	CHECK(st_console_create(&parent, &display, &geometry) == 0 && st_font_catalog_create(&fonts, parent) == 0);
	const struct st_cterm_config config = {.console = parent,
	    .fonts                                      = fonts,
	    .emulation                                  = ST_CTERM_ANSI_BBS,
	    .encoding                                   = ST_CTERM_ENCODING_CODEPAGE,
	    .codepage                                   = ST_CODEPAGE_CP437,
	    .graphics_mode                              = ST_CTERM_GRAPHICS_RIP_3,
	    .windowed_ansi                              = true};
	/* Preserve the parent's row-ring optimization, then draw across the
	 * physical wrap point from a window whose cells have a different height. */
	CHECK(st_console_scroll(parent, 1, ' ') == 0);
	struct st_rip3_text_window_layout layout = {.x = 5,
	    .y                                         = 23,
	    .columns                                   = 2,
	    .rows                                      = 2,
	    .font                                      = 0,
	    .cell_width                                = 8,
	    .cell_height                               = 11,
	    .wrap                                      = true};
	CHECK(st_term_rip3_view_create(&view, &config, &layout, 640, 480) == 0
	      && st_term_rip3_view_begin(view, parent) == 0);
	st_console_t *child = st_term_rip3_view_console(view);
	CHECK(st_console_regrid_preserve_pixels(child, 2, 2, 8, 11) == IO_ERROR_BUSY);
	CHECK(st_console_resize(parent, 8, 6) == IO_ERROR_BUSY);
	CHECK(st_console_reconfigure(parent, &geometry) == IO_ERROR_BUSY);
	struct st_pixel_buffer *source = NULL, *before = NULL, *after = NULL;
	CHECK(st_pixel_buffer_create(&source, 16, 22, true) == 0);
	for (size_t i = 0; i < 16 * 22; i++) {
		source->primary[i]   = ST_PIXEL_COLOR_RGB_TAG | ((uint32_t)i + 1);
		source->alternate[i] = ST_PIXEL_COLOR_RGB_TAG | ((uint32_t)i + 0x10000);
	}
	const struct st_pixel_blit blit = {.width = 16, .height = 22, .scale_x = 1, .scale_y = 1};
	CHECK(st_console_blit_pixels(child, source, NULL, &blit) == 0);
	const struct st_console_rect screen = {0, 0, 64, 48};
	CHECK(st_console_get_pixels(&before, parent, &screen, false) == 0);
	for (uint32_t y = 0; y < 22; y++)
		for (uint32_t x = 0; x < 16; x++) {
			size_t at = (size_t)(23 + y) * 64 + 5 + x, from = (size_t)y * 16 + x;
			CHECK(before->primary[at] == (source->primary[from] | ST_CONSOLE_COLOR_VIEW_RGB)
			      && before->alternate[at] == (source->alternate[from] | ST_CONSOLE_COLOR_VIEW_RGB));
		}
	st_console_snapshot_t *saved = NULL;
	CHECK(st_console_snapshot_create(&saved, child) == 0);
	const struct st_console_style style = {.foreground = 7, .background = ST_PIXEL_COLOR_RGB_TAG | 0x123456};
	for (int direction = -1; direction <= 1; direction += 2) {
		CHECK(st_console_snapshot_restore(child, saved, NULL) == 0);
		CHECK(st_console_set_style(child, &style) == 0 && st_console_scroll(child, direction, ' ') == 0);
		CHECK(st_console_get_pixels(&after, parent, &screen, false) == 0);
		for (uint32_t y = 0; y < 48; y++)
			for (uint32_t x = 0; x < 64; x++) {
				size_t   at      = (size_t)y * 64 + x;
				uint32_t primary = before->primary[at], alternate = before->alternate[at];
				if (x >= 5 && x < 21 && y >= 23 && y < 45) {
					bool moved = direction > 0 ? y < 34 : y >= 34;
					if (moved) {
						size_t from = (size_t)((int)y - 23 + direction * 11) * 16 + x - 5;
						primary     = source->primary[from] | ST_CONSOLE_COLOR_VIEW_RGB;
						alternate   = source->alternate[from] | ST_CONSOLE_COLOR_VIEW_RGB;
					}
					else
						primary = alternate = style.background | ST_CONSOLE_COLOR_VIEW_RGB;
				}
				CHECK(after->primary[at] == primary && after->alternate[at] == alternate);
			}
		st_pixel_buffer_destroy(after);
	}
	CHECK(st_console_snapshot_restore(child, saved, NULL) == 0);
	CHECK(st_console_get_pixels(&after, parent, &screen, false) == 0);
	CHECK(memcmp(before->primary, after->primary, 64 * 48 * sizeof(*after->primary)) == 0
	      && memcmp(before->alternate, after->alternate, 64 * 48 * sizeof(*after->alternate)) == 0);
	st_console_snapshot_destroy(saved);
	st_pixel_buffer_destroy(after);
	st_pixel_buffer_destroy(before);
	st_pixel_buffer_destroy(source);
	st_term_rip3_view_destroy(view);
	view = NULL;
	/* Partial edge cells are clipped in both axes; no next-row overwrite. */
	layout.x = 61;
	layout.y = 46;
	CHECK(st_term_rip3_view_create(&view, &config, &layout, 640, 480) == 0
	      && st_term_rip3_view_begin(view, parent) == 0);
	CHECK(st_console_get_pixels(&before, parent, &screen, false) == 0);
	CHECK(write_text(st_term_rip3_view_text(view), "\x1b[48;2;18;52;86m ") == 0);
	CHECK(st_console_get_pixels(&after, parent, &screen, false) == 0);
	for (uint32_t y = 0; y < 48; y++)
		for (uint32_t x = 0; x < 64; x++) {
			size_t at = (size_t)y * 64 + x;
			CHECK(after->primary[at]
			      == (x >= 61 && y >= 46 ? (style.background | ST_CONSOLE_COLOR_VIEW_RGB)
			                             : before->primary[at]));
			CHECK(after->alternate[at]
			      == (x >= 61 && y >= 46 ? (style.background | ST_CONSOLE_COLOR_VIEW_RGB)
			                             : before->alternate[at]));
		}
	st_pixel_buffer_destroy(after);
	st_term_rip3_view_destroy(view);
	view = NULL;
	/* An offscreen 64-bit origin must never truncate into the visible raster. */
	layout.x = UINT64_MAX - 2;
	layout.y = UINT64_MAX - 2;
	CHECK(st_term_rip3_view_create(&view, &config, &layout, 640, 480) == 0
	      && st_term_rip3_view_begin(view, parent) == 0);
	CHECK(st_console_get_pixels(&after, parent, &screen, false) == 0);
	st_pixel_buffer_destroy(before);
	before = after;
	CHECK(write_text(st_term_rip3_view_text(view), "\x1b[48;2;4;5;6m A\r\nB\r\n") == 0);
	CHECK(st_term_rip3_view_present(view, parent, true) == 0);
	struct st_console_pixel_cursor cursor;
	CHECK(st_console_get_pixel_cursor(parent, &cursor) == 0 && !cursor.visible);
	CHECK(st_console_get_pixels(&after, parent, &screen, false) == 0);
	CHECK(memcmp(before->primary, after->primary, 64 * 48 * sizeof(*after->primary)) == 0
	      && memcmp(before->alternate, after->alternate, 64 * 48 * sizeof(*after->alternate)) == 0);
	st_pixel_buffer_destroy(before);
	st_pixel_buffer_destroy(after);
	st_term_rip3_view_destroy(view);
	st_font_catalog_destroy(fonts);
	st_console_destroy(parent);
	return 0;
}

int
main(void)
{
	st_console_t                          *parent  = NULL;
	st_font_catalog_t                     *fonts   = NULL;
	struct st_term_rip3_view              *view    = NULL;
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry = {.columns = 80, .rows = 60, .cell_width = 8, .cell_height = 8};
	CHECK(st_console_create(&parent, &display, &geometry) == 0 && st_font_catalog_create(&fonts, parent) == 0);
	const struct st_cterm_config            config = {.console = parent,
	    .fonts                                                 = fonts,
	    .emulation                                             = ST_CTERM_ANSI_BBS,
	    .encoding                                              = ST_CTERM_ENCODING_CODEPAGE,
	    .codepage                                              = ST_CODEPAGE_CP437,
	    .graphics_mode                                         = ST_CTERM_GRAPHICS_RIP_3,
	    .windowed_ansi                                         = true};
	const struct st_rip3_text_window_layout layout = {.x = 16,
	    .y                                               = 16,
	    .columns                                         = 4,
	    .rows                                            = 2,
	    .font                                            = 0,
	    .cell_width                                      = 8,
	    .cell_height                                     = 11,
	    .wrap                                            = true};
	CHECK(st_term_rip3_view_create(&view, &config, &layout, 640, 480) == 0);
	CHECK(st_term_rip3_view_begin(view, parent) == 0 && st_term_rip3_view_present(view, parent, true) == 0);
	CHECK(st_console_begin_update(parent) == 0);
	CHECK(st_term_rip3_view_begin(view, parent) == 0 && st_term_rip3_view_present(view, parent, true) == 0);
	CHECK(check_flags(parent, ST_CONSOLE_CELL_DIRTY, (struct st_console_rect) {0}) == 0);
	st_console_t                *child = st_term_rip3_view_console(view);
	struct st_console_cell       logical;
	const struct st_console_rect first = {0, 0, 1, 1};
	CHECK(st_console_read_cells(child, &first, &logical, 1) == 0);
	CHECK((logical.flags & ST_CONSOLE_CELL_PIXEL_GRAPHICS) == 0);
	st_cterm_t *text = st_term_rip3_view_text(view);
	CHECK(write_text(text, "\x1b[48;2;18;52;86m ") == 0);
	CHECK(st_term_rip3_view_present(view, parent, true) == 0);
	CHECK(check_flags(parent, ST_CONSOLE_CELL_DIRTY, (struct st_console_rect) {0}) == 0);
	CHECK(check_flags(parent, ST_CONSOLE_CELL_PIXEL_GRAPHICS, (struct st_console_rect) {2, 2, 1, 2}) == 0);
	CHECK(parent->pixel_damage_valid
	      && st_console_rects_equal(&parent->pixel_damage, &(struct st_console_rect) {16, 16, 8, 11}));
	struct st_pixel_buffer      *pixels = NULL;
	const struct st_console_rect cell   = {16, 16, 8, 11};
	CHECK(st_console_get_pixels(&pixels, parent, &cell, false) == 0);
	for (size_t i = 0; i < 88; i++)
		CHECK(pixels->primary[i] == (ST_CONSOLE_COLOR_VIEW_RGB | ST_PIXEL_COLOR_RGB_TAG | 0x123456));
	st_pixel_buffer_destroy(pixels);
	CHECK(st_console_end_update(parent) == 0);
	/* A cursor-only update does not damage either raster plane. */
	CHECK(st_console_begin_update(parent) == 0);
	CHECK(st_term_rip3_view_present(view, parent, true) == 0);
	CHECK(st_cterm_position_text(text, 2, 1) == 0 && st_term_rip3_view_present(view, parent, true) == 0);
	CHECK(check_flags(parent, ST_CONSOLE_CELL_DIRTY, (struct st_console_rect) {0}) == 0);
	CHECK(!parent->pixel_damage_valid);
	CHECK(st_console_end_update(parent) == 0);
	/* Another owner paints beneath retained text. Adopt it, then repeat
	 * the same logical blank: new native output must overwrite that artwork. */
	CHECK(st_console_set_pixel(parent, 16, 16, ST_PIXEL_COLOR_RGB_TAG | 0xff00) == 0);
	CHECK(st_term_rip3_view_begin(view, parent) == 0 && st_cterm_position_text(text, 0, 0) == 0);
	CHECK(write_text(text, " ") == 0 && st_term_rip3_view_present(view, parent, true) == 0);
	CHECK(st_console_get_pixels(&pixels, parent, &cell, false) == 0);
	for (size_t i = 0; i < 88; i++)
		CHECK(pixels->primary[i] == (ST_CONSOLE_COLOR_VIEW_RGB | ST_PIXEL_COLOR_RGB_TAG | 0x123456));
	st_pixel_buffer_destroy(pixels);
	/* Two writes to the same row leave the intervening parent cell text-owned.
	 * A repeated write also replaces foreign pixels without another adoption. */
	CHECK(st_console_set_pixel(parent, 16, 16, ST_PIXEL_COLOR_RGB_TAG | 0xff00) == 0);
	CHECK(st_console_read_cells(child, &first, &logical, 1) == 0);
	CHECK(st_console_begin_update(child) == 0);
	CHECK(st_console_write_complete_cells(child, &first, &logical, 1) == 0);
	CHECK(st_console_write_cells(child, &(struct st_console_rect) {3, 0, 1, 1}, &logical, 1) == 0);
	CHECK(st_console_end_update(child) == 0);
	struct st_console_cell gap;
	CHECK(st_console_read_cells(parent, &(struct st_console_rect) {3, 2, 1, 1}, &gap, 1) == 0);
	CHECK(!(gap.flags & ST_CONSOLE_CELL_PIXEL_GRAPHICS));
	CHECK(st_console_get_pixels(&pixels, parent, &cell, false) == 0);
	for (size_t i = 0; i < 88; i++)
		CHECK(pixels->primary[i] == (ST_CONSOLE_COLOR_VIEW_RGB | ST_PIXEL_COLOR_RGB_TAG | 0x123456)
		      && pixels->alternate[i] == pixels->primary[i]);
	st_pixel_buffer_destroy(pixels);
	/* Move a column of shared artwork within the view; both planes move even
	 * when the source and destination have identical logical text. */
	CHECK(st_console_set_pixel(parent, 16, 16, ST_PIXEL_COLOR_RGB_TAG | 0xaabbcc) == 0);
	CHECK(st_console_move_cells(child, &first, 3, 0) == 0);
	CHECK(st_console_get_pixels(&pixels, parent, &(struct st_console_rect) {40, 16, 1, 1}, false) == 0);
	CHECK(pixels->primary[0] == (ST_PIXEL_COLOR_RGB_TAG | 0xaabbcc)
	      && pixels->alternate[0] == pixels->primary[0]);
	st_pixel_buffer_destroy(pixels);
	/* An adopted cell is no longer retained text for palette changes. */
	CHECK(st_term_rip3_view_begin(view, parent) == 0);
	uint32_t rgb = 0x112233;
	CHECK(st_console_set_palette(child, 7, &rgb, 1) == 0);
	CHECK(st_console_get_pixels(&pixels, parent, &(struct st_console_rect) {16, 16, 1, 1}, false) == 0);
	CHECK(pixels->primary[0] == (ST_PIXEL_COLOR_RGB_TAG | 0xaabbcc));
	st_pixel_buffer_destroy(pixels);
	/* A masked blit also leaves ownership of the gap with the parent. */
	struct st_pixel_buffer *stripe = NULL;
	struct st_pixel_mask   *mask   = NULL;
	CHECK(st_pixel_buffer_create(&stripe, 32, 1, true) == 0);
	CHECK(st_pixel_mask_create(&mask, 32, 1) == 0 && st_pixel_mask_set(mask, 0, 0, true) == 0
	      && st_pixel_mask_set(mask, 31, 0, true) == 0);
	const struct st_pixel_blit masked = {.width = 32, .height = 1, .scale_x = 1, .scale_y = 1};
	CHECK(st_console_blit_pixels(child, stripe, mask, &masked) == 0);
	CHECK(st_console_read_cells(parent, &(struct st_console_rect) {3, 2, 1, 1}, &gap, 1) == 0);
	CHECK(!(gap.flags & ST_CONSOLE_CELL_PIXEL_GRAPHICS));
	st_pixel_mask_destroy(mask);
	st_pixel_buffer_destroy(stripe);
	/* Scroll a small window while a separate marker remains outside it. */
	CHECK(st_console_set_pixel(parent, 60, 60, ST_PIXEL_COLOR_RGB_TAG | 0xabcdef) == 0);
	CHECK(st_term_rip3_view_begin(view, parent) == 0);
	CHECK(write_text(text, "\x1b[48;2;1;2;3m\x1b[2;1H\r\n") == 0
	      && st_term_rip3_view_present(view, parent, true) == 0);
	const struct st_console_rect window = {16, 16, 32, 22}, marker = {60, 60, 1, 1};
	CHECK(st_console_get_pixels(&pixels, parent, &window, false) == 0);
	for (size_t i = 32 * 11; i < 32 * 22; i++)
		CHECK(pixels->primary[i] == (ST_CONSOLE_COLOR_VIEW_RGB | ST_PIXEL_COLOR_RGB_TAG | 0x010203));
	st_pixel_buffer_destroy(pixels);
	CHECK(st_console_get_pixels(&pixels, parent, &marker, false) == 0
	      && pixels->primary[0] == (ST_PIXEL_COLOR_RGB_TAG | 0xabcdef));
	st_pixel_buffer_destroy(pixels);
	st_term_rip3_view_destroy(view);
	st_font_catalog_destroy(fonts);
	st_console_destroy(parent);
	CHECK(shared_raster() == 0);
	return 0;
}

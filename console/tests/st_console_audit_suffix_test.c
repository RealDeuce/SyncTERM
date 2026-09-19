/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

/* Run tests otherwise unreachable after A040; retain the original failure. */
#define main original_console_test_main
#include "st_console_test.c"
#undef main

int
main(void)
{
	test_pixels_and_font_rendering();
	test_fast_font_attributes();
	test_raster_margin_and_scroll();
	test_saa5050_double_height_raster();
	test_concurrent_writers();
	test_deferred_presentation();
	test_unicode_wide_cells();
	test_ansi_wide_cell_output();
	test_ansi_output_modes();
	test_ansi_cp437_output();
	test_font_resolution_and_complete_cells();
	test_deferred_font_resolution();
	test_font_registry_lru_replacement();
	test_multiple_consoles_one_display();
	test_failed_detachment_lifetime();
	test_display_copies_ansi_options();
	/* Real terminal checks are covered by the isolated PTY backend tests. */
	return 0;
}

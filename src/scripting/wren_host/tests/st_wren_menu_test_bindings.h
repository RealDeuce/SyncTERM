/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

/* Private native-return injection for the menu binding contract tests. */
#ifndef ST_WREN_MENU_TEST_BINDINGS_H
#define ST_WREN_MENU_TEST_BINDINGS_H

#include <stdbool.h>

extern int st_wren_test_font_insert_status;
extern int st_wren_test_font_rename_status;
extern int st_wren_test_font_activation_status;
extern int st_wren_test_settings_save_status;
extern int st_wren_test_emoji_status;
extern unsigned st_wren_test_activation_calls;
extern unsigned st_wren_test_font_save_calls;
extern bool st_wren_test_sort_update_failure;
extern bool st_wren_test_prepare_save_failure;

#endif

# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(STWrenHost LANGUAGES C)
endif()

set(_st_wren_scripts
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/syncterm.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/syncterm_menu.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/syncterm_picker.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/picker_bootstrap.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/wren_console.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/menu_bbs_editor.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/menu_palette_picker.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/menu_scrollback.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/menu_settings_ui.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/menu_sort_profiles.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/menu_theme_editor.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/menu_ui.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui_app.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui_button.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui_checkbox.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui_color_picker.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui_date_picker.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui_draw.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui_form.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui_help.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui_input.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui_list.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui_logview.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui_markdown.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui_menubar.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui_pane.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui_picker.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui_popup.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui_progress.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui_radio.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui_spinbox.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui_statusbar.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui_style.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui_widget.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/auto/menu/menu_host_ui.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/auto/menu/main_menu.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/auto/picker/file_picker.wren)

set(_st_wren_embedded ${CMAKE_CURRENT_BINARY_DIR}/embedded_wren.c)
if(WIN32)
	set(_st_wren_picker_fs wren_picker_fs_windows.c)
else()
	set(_st_wren_picker_fs wren_picker_fs_posix.c)
endif()
add_custom_command(OUTPUT ${_st_wren_embedded}
	COMMAND ${CMAKE_COMMAND} -DOUTPUT=${_st_wren_embedded}
		-P ${CMAKE_CURRENT_SOURCE_DIR}/GenerateEmbeddedWren.cmake
		${_st_wren_scripts}
	DEPENDS ${_st_wren_scripts}
		${CMAKE_CURRENT_SOURCE_DIR}/GenerateEmbeddedWren.cmake
	VERBATIM)

add_library(st_wren_host STATIC
	wren_host.c
	wren_bind_core.c
	wren_repl.c
	wren_bind_menu.c
	wren_selection.c
	wren_picker.c
	${_st_wren_picker_fs}
	${_st_wren_embedded})
target_include_directories(st_wren_host
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
	PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(st_wren_host SYSTEM PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/../wren_vm/vm)
target_link_libraries(st_wren_host
	PUBLIC st_console st_app_paths st_profile
	PRIVATE st_wren_vm st_codepage st_utf8 st_bbs_list st_audio
		st_fonts st_settings st_theme st_time syncterm_io_error syncterm_path
		syncterm_string_compat syncterm_filesystem
		syncterm_value_convert xptls::crypto)
if(SYNCTERM_ENABLE_UNIFONT)
	target_compile_definitions(st_wren_host PRIVATE ST_HAVE_UNIFONT=1)
endif()
if(WIN32)
	target_link_libraries(st_wren_host PRIVATE st_windows_utf)
endif()
set_target_properties(st_wren_host PROPERTIES
	C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)

if(MSVC)
	target_compile_options(st_wren_host PRIVATE /W4 /WX)
else()
	target_compile_options(st_wren_host PRIVATE
		-Wall -Wextra -Wpedantic -Werror -Wno-overlength-strings)
endif()

if(BUILD_TESTING)
	add_executable(st_wren_host_test tests/st_wren_host_test.c)
	target_link_libraries(st_wren_host_test PRIVATE st_wren_host
		st_wren_vm st_settings st_theme syncterm_filesystem
		syncterm_threads)
	target_compile_definitions(st_wren_host_test PRIVATE
		ST_WREN_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/host-test")
	if(SYNCTERM_ENABLE_UNIFONT)
		target_compile_definitions(st_wren_host_test PRIVATE
			ST_HAVE_UNIFONT=1)
	endif()
	set_target_properties(st_wren_host_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_wren_host_test PRIVATE /W4 /WX)
	else()
		target_compile_options(st_wren_host_test PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME wren_host COMMAND st_wren_host_test)

	add_executable(st_wren_picker_test tests/st_wren_picker_test.c)
	target_link_libraries(st_wren_picker_test PRIVATE st_wren_host
		st_wren_vm syncterm_filesystem syncterm_path syncterm_array_list)
	target_compile_definitions(st_wren_picker_test PRIVATE
		ST_WREN_PICKER_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/picker-test")
	set_target_properties(st_wren_picker_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_wren_picker_test PRIVATE /W4 /WX)
	else()
		target_compile_options(st_wren_picker_test PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME wren_picker COMMAND st_wren_picker_test)
endif()

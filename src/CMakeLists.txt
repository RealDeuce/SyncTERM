# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/SynctermTestRunner.cmake")

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(SyncTERMApplication LANGUAGES C)
	include("${CMAKE_CURRENT_LIST_DIR}/../cmake/SynctermCStandard.cmake")
endif()

add_library(st_app_startup STATIC
	st_app_state.c
	st_command_line.c
	st_console_startup.c)
target_include_directories(st_app_startup PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_app_startup PUBLIC st_console st_profile st_settings
	st_screen_mode syncterm_io_error)
set_target_properties(st_app_startup PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)

add_library(st_application STATIC syncterm.c st_syncterm_icon.c st_terminfo.c)
target_include_directories(st_application PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(st_application PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
target_compile_definitions(st_application PRIVATE
	$<$<CONFIG:Debug>:ST_DEBUG_BUILD>)
target_link_libraries(st_application PUBLIC st_console st_audio
	PRIVATE st_wren_host st_settings
	st_app_startup st_audio st_fonts st_font_catalog st_term
	st_session_history st_hyperlink st_bbs_list)
target_link_libraries(st_application PRIVATE st_time syncterm_threads
	syncterm_value_convert syncterm_filesystem syncterm_path)
syncterm_target_atomics(st_application)
if(SYNCTERM_ENABLE_UNIFONT)
	target_link_libraries(st_application PRIVATE st_unifont)
	target_compile_definitions(st_application PRIVATE ST_HAVE_UNIFONT=1)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/SynctermSharedApplication.cmake")
syncterm_shared_dependencies(st_application)

if(WIN32)
	add_library(syncterm_core SHARED st_libretro.c st_libretro_input.c
		st_windows_application.c)
	set_target_properties(syncterm_core PROPERTIES
		OUTPUT_NAME syncterm PREFIX "" PDB_NAME syncterm-core
		WINDOWS_EXPORT_ALL_SYMBOLS OFF)
	include(GenerateExportHeader)
	generate_export_header(syncterm_core BASE_NAME ST_APPLICATION
		EXPORT_MACRO_NAME ST_APPLICATION_API
		EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/st_application_export.h")
	target_include_directories(syncterm_core PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
	if(TARGET st_console_main)
		target_compile_definitions(syncterm_core PRIVATE ST_APP_USE_SDL_DISPATCH=1)
	endif()
	add_executable(syncterm_app WIN32 st_windows_launcher.c)
	target_compile_definitions(syncterm_app PRIVATE ST_LAUNCHER_GUI=1)
	add_executable(syncterm_console st_windows_launcher.c)
	set_target_properties(syncterm_console PROPERTIES
		OUTPUT_NAME syncterm SUFFIX .com PDB_NAME syncterm-console)
	set_target_properties(syncterm_app PROPERTIES PDB_NAME syncterm-gui)
	foreach(_launcher syncterm_app syncterm_console)
		target_include_directories(${_launcher} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
		target_link_libraries(${_launcher} PRIVATE syncterm_core st_windows_utf shell32)
		set_target_properties(${_launcher} PROPERTIES
			C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	endforeach()
	set(_cli syncterm_console)
else()
	add_library(syncterm_core MODULE st_libretro.c st_libretro_input.c)
	set_target_properties(syncterm_core PROPERTIES OUTPUT_NAME syncterm_libretro PREFIX "")
	if(APPLE)
		set_target_properties(syncterm_core PROPERTIES SUFFIX .dylib)
	endif()
	add_executable(syncterm_app main.c)
	target_link_libraries(syncterm_app PRIVATE st_application)
	if(TARGET st_console_main)
		target_link_libraries(syncterm_app PRIVATE st_console_main)
	endif()
	set(_cli syncterm_app)
endif()
set_target_properties(syncterm_app PROPERTIES OUTPUT_NAME syncterm
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/SynctermMacOSBundle.cmake")
syncterm_macos_bundle(syncterm_app)
target_link_libraries(syncterm_core PRIVATE st_application syncterm_string_compat
	syncterm_path syncterm_filesystem syncterm_threads st_time)
syncterm_target_atomics(syncterm_core)
set_target_properties(syncterm_core PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO
	C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden)
# Keep application products independent of the source directory layout.
set(_st_products syncterm_app syncterm_core)
if(WIN32)
	list(APPEND _st_products syncterm_console)
endif()
foreach(_product IN LISTS _st_products)
	set_target_properties(${_product} PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
		LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
		PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
	syncterm_stage_dependency_runtime(${_product})
endforeach()
if(APPLE)
	set(_artifacts "$<TARGET_BUNDLE_DIR:syncterm_app>\n")
else()
	set(_artifacts "$<TARGET_FILE:syncterm_app>\n")
endif()
string(APPEND _artifacts "$<TARGET_FILE:syncterm_core>\n")
if(WIN32)
	string(APPEND _artifacts "$<TARGET_FILE:syncterm_console>\n")
endif()
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/syncterm-artifacts-$<CONFIG>.txt" CONTENT "${_artifacts}")
if(MSVC)
	# Use the portable C runtime APIs, not Microsoft-only _s replacements.
	target_compile_definitions(st_app_startup PRIVATE _CRT_SECURE_NO_WARNINGS)
	target_compile_options(st_app_startup PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	target_compile_options(syncterm_app PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	target_compile_options(st_application PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	target_compile_options(syncterm_core PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	if(WIN32)
		target_compile_options(syncterm_console PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	endif()
else()
	target_compile_options(st_app_startup PRIVATE
		-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	target_compile_options(syncterm_app PRIVATE
		-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	target_compile_options(st_application PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	target_compile_options(syncterm_core PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	if(WIN32)
		target_compile_options(syncterm_console PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
endif()

if(BUILD_TESTING)
	if(ST_CONSOLE_TEST)
		find_package(Python3 REQUIRED COMPONENTS Interpreter)
		if(Python3_Interpreter_FOUND)
			set(_emoji_groups menu connected presentation aspect)
			if(NOT WIN32 AND NOT APPLE)
				list(APPEND _emoji_groups localized)
			endif()
			foreach(_emoji_group IN LISTS _emoji_groups)
				add_test(NAME syncterm_emoji_${_emoji_group}
					COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_emoji_picker_test.py
					$<TARGET_FILE:syncterm_app> ${_emoji_group} --reports ${CMAKE_BINARY_DIR}/reports/test-control)
				set_tests_properties(syncterm_emoji_${_emoji_group} PROPERTIES TIMEOUT 90)
				if(Python3_VERSION VERSION_GREATER_EQUAL 3.10)
					# Catch accidental use of a Windows legacy text encoding on
					# every host, including ones whose default is already UTF-8.
					set_tests_properties(syncterm_emoji_${_emoji_group} PROPERTIES
						ENVIRONMENT "PYTHONWARNDEFAULTENCODING=1;PYTHONWARNINGS=error::EncodingWarning")
				endif()
			endforeach()
			foreach(_group ellipses raster buttons wire interaction)
				foreach(_encoding CP437 UTF-8)
					add_test(NAME syncterm_rip154_${_group}_${_encoding}
						COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_rip154_conformance_test.py
						$<TARGET_FILE:syncterm_app> ${_group} ${_encoding}
						--reports ${CMAKE_BINARY_DIR}/reports/test-control)
					set_tests_properties(syncterm_rip154_${_group}_${_encoding}
						PROPERTIES TIMEOUT 90 LABELS "terminal;conformance;rip")
				endforeach()
			endforeach()
			foreach(_encoding CP437 UTF-8)
				add_test(NAME syncterm_font_assignments_${_encoding}
					COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_font_assignments_test.py
						$<TARGET_FILE:syncterm_app> ${_encoding}
						--reports ${CMAKE_BINARY_DIR}/reports/test-control)
				set_tests_properties(syncterm_font_assignments_${_encoding}
					PROPERTIES TIMEOUT 90 LABELS "terminal;conformance;font")
			endforeach()
			add_test(NAME syncterm_amiga_font_manager
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_amiga_font_manager_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_amiga_font_manager PROPERTIES TIMEOUT 60)
			add_test(NAME syncterm_graphics_scrollback
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_graphics_scrollback_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_graphics_scrollback PROPERTIES
				TIMEOUT 180 LABELS "terminal;conformance;graphics;scrollback")
			add_test(NAME syncterm_skypix_mouse
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_skypix_mouse_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_skypix_mouse PROPERTIES
				TIMEOUT 180 LABELS "terminal;conformance;skypix;mouse")
			add_test(NAME syncterm_skypix_scrolling
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_skypix_scrolling_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_skypix_scrolling PROPERTIES
				TIMEOUT 180 LABELS "terminal;conformance;skypix;scrollback")
			add_test(NAME syncterm_skypix_cache
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_skypix_cache_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_skypix_cache PROPERTIES
				TIMEOUT 180 LABELS "terminal;conformance;skypix;transfer")
			add_test(NAME syncterm_skypix_conformance
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_skypix_conformance_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_skypix_conformance PROPERTIES
				TIMEOUT 180 LABELS "terminal;conformance;skypix")
			add_test(NAME syncterm_skypix_welcome
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_skypix_welcome_test.py
				$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_skypix_welcome PROPERTIES
				TIMEOUT 90 LABELS "terminal;conformance;skypix")
			add_test(NAME syncterm_skypix_report
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_skypix_report_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_skypix_report PROPERTIES
				TIMEOUT 90 LABELS "terminal;conformance;skypix")
			add_test(NAME syncterm_igs_init
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_init_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_init PROPERTIES
				TIMEOUT 120 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_resolution
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_resolution_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_resolution PROPERTIES
				TIMEOUT 180 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_scaling
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_scaling_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_scaling PROPERTIES
				TIMEOUT 180 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_writing_mode
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_writing_mode_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_writing_mode PROPERTIES
				TIMEOUT 180 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_pen
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_pen_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_pen PROPERTIES
				TIMEOUT 180 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_palette
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_palette_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_palette PROPERTIES
				TIMEOUT 180 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_hollow
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_hollow_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_hollow PROPERTIES
				TIMEOUT 180 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_fill
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_fill_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_fill PROPERTIES
				TIMEOUT 180 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_type
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_type_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_type PROPERTIES
				TIMEOUT 180 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_marker
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_marker_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_marker PROPERTIES
				TIMEOUT 180 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_line
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_line_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_line PROPERTIES
				TIMEOUT 420 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_drawto
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_drawto_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_drawto PROPERTIES
				TIMEOUT 420 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_x10
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_x10_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_x10 PROPERTIES
				TIMEOUT 420 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_polyline
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_polyline_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_polyline PROPERTIES
				TIMEOUT 420 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_rectangle
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_rectangle_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_rectangle PROPERTIES
				TIMEOUT 420 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_box
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_box_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_box PROPERTIES
				TIMEOUT 420 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_round_box
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_round_box_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_round_box PROPERTIES
				TIMEOUT 420 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_circle
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_filled_curve_test.py
					$<TARGET_FILE:syncterm_app> --command O --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_circle PROPERTIES
				TIMEOUT 420 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_ellipse
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_filled_curve_test.py
					$<TARGET_FILE:syncterm_app> --command Q --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_ellipse PROPERTIES
				TIMEOUT 420 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_pie
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_filled_curve_test.py
					$<TARGET_FILE:syncterm_app> --command V --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_pie PROPERTIES
				TIMEOUT 420 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_ellpie
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_filled_curve_test.py
					$<TARGET_FILE:syncterm_app> --command Y --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_ellpie PROPERTIES
				TIMEOUT 420 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_polygon
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_polygon_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_polygon PROPERTIES
				TIMEOUT 420 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_flood
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_flood_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_flood PROPERTIES
				TIMEOUT 420 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_console_color
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_console_color_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_console_color PROPERTIES
				TIMEOUT 420 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_console_color_del
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_console_color_test.py
					$<TARGET_FILE:syncterm_app> --del-only --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_console_color_del PROPERTIES
				TIMEOUT 180 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_position
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_position_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_position PROPERTIES
				TIMEOUT 420 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_move
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_move_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_move PROPERTIES
				TIMEOUT 240 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_cursor_save
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_cursor_save_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_cursor_save PROPERTIES
				TIMEOUT 120 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_cursor
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_cursor_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_cursor PROPERTIES
				TIMEOUT 120 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_reverse
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_reverse_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_reverse PROPERTIES
				TIMEOUT 120 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_wrap
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_wrap_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_wrap PROPERTIES
				TIMEOUT 120 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_line_erase
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_line_erase_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_line_erase PROPERTIES
				TIMEOUT 120 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_screen_erase
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_screen_erase_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_screen_erase PROPERTIES
				TIMEOUT 120 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_delete_lines
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_console_scroll_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_delete_lines PROPERTIES
				TIMEOUT 120 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_insert_lines
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_console_scroll_test.py
					$<TARGET_FILE:syncterm_app> --insert --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_insert_lines PROPERTIES
				TIMEOUT 120 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_pause
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_pause_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_pause PROPERTIES
				TIMEOUT 120 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_sync_wait
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_sync_wait_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_sync_wait PROPERTIES
				TIMEOUT 120 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_raw_palette
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_raw_palette_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_raw_palette PROPERTIES
				TIMEOUT 120 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_palette_group
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_raw_palette_test.py
					$<TARGET_FILE:syncterm_app> --groups --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_palette_group PROPERTIES
				TIMEOUT 120 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_pattern_load
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_pattern_load_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_pattern_load PROPERTIES
				TIMEOUT 120 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_random_range
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_random_range_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_random_range PROPERTIES
				TIMEOUT 120 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_scatter
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_scatter_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_scatter PROPERTIES
				TIMEOUT 120 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_math
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_math_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_math PROPERTIES
				TIMEOUT 120 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_grab
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_grab_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_grab PROPERTIES
				TIMEOUT 120 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_routing
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_routing_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_routing PROPERTIES
				TIMEOUT 120 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_bitmap
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_bitmap_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_bitmap PROPERTIES
				TIMEOUT 180 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_palette_rotation
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_palette_rotation_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_palette_rotation PROPERTIES
				TIMEOUT 180 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_sweep
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_sweep_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_sweep PROPERTIES
				TIMEOUT 180 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_effect
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_effect_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_effect PROPERTIES
				TIMEOUT 180 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_flow
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_flow_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_flow PROPERTIES
				TIMEOUT 180 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_macro
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_macro_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_macro PROPERTIES
				TIMEOUT 180 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_zone
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_zone_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_zone PROPERTIES
				TIMEOUT 180 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_left_mouse
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_left_mouse_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_left_mouse PROPERTIES
				TIMEOUT 180 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_music
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_music_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_music PROPERTIES
				TIMEOUT 180 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_note
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_note_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_note PROPERTIES
				TIMEOUT 180 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_text
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_text_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_text PROPERTIES
				TIMEOUT 420 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_text_attributes
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_text_attributes_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_text_attributes PROPERTIES
				TIMEOUT 420 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_arc
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_arc_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_arc PROPERTIES
				TIMEOUT 420 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_igs_ellarc
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_igs_arc_test.py
					$<TARGET_FILE:syncterm_app> --command J --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_igs_ellarc PROPERTIES
				TIMEOUT 420 LABELS "terminal;conformance;igs")
			add_test(NAME syncterm_skypix_cursor
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_skypix_cursor_test.py
					$<TARGET_FILE:syncterm_app> --reports ${CMAKE_BINARY_DIR}/reports/test-control)
			set_tests_properties(syncterm_skypix_cursor PROPERTIES
				TIMEOUT 180 LABELS "terminal;conformance;skypix;cursor")
			foreach(_group movement editing margins attributes queries extensions keyboard recovery)
				foreach(_encoding CP437 UTF-8)
					add_test(NAME syncterm_conformance_${_group}_${_encoding}
						COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_terminal_conformance_test.py
						$<TARGET_FILE:syncterm_app> ${_group} ${_encoding}
						--reports ${CMAKE_BINARY_DIR}/reports/test-control)
					set_tests_properties(syncterm_conformance_${_group}_${_encoding}
						PROPERTIES TIMEOUT 90 LABELS "terminal;conformance")
				endforeach()
			endforeach()
			foreach(_variant basic cp437 utf8 protocol transfer)
				add_test(NAME syncterm_control_${_variant}
					COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_test_control_test.py
					$<TARGET_FILE:syncterm_app> ${_variant})
				set_tests_properties(syncterm_control_${_variant} PROPERTIES TIMEOUT 90)
			endforeach()
			foreach(_case draw widget radio input form color menubar spinbox
					popup help progress menu logview console main_menu_address settings entry_editor
					list style button checkbox date palette statusbar pixels
					terminal_cp437 terminal_utf8 cterm_reset_cp437 cterm_reset_utf8 status_cp437 status_utf8
					rendering_cp437 rendering_utf8 scrollback_cp437 scrollback_utf8
					profile session_modes picker menu_exit menu_position menu_shortcuts autoload
					recovery recovery_close menu_error web_list bbs_setters settings_ui
					graphics_cp437 graphics_utf8 capture_cp437 capture_utf8)
				add_test(NAME syncterm_behavior_${_case}
					COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_migrated_control_test.py
					$<TARGET_FILE:syncterm_app> ${_case}
					--reports ${CMAKE_BINARY_DIR}/reports/test-control)
				set_tests_properties(syncterm_behavior_${_case} PROPERTIES TIMEOUT 90)
			endforeach()
			# Keep writable PTY-fixture paths in private XDG directories.
			if(TARGET st_app_paths_xdg_test)
				foreach(_mode a ac c f i)
					set(_shortcut_backend ansi)
					if(_mode MATCHES "^(c|f|i)$")
						set(_shortcut_backend ncurses)
					endif()
					if(TARGET st_console_${_shortcut_backend}_test)
						add_test(NAME syncterm_menu_shortcuts_pty_${_mode}
							COMMAND ${Python3_EXECUTABLE}
							${CMAKE_CURRENT_SOURCE_DIR}/tests/st_menu_shortcuts_pty_test.py
							$<TARGET_FILE:syncterm_app> ${_mode})
						set_tests_properties(syncterm_menu_shortcuts_pty_${_mode}
							PROPERTIES TIMEOUT 60)
					endif()
				endforeach()
			endif()
			if(SYNCTERM_ENABLE_UNIFONT)
				add_test(NAME syncterm_behavior_emoji
					COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_migrated_control_test.py
					$<TARGET_FILE:syncterm_app> emoji --reports ${CMAKE_BINARY_DIR}/reports/test-control)
				set_tests_properties(syncterm_behavior_emoji PROPERTIES TIMEOUT 90)
			endif()
			if(WIN32)
				add_test(NAME syncterm_control_console
					COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_test_control_test.py
					$<TARGET_FILE:syncterm_console> basic)
				set_tests_properties(syncterm_control_console PROPERTIES TIMEOUT 90)
			endif()
		endif()
	endif()
	add_executable(st_libretro_input_test tests/st_libretro_input_test.c st_libretro_input.c)
	target_link_libraries(st_libretro_input_test PRIVATE st_console)
	target_include_directories(st_libretro_input_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
	target_compile_definitions(st_libretro_input_test PRIVATE RETRO_IMPORT_SYMBOLS)
	add_test(NAME libretro_input COMMAND st_libretro_input_test)
	add_executable(st_libretro_test tests/st_libretro_test.c)
	target_include_directories(st_libretro_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
		${CMAKE_CURRENT_BINARY_DIR}/..)
	target_link_libraries(st_libretro_test PRIVATE syncterm_filesystem syncterm_threads ${CMAKE_DL_LIBS})
	if(WIN32)
		target_link_libraries(st_libretro_test PRIVATE st_windows_utf)
	endif()
	target_compile_definitions(st_libretro_test PRIVATE
		ST_LIBRETRO_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/libretro-test")
	add_dependencies(st_libretro_test syncterm_core)
	foreach(_test st_libretro_test st_libretro_input_test)
		set_target_properties(${_test} PROPERTIES
			C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
		if(MSVC)
			target_compile_definitions(${_test} PRIVATE _CRT_SECURE_NO_WARNINGS)
			target_compile_options(${_test} PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
		else()
			target_compile_options(${_test} PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
		endif()
	endforeach()
	add_test(NAME libretro_core COMMAND st_libretro_test $<TARGET_FILE:syncterm_core>)
	set_tests_properties(libretro_core PROPERTIES TIMEOUT 180 RESOURCE_LOCK libretro_files)
	add_test(NAME syncterm_help COMMAND ${_cli} --help)
	set_tests_properties(syncterm_help PROPERTIES
		PASS_REGULAR_EXPRESSION "usage:.*syncterm.*options")
	add_test(NAME syncterm_version COMMAND ${_cli} -v)
	set_tests_properties(syncterm_version PROPERTIES
		PASS_REGULAR_EXPRESSION "SyncTERM 2\\.0α")
	add_test(NAME syncterm_terminfo COMMAND ${_cli} -T)
	set_tests_properties(syncterm_terminfo PROPERTIES
		PASS_REGULAR_EXPRESSION "syncterm\\|SyncTERM")
	if(NOT WIN32)
		find_package(Python3 QUIET COMPONENTS Interpreter)
		if(Python3_Interpreter_FOUND)
			foreach(_mode IN ITEMS A C F I)
				if(_mode STREQUAL "A" OR ST_CONSOLE_HAVE_CURSES_H)
					add_test(NAME syncterm_text_startup_${_mode}
						COMMAND ${Python3_EXECUTABLE}
						${CMAKE_CURRENT_SOURCE_DIR}/tests/st_text_startup_test.py
						$<TARGET_FILE:${_cli}> ${_mode})
					set_tests_properties(syncterm_text_startup_${_mode}
						PROPERTIES TIMEOUT 25)
					add_test(NAME syncterm_text_scaling_${_mode}
						COMMAND ${Python3_EXECUTABLE}
						${CMAKE_CURRENT_SOURCE_DIR}/tests/st_text_startup_test.py
						$<TARGET_FILE:${_cli}> ${_mode} scaling)
					set_tests_properties(syncterm_text_scaling_${_mode}
						PROPERTIES TIMEOUT 25)
					foreach(_layout IN ITEMS center left)
						add_test(NAME syncterm_text_${_layout}_${_mode}
							COMMAND ${Python3_EXECUTABLE}
							${CMAKE_CURRENT_SOURCE_DIR}/tests/st_text_startup_test.py
							$<TARGET_FILE:${_cli}> ${_mode} ${_layout})
						set_tests_properties(syncterm_text_${_layout}_${_mode}
							PROPERTIES TIMEOUT 25)
					endforeach()
				endif()
			endforeach()
		endif()
	endif()
	syncterm_add_test_case(st_host_tests st_app_startup_test tests/st_app_startup_test.c)
	target_link_libraries(st_app_startup_test PRIVATE st_app_startup)
	set_target_properties(st_app_startup_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_app_startup_test PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_app_startup_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_app_startup_test PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME app_startup COMMAND st_host_tests st_app_startup_test)
	if(TARGET st_console_x11_test)
		find_package(X11 REQUIRED)
		syncterm_add_test_case(st_host_tests st_app_scaling_test ARGS tests/st_app_scaling_test.c)
		target_link_libraries(st_app_scaling_test PRIVATE st_application
			X11::X11 syncterm_ini syncterm_filesystem syncterm_threads)
		set_target_properties(st_app_scaling_test PROPERTIES
			C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
		target_compile_options(st_app_scaling_test PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
		add_test(NAME app_scaling COMMAND st_host_tests st_app_scaling_test
			${CMAKE_CURRENT_BINARY_DIR}/scaling-test)
		set_tests_properties(app_scaling PROPERTIES TIMEOUT 30
			SKIP_RETURN_CODE 77 RESOURCE_LOCK x11_display)
	endif()
	if(NOT WIN32)
		syncterm_add_test_case(st_host_tests st_app_session_mode_test
			tests/st_app_session_mode_test.c)
		target_link_libraries(st_app_session_mode_test PRIVATE st_application st_wren_host
			st_settings st_app_startup st_audio st_fonts st_font_catalog st_term st_amiga_font
			st_session_history st_hyperlink st_bbs_list st_time syncterm_threads syncterm_path
			syncterm_filesystem syncterm_value_convert)
		target_compile_definitions(st_app_session_mode_test PRIVATE
			ST_APP_SESSION_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/session-mode-test")
		if(SYNCTERM_ENABLE_UNIFONT)
			target_link_libraries(st_app_session_mode_test PRIVATE st_unifont)
			target_compile_definitions(st_app_session_mode_test PRIVATE ST_HAVE_UNIFONT=1)
		endif()
		set_target_properties(st_app_session_mode_test PROPERTIES
			C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
		target_compile_options(st_app_session_mode_test PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
		add_test(NAME app_session_mode COMMAND st_host_tests st_app_session_mode_test)
		set_tests_properties(app_session_mode PROPERTIES TIMEOUT 60)
	endif()
	syncterm_add_test_case(st_host_tests st_command_line_empty_backend_test
		tests/st_command_line_empty_backend_test.c)
	target_link_libraries(st_command_line_empty_backend_test PRIVATE
		st_app_startup)
	set_target_properties(st_command_line_empty_backend_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_command_line_empty_backend_test PRIVATE
			/W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_command_line_empty_backend_test PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME command_line_empty_backend
		COMMAND st_host_tests st_command_line_empty_backend_test)
endif()

target_link_libraries(st_application PRIVATE st_amiga_font)

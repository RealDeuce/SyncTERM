# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/SynctermCheckC17Source.cmake")

if(WIN32)
	set(ST_CTERM_RIP_LOOKUP_SOURCE cterm_rip_asset_lookup_windows.c)
	set(ST_CTERM_RIP_TIME_SOURCE cterm_rip_time_windows.c)
	syncterm_check_c17_source([=[
#include <time.h>
int main(void)
{
	time_t value = 0;
	struct tm result;
	return localtime_s(&result, &value) != 0;
}
]=] ST_CTERM_HAVE_LOCALTIME_S)
	if(NOT ST_CTERM_HAVE_LOCALTIME_S)
		message(FATAL_ERROR "The Windows C library must provide localtime_s for RIP file queries")
	endif()
else()
	set(ST_CTERM_RIP_LOOKUP_SOURCE cterm_rip_asset_lookup_posix.c)
	set(ST_CTERM_RIP_TIME_SOURCE cterm_rip_time_posix.c)
	set(_st_cterm_saved_required_definitions ${CMAKE_REQUIRED_DEFINITIONS})
	list(APPEND CMAKE_REQUIRED_DEFINITIONS -D_POSIX_C_SOURCE=200809L)
	syncterm_check_c17_source([=[
#include <time.h>
int main(void)
{
	time_t value = 0;
	struct tm result;
	return localtime_r(&value, &result) == 0;
}
]=] ST_CTERM_HAVE_LOCALTIME_R)
	set(CMAKE_REQUIRED_DEFINITIONS ${_st_cterm_saved_required_definitions})
	unset(_st_cterm_saved_required_definitions)
	if(NOT ST_CTERM_HAVE_LOCALTIME_R)
		message(FATAL_ERROR "The POSIX C library must provide localtime_r for RIP file queries")
	endif()
	set_source_files_properties(cterm_rip_time_posix.c PROPERTIES
		COMPILE_DEFINITIONS _POSIX_C_SOURCE=200809L)
endif()

add_library(st_cterm STATIC
	cterm.c
	cterm_atascii.c
	cterm_cterm.c
	cterm_dec.c
	cterm_ecma48.c
	cterm_render.c
	cterm_petscii.c
	cterm_prestel.c
	cterm_rip_asset.c
	${ST_CTERM_RIP_LOOKUP_SOURCE}
	cterm_rip_engine.c
	cterm_rip_file_query.c
	cterm_rip_fonts.c
	cterm_rip_icon_file.c
	cterm_rip_icons.c
	cterm_rip_parser.c
	cterm_rip_raster.c
	${ST_CTERM_RIP_TIME_SOURCE}
	st_cterm.c
	cterm_vt52.c)
target_include_directories(st_cterm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_cterm PUBLIC st_console st_codepage st_font_catalog st_hyperlink
	st_audio st_base64 st_time st_utf8 st_unicode_text)
target_link_libraries(st_cterm PRIVATE st_fonts syncterm_threads
	syncterm_string_compat syncterm_filesystem
	syncterm_path syncterm_ini)
if(WIN32)
	target_link_libraries(st_cterm PRIVATE st_windows_utf)
endif()
set_target_properties(st_cterm PROPERTIES C_STANDARD 17
	C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(MSVC)
	target_compile_options(st_cterm PRIVATE /W4 /WX)
else()
	target_compile_options(st_cterm PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(BUILD_TESTING)
	add_executable(st_cterm_benchmark tests/st_cterm_benchmark.c)
	target_link_libraries(st_cterm_benchmark PRIVATE st_cterm st_time)
	set_target_properties(st_cterm_benchmark PROPERTIES C_STANDARD 17
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_benchmark PRIVATE /W4 /WX)
	else()
		target_compile_options(st_cterm_benchmark PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME cterm_stream_consistency COMMAND st_cterm_benchmark 100)

	add_executable(st_cterm_rip_icon_file_test
		tests/st_cterm_rip_icon_file_test.c)
	target_link_libraries(st_cterm_rip_icon_file_test PRIVATE st_cterm)
	# The external RIP assets are optional test inputs, not a configuration
	# prerequisite. SyncTERM v1.10a is the rendering reference.
	if(EXISTS "/synchronet/tmp/ripterm/ICONS/RLIB.ICN")
		target_compile_definitions(st_cterm_rip_icon_file_test PRIVATE
			RIP_ICON_PATH="/synchronet/tmp/ripterm/ICONS/RLIB.ICN")
	endif()
	set_target_properties(st_cterm_rip_icon_file_test PROPERTIES C_STANDARD 17
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_rip_icon_file_test PRIVATE /W4 /WX)
	else()
		target_compile_options(st_cterm_rip_icon_file_test PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME cterm_rip_icon_file COMMAND st_cterm_rip_icon_file_test)
	add_executable(st_cterm_rip_assets_test
		tests/st_cterm_rip_assets_test.c)
	target_link_libraries(st_cterm_rip_assets_test PRIVATE st_cterm
		syncterm_filesystem syncterm_path)
	target_compile_definitions(st_cterm_rip_assets_test PRIVATE
		RIP_ASSET_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/rip-assets-test")
	if(EXISTS "/synchronet/tmp/ripterm/ICONS/RLIB.ICN")
		target_compile_definitions(st_cterm_rip_assets_test PRIVATE
			RIP_ICON_PATH="/synchronet/tmp/ripterm/ICONS/RLIB.ICN")
	endif()
	set_target_properties(st_cterm_rip_assets_test PROPERTIES C_STANDARD 17
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_rip_assets_test PRIVATE /W4 /WX)
	else()
		target_compile_options(st_cterm_rip_assets_test PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME cterm_rip_assets COMMAND st_cterm_rip_assets_test)

	add_executable(st_cterm_test tests/st_cterm_test.c)
	target_link_libraries(st_cterm_test PRIVATE st_cterm)
	set_target_properties(st_cterm_test PROPERTIES C_STANDARD 17
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_test PRIVATE /W4 /WX)
	else()
		target_compile_options(st_cterm_test PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME cterm COMMAND st_cterm_test)
	add_executable(st_cterm_keyboard_test tests/st_cterm_keyboard_test.c)
	target_link_libraries(st_cterm_keyboard_test PRIVATE st_cterm)
	set_target_properties(st_cterm_keyboard_test PROPERTIES C_STANDARD 17
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_keyboard_test PRIVATE /W4 /WX)
	else()
		target_compile_options(st_cterm_keyboard_test PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME cterm_keyboard COMMAND st_cterm_keyboard_test)

	add_executable(st_cterm_rip_parser_test tests/st_cterm_rip_parser_test.c)
	target_link_libraries(st_cterm_rip_parser_test PRIVATE st_cterm)
	set_target_properties(st_cterm_rip_parser_test PROPERTIES C_STANDARD 17
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_rip_parser_test PRIVATE /W4 /WX)
	else()
		target_compile_options(st_cterm_rip_parser_test PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME cterm_rip_parser COMMAND st_cterm_rip_parser_test)

	add_executable(st_cterm_rip_engine_test tests/st_cterm_rip_engine_test.c)
	target_link_libraries(st_cterm_rip_engine_test PRIVATE st_cterm)
	set_target_properties(st_cterm_rip_engine_test PROPERTIES C_STANDARD 17
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_rip_engine_test PRIVATE /W4 /WX)
	else()
		target_compile_options(st_cterm_rip_engine_test PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME cterm_rip_engine COMMAND st_cterm_rip_engine_test)

	add_executable(st_cterm_rip_raster_test tests/st_cterm_rip_raster_test.c)
	target_link_libraries(st_cterm_rip_raster_test PRIVATE st_cterm)
	set_target_properties(st_cterm_rip_raster_test PROPERTIES C_STANDARD 17
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_rip_raster_test PRIVATE /W4 /WX)
	else()
		target_compile_options(st_cterm_rip_raster_test PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME cterm_rip_raster COMMAND st_cterm_rip_raster_test)

	add_executable(st_cterm_rip_live_test tests/st_cterm_rip_live_test.c)
	target_link_libraries(st_cterm_rip_live_test PRIVATE st_cterm)
	set_target_properties(st_cterm_rip_live_test PROPERTIES C_STANDARD 17
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_rip_live_test PRIVATE /W4 /WX)
	else()
		target_compile_options(st_cterm_rip_live_test PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME cterm_rip_live COMMAND st_cterm_rip_live_test)

	add_executable(st_cterm_rip_variables_test
		tests/st_cterm_rip_variables_test.c)
	target_link_libraries(st_cterm_rip_variables_test PRIVATE st_cterm
		syncterm_filesystem syncterm_ini syncterm_path)
	target_compile_definitions(st_cterm_rip_variables_test PRIVATE
		RIP_VARIABLES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/rip-variables-test")
	set_target_properties(st_cterm_rip_variables_test PROPERTIES C_STANDARD 17
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_rip_variables_test PRIVATE /W4 /WX)
	else()
		target_compile_options(st_cterm_rip_variables_test PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME cterm_rip_variables COMMAND st_cterm_rip_variables_test)

	add_executable(st_cterm_rip_unicode_text_test
		tests/st_cterm_rip_unicode_text_test.c)
	target_link_libraries(st_cterm_rip_unicode_text_test PRIVATE st_cterm)
	set_target_properties(st_cterm_rip_unicode_text_test PROPERTIES C_STANDARD 17
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_rip_unicode_text_test PRIVATE /W4 /WX)
	else()
		target_compile_options(st_cterm_rip_unicode_text_test PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME cterm_rip_unicode_text
		COMMAND st_cterm_rip_unicode_text_test)

	# A local v1.10a capture can verify the external corpus input without
	# adding either the RIP file or its derived frame to this source tree.
	if(EXISTS "/synchronet/tmp/ripterm/rips/gl-morfr.rip"
		AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/v110a_gl_morfr_expected.h")
		add_executable(st_cterm_rip_v110a_live_test
			tests/st_cterm_rip_v110a_live_test.c)
		target_link_libraries(st_cterm_rip_v110a_live_test PRIVATE st_cterm)
		target_compile_definitions(st_cterm_rip_v110a_live_test PRIVATE
			RIP_V110A_GLMORFR_PATH="/synchronet/tmp/ripterm/rips/gl-morfr.rip")
		set_target_properties(st_cterm_rip_v110a_live_test PROPERTIES
			C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
		if(MSVC)
			target_compile_options(st_cterm_rip_v110a_live_test PRIVATE /W4 /WX)
		else()
			target_compile_options(st_cterm_rip_v110a_live_test PRIVATE
				-Wall -Wextra -Wpedantic -Werror)
		endif()
		add_test(NAME cterm_rip_v110a_live
			COMMAND st_cterm_rip_v110a_live_test)
	endif()

	add_executable(st_cterm_rip_corpus_inventory
		tests/st_cterm_rip_corpus_inventory.c)
	target_link_libraries(st_cterm_rip_corpus_inventory PRIVATE st_cterm)
	set_target_properties(st_cterm_rip_corpus_inventory PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_rip_corpus_inventory PRIVATE /W4 /WX)
	else()
		target_compile_options(st_cterm_rip_corpus_inventory PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()

	add_executable(st_cterm_rip_corpus_replay
		tests/st_cterm_rip_corpus_replay.c)
	target_link_libraries(st_cterm_rip_corpus_replay PRIVATE st_term)
	set_target_properties(st_cterm_rip_corpus_replay PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_rip_corpus_replay PRIVATE /W4 /WX)
	else()
		target_compile_options(st_cterm_rip_corpus_replay PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()

	find_package(Python3 COMPONENTS Interpreter QUIET)
	if(Python3_Interpreter_FOUND)
		add_test(NAME cterm_rip_oracle
			COMMAND Python3::Interpreter
				"${CMAKE_CURRENT_SOURCE_DIR}/tests/test_compare_legacy_rip_live.py")
	endif()
endif()

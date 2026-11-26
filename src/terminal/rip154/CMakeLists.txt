# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCheckC17Source.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermTestRunner.cmake")
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
	set(_rip_saved_definitions ${CMAKE_REQUIRED_DEFINITIONS})
	list(APPEND CMAKE_REQUIRED_DEFINITIONS "-D_POSIX_C_SOURCE=200809L")
	syncterm_check_c17_source([=[
#include <time.h>
int main(void)
{
	time_t value = 0;
	struct tm result;
	return localtime_r(&value, &result) == 0;
}
]=] ST_CTERM_HAVE_LOCALTIME_R)
	set(CMAKE_REQUIRED_DEFINITIONS ${_rip_saved_definitions})
	unset(_rip_saved_definitions)
	if(NOT ST_CTERM_HAVE_LOCALTIME_R)
		message(FATAL_ERROR "The POSIX C library must provide localtime_r for RIP file queries")
	endif()
	set_source_files_properties(cterm_rip_time_posix.c PROPERTIES
		COMPILE_DEFINITIONS _POSIX_C_SOURCE=200809L)
endif()

add_library(st_rip154 STATIC st_rip154.c
	cterm_rip_asset.c
	cterm_rip_args.c
	${ST_CTERM_RIP_LOOKUP_SOURCE}
	cterm_rip_engine.c
	cterm_rip_file_query.c
	cterm_rip_fonts.c
	cterm_rip_icon_file.c
	cterm_rip_icons.c
	cterm_rip_parser.c
	cterm_rip_raster.c
	${ST_CTERM_RIP_TIME_SOURCE})
target_include_directories(st_rip154 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_rip154 PUBLIC st_cterm PRIVATE st_bgi st_fonts st_pixel_image st_screen_mode syncterm_threads
	syncterm_filesystem syncterm_ini syncterm_string_compat syncterm_string_util syncterm_path)
set_target_properties(st_rip154 PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(WIN32)
	target_link_libraries(st_rip154 PRIVATE st_windows_utf)
endif()
if(MSVC)
	target_compile_definitions(st_rip154 PRIVATE _CRT_SECURE_NO_WARNINGS)
	target_compile_options(st_rip154 PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
else()
	target_compile_options(st_rip154 PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
endif()
if(BUILD_TESTING)
	option(ST_CTERM_TEST_EXTERNAL_ASSETS "Use optional external RIP test assets" ON)
	syncterm_add_test_case(st_terminal_tests st_cterm_rip_icon_file_test
		tests/st_cterm_rip_icon_file_test.c)
	target_link_libraries(st_cterm_rip_icon_file_test PRIVATE st_rip154)
	# The external RIP assets are optional test inputs, not a configuration
	# prerequisite. SyncTERM v1.10a is the rendering reference.
	if(ST_CTERM_TEST_EXTERNAL_ASSETS AND EXISTS "/synchronet/tmp/ripterm/ICONS/RLIB.ICN")
		target_compile_definitions(st_cterm_rip_icon_file_test PRIVATE
			RIP_ICON_PATH="/synchronet/tmp/ripterm/ICONS/RLIB.ICN")
	endif()
	set_target_properties(st_cterm_rip_icon_file_test PROPERTIES C_STANDARD ${SYNCTERM_C_STANDARD}
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_cterm_rip_icon_file_test PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_cterm_rip_icon_file_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_rip_icon_file_test PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_rip_icon_file COMMAND st_terminal_tests st_cterm_rip_icon_file_test)
	syncterm_add_test_case(st_terminal_tests st_cterm_rip_transfer_test tests/st_cterm_rip_transfer_test.c)
	target_link_libraries(st_cterm_rip_transfer_test PRIVATE st_rip154 syncterm_filesystem)
	target_compile_definitions(st_cterm_rip_transfer_test PRIVATE
		RIP_TRANSFER_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/rip-transfer-test")
	set_target_properties(st_cterm_rip_transfer_test PROPERTIES C_STANDARD ${SYNCTERM_C_STANDARD}
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_cterm_rip_transfer_test PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_cterm_rip_transfer_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_rip_transfer_test PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_rip_transfer COMMAND st_terminal_tests st_cterm_rip_transfer_test)
	if(NOT WIN32)
		target_compile_definitions(st_cterm_rip_transfer_test PRIVATE
			ST_RIP_TRANSFER_TEST_POSIX=1 _POSIX_C_SOURCE=200809L _DARWIN_C_SOURCE _NETBSD_SOURCE)
	endif()

	syncterm_add_test_case(st_terminal_tests st_cterm_rip_assets_test
		tests/st_cterm_rip_assets_test.c)
	target_link_libraries(st_cterm_rip_assets_test PRIVATE st_rip154
		syncterm_filesystem syncterm_path)
	target_compile_definitions(st_cterm_rip_assets_test PRIVATE
		RIP_ASSET_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/rip-assets-test")
	if(ST_CTERM_TEST_EXTERNAL_ASSETS AND EXISTS "/synchronet/tmp/ripterm/ICONS/RLIB.ICN")
		target_compile_definitions(st_cterm_rip_assets_test PRIVATE
			RIP_ICON_PATH="/synchronet/tmp/ripterm/ICONS/RLIB.ICN")
	endif()
	set_target_properties(st_cterm_rip_assets_test PROPERTIES C_STANDARD ${SYNCTERM_C_STANDARD}
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_cterm_rip_assets_test PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_cterm_rip_assets_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_rip_assets_test PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_rip_assets COMMAND st_terminal_tests st_cterm_rip_assets_test)

	syncterm_add_test_case(st_terminal_tests st_cterm_rip_args_test tests/st_cterm_rip_args_test.c)
	target_link_libraries(st_cterm_rip_args_test PRIVATE st_rip154)
	set_target_properties(st_cterm_rip_args_test PROPERTIES C_STANDARD ${SYNCTERM_C_STANDARD}
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_rip_args_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_rip_args_test PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_rip_args COMMAND st_terminal_tests st_cterm_rip_args_test)

	syncterm_add_test_case(st_terminal_tests st_cterm_rip_copy_test tests/st_cterm_rip_copy_test.c)
	target_link_libraries(st_cterm_rip_copy_test PRIVATE st_rip154)
	set_target_properties(st_cterm_rip_copy_test PROPERTIES C_STANDARD ${SYNCTERM_C_STANDARD}
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_rip_copy_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_rip_copy_test PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_rip_copy COMMAND st_terminal_tests st_cterm_rip_copy_test)

	syncterm_add_test_case(st_terminal_tests st_cterm_rip_parser_test tests/st_cterm_rip_parser_test.c)
	target_link_libraries(st_cterm_rip_parser_test PRIVATE st_rip154)
	set_target_properties(st_cterm_rip_parser_test PROPERTIES C_STANDARD ${SYNCTERM_C_STANDARD}
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_cterm_rip_parser_test PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_cterm_rip_parser_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_rip_parser_test PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_rip_parser COMMAND st_terminal_tests st_cterm_rip_parser_test)

	syncterm_add_test_case(st_terminal_tests st_cterm_rip_engine_test tests/st_cterm_rip_engine_test.c)
	target_link_libraries(st_cterm_rip_engine_test PRIVATE st_rip154)
	set_target_properties(st_cterm_rip_engine_test PROPERTIES C_STANDARD ${SYNCTERM_C_STANDARD}
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_cterm_rip_engine_test PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_cterm_rip_engine_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_rip_engine_test PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_rip_engine COMMAND st_terminal_tests st_cterm_rip_engine_test)

	syncterm_add_test_case(st_terminal_tests st_cterm_rip_raster_test tests/st_cterm_rip_raster_test.c)
	target_link_libraries(st_cterm_rip_raster_test PRIVATE st_rip154)
	set_target_properties(st_cterm_rip_raster_test PROPERTIES C_STANDARD ${SYNCTERM_C_STANDARD}
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_cterm_rip_raster_test PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_cterm_rip_raster_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_rip_raster_test PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_rip_raster COMMAND st_terminal_tests st_cterm_rip_raster_test)

	syncterm_add_test_case(st_terminal_tests st_cterm_rip_live_test tests/st_cterm_rip_live_test.c)
	target_link_libraries(st_cterm_rip_live_test PRIVATE st_rip154)
	set_target_properties(st_cterm_rip_live_test PROPERTIES C_STANDARD ${SYNCTERM_C_STANDARD}
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_cterm_rip_live_test PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_cterm_rip_live_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_rip_live_test PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_rip_live COMMAND st_terminal_tests st_cterm_rip_live_test)

	syncterm_add_test_case(st_terminal_tests st_cterm_rip_variables_test
		tests/st_cterm_rip_variables_test.c)
	target_link_libraries(st_cterm_rip_variables_test PRIVATE st_rip154
		syncterm_filesystem syncterm_ini syncterm_path)
	target_compile_definitions(st_cterm_rip_variables_test PRIVATE
		RIP_VARIABLES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/rip-variables-test")
	set_target_properties(st_cterm_rip_variables_test PROPERTIES C_STANDARD ${SYNCTERM_C_STANDARD}
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_cterm_rip_variables_test PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_cterm_rip_variables_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_rip_variables_test PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_rip_variables COMMAND st_terminal_tests st_cterm_rip_variables_test)

	syncterm_add_test_case(st_terminal_tests st_cterm_rip_unicode_text_test
		tests/st_cterm_rip_unicode_text_test.c)
	target_link_libraries(st_cterm_rip_unicode_text_test PRIVATE st_rip154)
	set_target_properties(st_cterm_rip_unicode_text_test PROPERTIES C_STANDARD ${SYNCTERM_C_STANDARD}
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_cterm_rip_unicode_text_test PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_cterm_rip_unicode_text_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_rip_unicode_text_test PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_rip_unicode_text
		COMMAND st_terminal_tests st_cterm_rip_unicode_text_test)

	# A local v1.10a capture can verify the external corpus input without
	# adding either the RIP file or its derived frame to this source tree.
	if(ST_CTERM_TEST_EXTERNAL_ASSETS AND EXISTS "/synchronet/tmp/ripterm/rips/gl-morfr.rip"
		AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/v110a_gl_morfr_expected.h")
		syncterm_add_test_case(st_terminal_tests st_cterm_rip_v110a_live_test
			tests/st_cterm_rip_v110a_live_test.c)
		target_link_libraries(st_cterm_rip_v110a_live_test PRIVATE st_rip154)
		target_compile_definitions(st_cterm_rip_v110a_live_test PRIVATE
			RIP_V110A_GLMORFR_PATH="/synchronet/tmp/ripterm/rips/gl-morfr.rip")
		set_target_properties(st_cterm_rip_v110a_live_test PROPERTIES
			C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
		if(MSVC)
			target_compile_definitions(st_cterm_rip_v110a_live_test PRIVATE _CRT_SECURE_NO_WARNINGS)
			target_compile_options(st_cterm_rip_v110a_live_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
		else()
			target_compile_options(st_cterm_rip_v110a_live_test PRIVATE
				-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
		endif()
		add_test(NAME cterm_rip_v110a_live
			COMMAND st_terminal_tests st_cterm_rip_v110a_live_test)
	endif()

	add_executable(st_cterm_rip_corpus_inventory EXCLUDE_FROM_ALL
		tests/st_cterm_rip_corpus_inventory.c)
	target_link_libraries(st_cterm_rip_corpus_inventory PRIVATE st_rip154)
	set_target_properties(st_cterm_rip_corpus_inventory PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_cterm_rip_corpus_inventory PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_cterm_rip_corpus_inventory PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_rip_corpus_inventory PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()

	add_executable(st_cterm_rip_corpus_replay EXCLUDE_FROM_ALL
		tests/st_cterm_rip_corpus_replay.c)
	target_link_libraries(st_cterm_rip_corpus_replay PRIVATE st_term)
	set_target_properties(st_cterm_rip_corpus_replay PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_cterm_rip_corpus_replay PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_cterm_rip_corpus_replay PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_rip_corpus_replay PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()

	find_package(Python3 COMPONENTS Interpreter QUIET)
	if(Python3_Interpreter_FOUND)
		add_test(NAME cterm_rip_oracle
			COMMAND Python3::Interpreter
				"${CMAKE_CURRENT_SOURCE_DIR}/tests/test_compare_legacy_rip_live.py")
	endif()
endif()

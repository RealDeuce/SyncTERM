# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(SyncTERMFonts LANGUAGES C)
	include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCStandard.cmake")
endif()

if(BUILD_TESTING)
	# Catalog eligibility is an integration contract with all three font banks.
	if(TARGET st_emoji_catalog)
		add_executable(st_emoji_catalog_test tests/st_emoji_catalog_test.c)
		target_link_libraries(st_emoji_catalog_test PRIVATE
			st_emoji_catalog st_fonts st_utf8 st_unicode_grapheme)
		set_target_properties(st_emoji_catalog_test PROPERTIES
			C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
		if(MSVC)
			target_compile_options(st_emoji_catalog_test PRIVATE /W4 /WX)
		else()
			target_compile_options(st_emoji_catalog_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
		endif()
		add_test(NAME emoji_catalog COMMAND st_emoji_catalog_test)
	endif()
	add_executable(st_fonts_test tests/st_fonts_test.c)
	target_link_libraries(st_fonts_test PRIVATE st_fonts)
	set_target_properties(st_fonts_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_fonts_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_fonts_test PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME fonts COMMAND st_fonts_test)
	add_executable(st_emoji_rll_test tests/st_emoji_rll_test.c)
	target_link_libraries(st_emoji_rll_test PRIVATE st_fonts st_time)
	target_include_directories(st_emoji_rll_test PRIVATE ../console/tests)
	set_target_properties(st_emoji_rll_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_emoji_rll_test PRIVATE /W4 /WX)
	else()
		target_compile_options(st_emoji_rll_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME emoji_rll COMMAND st_emoji_rll_test)
	add_executable(st_emoji_test tests/st_emoji_test.c)
	target_link_libraries(st_emoji_test PRIVATE st_fonts)
	set_target_properties(st_emoji_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_emoji_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_emoji_test PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME emoji COMMAND st_emoji_test)
endif()

add_library(st_fonts STATIC
	st_builtin_font_data.c
	st_builtin_fonts.c
	st_presentation_font.c
	generated/st_emoji_data.c
	generated/st_unscii_8.c
	generated/st_unscii_16.c)
target_include_directories(st_fonts PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_fonts PUBLIC st_codepage st_console PRIVATE st_utf8)
set_target_properties(st_fonts PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(MSVC)
	target_compile_options(st_fonts PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
else()
	target_compile_options(st_fonts PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
endif()

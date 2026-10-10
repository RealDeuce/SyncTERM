# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(STUnicodeText LANGUAGES C)
	include(CTest)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../io_error"
		"${CMAKE_CURRENT_BINARY_DIR}/io_error")
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../utf8"
		"${CMAKE_CURRENT_BINARY_DIR}/utf8")
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../unicode_width"
		"${CMAKE_CURRENT_BINARY_DIR}/unicode_width")
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../unicode_normalization"
		"${CMAKE_CURRENT_BINARY_DIR}/unicode_normalization")
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../unicode_grapheme"
		"${CMAKE_CURRENT_BINARY_DIR}/unicode_grapheme")
endif()

add_library(st_unicode_text STATIC st_unicode_text.c
	st_unicode_presentation.c)
target_include_directories(st_unicode_text PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_unicode_text PUBLIC
	syncterm_io_error st_utf8 st_unicode_width st_unicode_normalization
	st_unicode_grapheme)
set_target_properties(st_unicode_text PROPERTIES
	C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(MSVC)
	target_compile_options(st_unicode_text PRIVATE /W4 /WX)
else()
	target_compile_options(st_unicode_text PRIVATE
		-Wall -Wextra -Wpedantic -Werror)
endif()

if(BUILD_TESTING)
	add_executable(st_unicode_text_test tests/st_unicode_text_test.c)
	target_compile_definitions(st_unicode_text_test PRIVATE
		ST_UNICODE_NORMALIZATION_TEST_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/data/NormalizationTest-18.0.0.txt"
		ST_UNICODE_GRAPHEME_TEST_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/data/GraphemeBreakTest-18.0.0.txt")
	target_link_libraries(st_unicode_text_test PRIVATE st_unicode_text)
	set_target_properties(st_unicode_text_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_unicode_text_test PRIVATE /W4 /WX)
	else()
		target_compile_options(st_unicode_text_test PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME unicode_text COMMAND st_unicode_text_test)
endif()

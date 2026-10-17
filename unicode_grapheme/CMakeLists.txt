# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(STUnicodeGrapheme LANGUAGES C)
	include("${CMAKE_CURRENT_LIST_DIR}/../cmake/SynctermCStandard.cmake")
	include(CTest)
endif()

add_library(st_unicode_grapheme STATIC generated/st_unicode_grapheme.c)
target_include_directories(st_unicode_grapheme PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(st_unicode_grapheme PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(MSVC)
	target_compile_options(st_unicode_grapheme PRIVATE /W4 /WX)
else()
	target_compile_options(st_unicode_grapheme PRIVATE
		-Wall -Wextra -Wpedantic -Werror)
endif()

if(BUILD_TESTING)
	add_executable(st_unicode_grapheme_test tests/st_unicode_grapheme_test.c)
	target_link_libraries(st_unicode_grapheme_test PRIVATE st_unicode_grapheme)
	set_target_properties(st_unicode_grapheme_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_unicode_grapheme_test PRIVATE /W4 /WX)
	else()
		target_compile_options(st_unicode_grapheme_test PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME unicode_grapheme COMMAND st_unicode_grapheme_test)
endif()

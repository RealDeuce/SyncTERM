# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(STUnicodeNormalization LANGUAGES C)
	include(CTest)
endif()

add_library(st_unicode_normalization STATIC
	generated/st_unicode_normalization.c)
target_include_directories(st_unicode_normalization PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(st_unicode_normalization PROPERTIES
	C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(MSVC)
	target_compile_options(st_unicode_normalization PRIVATE /W4 /WX)
else()
	target_compile_options(st_unicode_normalization PRIVATE
		-Wall -Wextra -Wpedantic -Werror)
endif()

if(BUILD_TESTING)
	add_executable(st_unicode_normalization_test
		tests/st_unicode_normalization_test.c)
	target_link_libraries(st_unicode_normalization_test PRIVATE
		st_unicode_normalization)
	set_target_properties(st_unicode_normalization_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_unicode_normalization_test PRIVATE /W4 /WX)
	else()
		target_compile_options(st_unicode_normalization_test PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME unicode_normalization
		COMMAND st_unicode_normalization_test)
endif()

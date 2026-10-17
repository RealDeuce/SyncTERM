# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(STUTF8 LANGUAGES C)
	include("${CMAKE_CURRENT_LIST_DIR}/../cmake/SynctermCStandard.cmake")
	include(CTest)
endif()

if(NOT TARGET syncterm_io_error)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../io_error"
		"${CMAKE_CURRENT_BINARY_DIR}/io_error")
endif()

add_library(st_utf8 STATIC st_utf8.c)
target_include_directories(st_utf8 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_utf8 PUBLIC syncterm_io_error)
set_target_properties(st_utf8 PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(MSVC)
	target_compile_options(st_utf8 PRIVATE /W4 /WX)
else()
	target_compile_options(st_utf8 PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(BUILD_TESTING)
	add_executable(st_utf8_test tests/st_utf8_test.c)
	target_link_libraries(st_utf8_test PRIVATE st_utf8)
	set_target_properties(st_utf8_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_utf8_test PRIVATE /W4 /WX)
	else()
		target_compile_options(st_utf8_test PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME utf8 COMMAND st_utf8_test)
endif()

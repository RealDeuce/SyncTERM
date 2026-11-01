# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(STWindowsUTF LANGUAGES C)
	include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCStandard.cmake")
	include(CTest)
endif()

if(NOT WIN32)
	message(FATAL_ERROR "st_windows_utf is available only for Windows targets")
endif()
if(NOT TARGET syncterm_io_error)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../base/io_error"
		"${CMAKE_CURRENT_BINARY_DIR}/io_error")
endif()

add_library(st_windows_utf STATIC st_windows_utf.c)
target_include_directories(st_windows_utf PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_windows_utf PUBLIC syncterm_io_error)
set_target_properties(st_windows_utf PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(MSVC)
	target_compile_options(st_windows_utf PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
else()
	target_compile_options(st_windows_utf PRIVATE
		-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
endif()

if(BUILD_TESTING)
	add_executable(st_windows_utf_test tests/st_windows_utf_test.c)
	target_link_libraries(st_windows_utf_test PRIVATE st_windows_utf)
	set_target_properties(st_windows_utf_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_windows_utf_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_windows_utf_test PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME windows_utf COMMAND st_windows_utf_test)
endif()

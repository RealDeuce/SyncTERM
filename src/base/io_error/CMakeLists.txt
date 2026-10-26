# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(SyncTERMIOError LANGUAGES C)
	include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCStandard.cmake")
	include(CTest)
endif()

set(SYNCTERM_IO_ERROR_SOURCES io_error.c)
if(WIN32)
	list(APPEND SYNCTERM_IO_ERROR_SOURCES io_error_windows.c)
endif()

add_library(syncterm_io_error STATIC ${SYNCTERM_IO_ERROR_SOURCES})
target_include_directories(syncterm_io_error PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(syncterm_io_error PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(MSVC)
	target_compile_options(syncterm_io_error PRIVATE /W4 /WX)
else()
	target_compile_options(syncterm_io_error PRIVATE
		-Wall -Wextra -Wpedantic -Werror)
endif()

if(BUILD_TESTING)
	add_executable(io_error_test tests/io_error_test.c)
	target_link_libraries(io_error_test PRIVATE syncterm_io_error)
	set_target_properties(io_error_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(io_error_test PRIVATE /W4 /WX)
	else()
		target_compile_options(io_error_test PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME io_error COMMAND io_error_test)
endif()

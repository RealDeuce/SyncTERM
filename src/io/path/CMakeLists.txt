# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(SyncTERMPath LANGUAGES C)
	include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCStandard.cmake")
	include(CTest)
endif()

if(NOT TARGET syncterm_io_error)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../base/io_error"
		"${CMAKE_CURRENT_BINARY_DIR}/io_error")
endif()

if(WIN32)
	if(NOT TARGET st_windows_utf)
		add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../text/windows_utf"
			"${CMAKE_CURRENT_BINARY_DIR}/windows_utf")
	endif()
	set(SYNCTERM_PATH_PLATFORM_SOURCE path_windows.c)
else()
	set(SYNCTERM_PATH_PLATFORM_SOURCE path_posix.c)
endif()

add_library(syncterm_path STATIC path.c ${SYNCTERM_PATH_PLATFORM_SOURCE})
target_include_directories(syncterm_path PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(syncterm_path PUBLIC syncterm_io_error)
if(WIN32)
	target_link_libraries(syncterm_path PRIVATE st_windows_utf)
endif()
set_target_properties(syncterm_path PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD}
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS NO)

function(syncterm_path_warnings target)
	if(MSVC)
		# Use the portable C runtime APIs, not Microsoft-only _s replacements.
		target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(${target} PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(${target} PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
endfunction()

syncterm_path_warnings(syncterm_path)

if(BUILD_TESTING)
	add_executable(path_test tests/path_test.c)
	target_link_libraries(path_test PRIVATE syncterm_path)
	set_target_properties(path_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD}
		C_STANDARD_REQUIRED YES
		C_EXTENSIONS NO)
	syncterm_path_warnings(path_test)
	add_test(NAME path COMMAND path_test)
	if(NOT WIN32)
		set_tests_properties(path PROPERTIES
			ENVIRONMENT "HOME=/tmp/path-home")
	else()
		add_executable(path_windows_growth_test
			tests/path_windows_growth_test.c path.c)
		target_include_directories(path_windows_growth_test PRIVATE
			${CMAKE_CURRENT_SOURCE_DIR})
		target_link_libraries(path_windows_growth_test PRIVATE
			st_windows_utf syncterm_io_error)
		set_target_properties(path_windows_growth_test PROPERTIES
			C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
		syncterm_path_warnings(path_windows_growth_test)
		add_test(NAME path_windows_growth COMMAND
			path_windows_growth_test)
	endif()
endif()

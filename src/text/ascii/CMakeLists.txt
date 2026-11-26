# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(SyncTERMStringUtil LANGUAGES C)
	include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCStandard.cmake")
	include(CTest)
endif()

add_library(syncterm_string_util STATIC string_util.c)
target_include_directories(syncterm_string_util PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(syncterm_string_util PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)

function(syncterm_string_util_warnings target)
	if(MSVC)
		target_compile_options(${target} PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(${target} PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
endfunction()
syncterm_string_util_warnings(syncterm_string_util)

if(BUILD_TESTING)
	add_executable(string_util_test tests/string_util_test.c)
	target_link_libraries(string_util_test PRIVATE syncterm_string_util)
	set_target_properties(string_util_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	syncterm_string_util_warnings(string_util_test)
	add_test(NAME string_util COMMAND string_util_test)
endif()

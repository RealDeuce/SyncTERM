# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(SyncTERMStrList LANGUAGES C)
	include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCStandard.cmake")
	include(CTest)
endif()

if(NOT TARGET syncterm_string_compat)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../string_compat"
		"${CMAKE_CURRENT_BINARY_DIR}/string_compat")
endif()
if(NOT TARGET syncterm_array_list)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../array_list"
		"${CMAKE_CURRENT_BINARY_DIR}/array_list")
endif()

add_library(syncterm_str_list STATIC str_list.c)

target_include_directories(syncterm_str_list
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(syncterm_str_list PRIVATE
	syncterm_array_list syncterm_string_compat)
set_target_properties(syncterm_str_list PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD}
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS NO)

function(syncterm_str_list_warnings target)
	if(MSVC)
		target_compile_options(${target} PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(${target} PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
endfunction()

syncterm_str_list_warnings(syncterm_str_list)

if(BUILD_TESTING)
	add_executable(str_list_test tests/str_list_test.c)
	target_link_libraries(str_list_test PRIVATE syncterm_str_list)
	set_target_properties(str_list_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD}
		C_STANDARD_REQUIRED YES
		C_EXTENSIONS NO)
	syncterm_str_list_warnings(str_list_test)
	add_test(NAME str_list COMMAND str_list_test)

	add_executable(str_list_failure_test
		str_list.c
		tests/str_list_failure_test.c
		tests/str_list_test_array.c
		tests/str_list_test_allocator.c)
	target_include_directories(str_list_failure_test PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
		${CMAKE_CURRENT_SOURCE_DIR}/../array_list
		${CMAKE_CURRENT_SOURCE_DIR}/tests)
	target_compile_definitions(str_list_failure_test PRIVATE
		STR_LIST_TEST_ALLOCATOR=1)
	target_link_libraries(str_list_failure_test PRIVATE
		syncterm_string_compat)
	set_target_properties(str_list_failure_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD}
		C_STANDARD_REQUIRED YES
		C_EXTENSIONS NO)
	syncterm_str_list_warnings(str_list_failure_test)
	if(MSVC)
		# This allocator test also compiles array_list's flexible array member.
		target_compile_options(str_list_failure_test PRIVATE /wd4200)
	endif()
	add_test(NAME str_list_failures COMMAND str_list_failure_test)
endif()

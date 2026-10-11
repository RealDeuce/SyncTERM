# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(SyncTERMNameValueList LANGUAGES C)
	include(CTest)
endif()

if(NOT TARGET syncterm_array_list)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../array_list"
		"${CMAKE_CURRENT_BINARY_DIR}/array_list")
endif()
if(NOT TARGET syncterm_string_compat)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../string_compat"
		"${CMAKE_CURRENT_BINARY_DIR}/string_compat")
endif()
if(NOT TARGET syncterm_string_util)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../string_util"
		"${CMAKE_CURRENT_BINARY_DIR}/string_util")
endif()
if(NOT TARGET st_unicode_compare)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../unicode_compare"
		"${CMAKE_CURRENT_BINARY_DIR}/unicode_compare")
endif()
if(NOT TARGET syncterm_io_error)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../io_error"
		"${CMAKE_CURRENT_BINARY_DIR}/io_error")
endif()

add_library(syncterm_name_value_list STATIC name_value_list.c)
target_include_directories(syncterm_name_value_list PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(syncterm_name_value_list PUBLIC syncterm_array_list
	PRIVATE syncterm_string_compat syncterm_string_util st_unicode_compare
		syncterm_io_error)
set_target_properties(syncterm_name_value_list PROPERTIES
	C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)

function(syncterm_name_value_list_warnings target)
	if(MSVC)
		target_compile_options(${target} PRIVATE /W4 /WX)
	else()
		target_compile_options(${target} PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
endfunction()
syncterm_name_value_list_warnings(syncterm_name_value_list)

if(BUILD_TESTING)
	add_executable(name_value_list_test tests/name_value_list_test.c)
	target_link_libraries(name_value_list_test PRIVATE
		syncterm_name_value_list)
	set_target_properties(name_value_list_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	syncterm_name_value_list_warnings(name_value_list_test)
	add_test(NAME name_value_list COMMAND name_value_list_test)
endif()

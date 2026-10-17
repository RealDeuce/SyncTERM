# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(SyncTERMArrayList LANGUAGES C)
	include("${CMAKE_CURRENT_LIST_DIR}/../cmake/SynctermCStandard.cmake")
	include(CTest)
endif()

add_library(syncterm_array_list STATIC array_list.c)
target_include_directories(syncterm_array_list PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(syncterm_array_list PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD}
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS NO)

function(syncterm_array_list_warnings target)
	if(MSVC)
		target_compile_options(${target} PRIVATE /W4 /WX)
	else()
		target_compile_options(${target} PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
endfunction()

syncterm_array_list_warnings(syncterm_array_list)
if(MSVC)
	# MSVC diagnoses standard C flexible array members as an extension.
	target_compile_options(syncterm_array_list PRIVATE /wd4200)
endif()

if(BUILD_TESTING)
	add_executable(array_list_test tests/array_list_test.c)
	target_link_libraries(array_list_test PRIVATE syncterm_array_list)
	set_target_properties(array_list_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD}
		C_STANDARD_REQUIRED YES
		C_EXTENSIONS NO)
	syncterm_array_list_warnings(array_list_test)
	add_test(NAME array_list COMMAND array_list_test)
endif()

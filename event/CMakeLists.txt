# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(SyncTERMEvent LANGUAGES C)
	include(CTest)
endif()

if(NOT TARGET syncterm_threads)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../threads"
		"${CMAKE_CURRENT_BINARY_DIR}/threads")
endif()

add_library(syncterm_event STATIC st_event.c)
target_include_directories(syncterm_event PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(syncterm_event PUBLIC syncterm_threads)
set_target_properties(syncterm_event PROPERTIES
	C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)

function(st_event_warnings target)
	if(MSVC)
		target_compile_options(${target} PRIVATE /W4 /WX)
	else()
		target_compile_options(${target} PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
endfunction()
st_event_warnings(syncterm_event)

if(BUILD_TESTING)
	add_executable(st_event_test tests/st_event_test.c)
	target_link_libraries(st_event_test PRIVATE syncterm_event)
	set_target_properties(st_event_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	st_event_warnings(st_event_test)
	add_test(NAME st_event COMMAND st_event_test)
	set_tests_properties(st_event PROPERTIES TIMEOUT 20)
endif()

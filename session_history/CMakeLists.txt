# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(STSessionHistory LANGUAGES C)
	include("${CMAKE_CURRENT_LIST_DIR}/../cmake/SynctermCStandard.cmake")
endif()

add_library(st_session_history STATIC st_session_history.c)
target_include_directories(st_session_history PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_session_history PUBLIC st_console st_screen_mode
	PRIVATE syncterm_io_error syncterm_threads)
set_target_properties(st_session_history PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(MSVC)
	target_compile_options(st_session_history PRIVATE /W4 /WX)
else()
	target_compile_options(st_session_history PRIVATE
		-Wall -Wextra -Wpedantic -Werror)
endif()

if(BUILD_TESTING)
	add_executable(st_session_history_test tests/st_session_history_test.c)
	target_link_libraries(st_session_history_test PRIVATE st_session_history)
	set_target_properties(st_session_history_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_session_history_test PRIVATE /W4 /WX)
	else()
		target_compile_options(st_session_history_test PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME session_history COMMAND st_session_history_test)
endif()

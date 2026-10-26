# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(STHyperlink LANGUAGES C)
	include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCStandard.cmake")
	include(CTest)
endif()

if(NOT TARGET syncterm_io_error)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../base/io_error"
		"${CMAKE_CURRENT_BINARY_DIR}/io_error")
endif()
if(NOT TARGET syncterm_threads)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../base/threads"
		"${CMAKE_CURRENT_BINARY_DIR}/threads")
endif()

add_library(st_hyperlink STATIC st_hyperlink.c)
target_include_directories(st_hyperlink PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_hyperlink
	PUBLIC syncterm_io_error
	PRIVATE syncterm_threads)
set_target_properties(st_hyperlink PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)

function(st_hyperlink_warnings target)
	if(MSVC)
		target_compile_options(${target} PRIVATE /W4 /WX)
	else()
		target_compile_options(${target} PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
endfunction()
st_hyperlink_warnings(st_hyperlink)

if(BUILD_TESTING)
	add_executable(st_hyperlink_test tests/st_hyperlink_test.c)
	target_link_libraries(st_hyperlink_test PRIVATE st_hyperlink
		syncterm_threads)
	set_target_properties(st_hyperlink_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	st_hyperlink_warnings(st_hyperlink_test)
	add_test(NAME hyperlink COMMAND st_hyperlink_test)
	set_tests_properties(hyperlink PROPERTIES TIMEOUT 20)
endif()

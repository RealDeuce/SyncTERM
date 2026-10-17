# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(SyncTERMSemaphore LANGUAGES C)
	include("${CMAKE_CURRENT_LIST_DIR}/../cmake/SynctermCStandard.cmake")
	include(CTest)
endif()

if(NOT TARGET syncterm_threads)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../threads"
		"${CMAKE_CURRENT_BINARY_DIR}/threads")
endif()

add_library(syncterm_semaphore STATIC st_semaphore.c)
target_include_directories(syncterm_semaphore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(syncterm_semaphore PUBLIC syncterm_threads)
set_target_properties(syncterm_semaphore PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)

function(st_semaphore_warnings target)
	if(MSVC)
		target_compile_options(${target} PRIVATE /W4 /WX)
	else()
		target_compile_options(${target} PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
endfunction()
st_semaphore_warnings(syncterm_semaphore)

if(BUILD_TESTING)
	add_executable(st_semaphore_test tests/st_semaphore_test.c)
	target_link_libraries(st_semaphore_test PRIVATE syncterm_semaphore)
	set_target_properties(st_semaphore_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	st_semaphore_warnings(st_semaphore_test)
	add_test(NAME st_semaphore COMMAND st_semaphore_test)
	set_tests_properties(st_semaphore PROPERTIES TIMEOUT 20)
endif()

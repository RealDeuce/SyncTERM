# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermAtomics.cmake")

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(SyncTERMFileLock LANGUAGES C)
	include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCStandard.cmake")
	include(CTest)
endif()

if(NOT TARGET syncterm_threads)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../base/threads"
		"${CMAKE_CURRENT_BINARY_DIR}/threads")
endif()
if(NOT TARGET syncterm_io_error)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../base/io_error"
		"${CMAKE_CURRENT_BINARY_DIR}/io_error")
endif()
if(NOT TARGET syncterm_string_compat)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../base/string_compat"
		"${CMAKE_CURRENT_BINARY_DIR}/string_compat")
endif()
if(WIN32 AND NOT TARGET syncterm_path)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../path"
		"${CMAKE_CURRENT_BINARY_DIR}/path")
endif()

if(WIN32)
	set(ST_FILE_LOCK_PLATFORM_SOURCE st_file_lock_windows.c)
else()
	include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCheckC17Source.cmake")
	syncterm_check_c17_source([=[
#include <sys/file.h>
#include <unistd.h>
int main(void) { return flock(0, LOCK_SH) == 0; }
]=] ST_FILE_LOCK_HAVE_FLOCK)
	if(NOT ST_FILE_LOCK_HAVE_FLOCK)
		message(FATAL_ERROR
			"The selected POSIX programming environment does not provide flock()")
	endif()
	set(ST_FILE_LOCK_PLATFORM_SOURCE st_file_lock_posix.c)
endif()

add_library(st_file_lock STATIC st_file_lock.c
	${ST_FILE_LOCK_PLATFORM_SOURCE})
target_include_directories(st_file_lock PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_file_lock PUBLIC syncterm_io_error PRIVATE
	syncterm_string_compat syncterm_threads)
if(WIN32)
	target_link_libraries(st_file_lock PRIVATE syncterm_path)
endif()
set_target_properties(st_file_lock PROPERTIES C_STANDARD ${SYNCTERM_C_STANDARD}
	C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(MSVC)
	target_compile_options(st_file_lock PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
else()
	target_compile_options(st_file_lock PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
endif()

if(BUILD_TESTING)
	if(NOT TARGET syncterm_filesystem)
		add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../filesystem"
			"${CMAKE_CURRENT_BINARY_DIR}/filesystem")
	endif()
	add_executable(st_file_lock_test tests/st_file_lock_test.c)
	target_link_libraries(st_file_lock_test PRIVATE st_file_lock
		syncterm_filesystem syncterm_threads)
	target_compile_definitions(st_file_lock_test PRIVATE
		ST_FILE_LOCK_TEST_PATH="${CMAKE_CURRENT_BINARY_DIR}/coordination")
	syncterm_target_atomics(st_file_lock_test)
	set_target_properties(st_file_lock_test PROPERTIES C_STANDARD ${SYNCTERM_C_STANDARD}
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_file_lock_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_file_lock_test PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME file_lock COMMAND st_file_lock_test)
	set_tests_properties(file_lock PROPERTIES TIMEOUT 10)
	if(NOT WIN32)
		add_executable(st_file_lock_error_test
			tests/st_file_lock_error_test.c)
		target_include_directories(st_file_lock_error_test PRIVATE
			${CMAKE_CURRENT_SOURCE_DIR})
		target_link_libraries(st_file_lock_error_test PRIVATE
			syncterm_io_error)
		set_target_properties(st_file_lock_error_test PROPERTIES
			C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
		if(MSVC)
			target_compile_options(st_file_lock_error_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
		else()
			target_compile_options(st_file_lock_error_test PRIVATE
				-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
		endif()
		add_test(NAME file_lock_error COMMAND st_file_lock_error_test)
	endif()
endif()

# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(SyncTERMFilesystem LANGUAGES C)
	include(CTest)
endif()

if(NOT TARGET syncterm_path)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../path"
		"${CMAKE_CURRENT_BINARY_DIR}/path")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/SynctermCheckC17Source.cmake")

if(WIN32)
	set(SYNCTERM_FILESYSTEM_PLATFORM_SOURCE filesystem_windows.c)
	if(NOT TARGET st_windows_utf)
		add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../windows_utf"
			"${CMAKE_CURRENT_BINARY_DIR}/windows_utf")
	endif()
else()
	set(SYNCTERM_FILESYSTEM_PLATFORM_SOURCE filesystem_posix.c)
	set(SYNCTERM_FILESYSTEM_POSIX_DEFINITIONS)
	set(_filesystem_posix_probe_definitions)
	if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
		list(APPEND SYNCTERM_FILESYSTEM_POSIX_DEFINITIONS _GNU_SOURCE)
		list(APPEND _filesystem_posix_probe_definitions -D_GNU_SOURCE)
	endif()
	set(_filesystem_posix_probe [=[
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utime.h>

_Static_assert(sizeof(off_t) >= 8, "64-bit off_t required");

int main(void)
{
	struct stat status;
	struct utimbuf times = {0, 0};
	(void)fileno(stdin);
	(void)fstat(0, &status);
	(void)lstat(".", &status);
	(void)ftruncate(0, (off_t)0);
	(void)ftello(stdin);
	(void)fseeko(stdin, (off_t)0, SEEK_SET);
	(void)fchmod(0, (mode_t)0600);
	(void)utime(".", &times);
	return 0;
}
]=])
	set(_filesystem_saved_required_definitions
		${CMAKE_REQUIRED_DEFINITIONS})
	set(CMAKE_REQUIRED_DEFINITIONS
		${_filesystem_saved_required_definitions}
		${_filesystem_posix_probe_definitions})
	syncterm_check_c17_source("${_filesystem_posix_probe}"
		SYNCTERM_FILESYSTEM_POSIX_ENV_BASELINE)
	if(NOT SYNCTERM_FILESYSTEM_POSIX_ENV_BASELINE)
		set(CMAKE_REQUIRED_DEFINITIONS
			${_filesystem_saved_required_definitions}
			${_filesystem_posix_probe_definitions}
			-D_FILE_OFFSET_BITS=64)
		syncterm_check_c17_source("${_filesystem_posix_probe}"
			SYNCTERM_FILESYSTEM_POSIX_ENV_LARGEFILE_BASELINE)
		if(SYNCTERM_FILESYSTEM_POSIX_ENV_LARGEFILE_BASELINE)
			list(APPEND SYNCTERM_FILESYSTEM_POSIX_DEFINITIONS
				_FILE_OFFSET_BITS=64)
		endif()
	endif()
	set(CMAKE_REQUIRED_DEFINITIONS
		${_filesystem_saved_required_definitions})
	unset(_filesystem_saved_required_definitions)
	unset(_filesystem_posix_probe_definitions)
	unset(_filesystem_posix_probe)
	if(NOT SYNCTERM_FILESYSTEM_POSIX_ENV_BASELINE AND
	    NOT SYNCTERM_FILESYSTEM_POSIX_ENV_LARGEFILE_BASELINE)
		message(FATAL_ERROR
			"The selected programming environment does not declare the required filesystem APIs with 64-bit off_t")
	endif()
endif()

add_library(syncterm_filesystem STATIC filesystem.c
	${SYNCTERM_FILESYSTEM_PLATFORM_SOURCE})
target_include_directories(syncterm_filesystem PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(syncterm_filesystem PUBLIC
	syncterm_io_error syncterm_path)
if(WIN32)
	target_link_libraries(syncterm_filesystem PRIVATE st_windows_utf)
endif()
if(SYNCTERM_FILESYSTEM_POSIX_DEFINITIONS)
	target_compile_definitions(syncterm_filesystem PRIVATE
		${SYNCTERM_FILESYSTEM_POSIX_DEFINITIONS})
endif()
set_target_properties(syncterm_filesystem PROPERTIES
	C_STANDARD 17
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS NO)

function(syncterm_filesystem_warnings target)
	if(MSVC)
		target_compile_options(${target} PRIVATE /W4 /WX)
	else()
		target_compile_options(${target} PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
endfunction()

syncterm_filesystem_warnings(syncterm_filesystem)

if(BUILD_TESTING)
	add_executable(filesystem_test tests/filesystem_test.c)
	target_link_libraries(filesystem_test PRIVATE syncterm_filesystem)
	if(SYNCTERM_FILESYSTEM_POSIX_DEFINITIONS)
		target_compile_definitions(filesystem_test PRIVATE
			${SYNCTERM_FILESYSTEM_POSIX_DEFINITIONS})
	endif()
	set_target_properties(filesystem_test PROPERTIES
		C_STANDARD 17
		C_STANDARD_REQUIRED YES
		C_EXTENSIONS NO)
	syncterm_filesystem_warnings(filesystem_test)
	add_test(NAME filesystem COMMAND filesystem_test)
	if(WIN32)
		add_executable(filesystem_windows_root_test
			tests/filesystem_missing_root_test.c)
		target_link_libraries(filesystem_windows_root_test PRIVATE
			syncterm_filesystem)
		set_target_properties(filesystem_windows_root_test PROPERTIES
			C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
		syncterm_filesystem_warnings(filesystem_windows_root_test)
		add_test(NAME filesystem_windows_root COMMAND
			filesystem_windows_root_test)
		set_tests_properties(filesystem_windows_root PROPERTIES
			SKIP_RETURN_CODE 77)
	endif()
endif()

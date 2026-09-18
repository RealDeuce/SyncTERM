# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(SyncTERMFileMap LANGUAGES C)
	include(CTest)
endif()

foreach(_dependency IN ITEMS io_error filesystem)
	if(NOT TARGET syncterm_${_dependency})
		add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../${_dependency}"
			"${CMAKE_CURRENT_BINARY_DIR}/${_dependency}")
	endif()
endforeach()

include(CheckCSourceCompiles)

set(SYNCTERM_FILE_MAP_DEFINITIONS)
set(_file_map_saved_required_definitions ${CMAKE_REQUIRED_DEFINITIONS})
if(WIN32)
	set(SYNCTERM_FILE_MAP_PLATFORM_SOURCE file_map_windows.c)
	check_c_source_compiles([=[
#include <stdio.h>
#include <io.h>
#include <stdint.h>
#include <windows.h>
int main(void)
{
	int descriptor = _fileno(stdin);
	intptr_t native = _get_osfhandle(descriptor);
	BY_HANDLE_FILE_INFORMATION information;
	LARGE_INTEGER size;
	DWORD type = GetFileType((HANDLE)native);
	(void)GetFileInformationByHandle((HANDLE)native, &information);
	(void)GetFileSizeEx((HANDLE)native, &size);
	HANDLE mapping = CreateFileMappingW((HANDLE)native, NULL,
	    PAGE_READONLY, 0, 1, NULL);
	void *view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 1);
	(void)type;
	(void)UnmapViewOfFile(view);
	(void)CloseHandle(mapping);
	return 0;
}
]=] SYNCTERM_FILE_MAP_PLATFORM_BASELINE)
else()
	set(SYNCTERM_FILE_MAP_PLATFORM_SOURCE file_map_posix.c)
	if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
		list(APPEND SYNCTERM_FILE_MAP_DEFINITIONS _GNU_SOURCE)
		set(CMAKE_REQUIRED_DEFINITIONS
			${_file_map_saved_required_definitions} -D_GNU_SOURCE)
	endif()
	check_c_source_compiles([=[
#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
int main(void)
{
	int descriptor = fileno(stdin);
	struct stat status;
	int flags = fcntl(descriptor, F_GETFL);
	(void)fstat(descriptor, &status);
	void *view = mmap(NULL, 1, PROT_READ, MAP_PRIVATE, descriptor, 0);
	(void)flags;
	(void)munmap(view, 1);
	return 0;
}
]=] SYNCTERM_FILE_MAP_PLATFORM_BASELINE)
endif()
set(CMAKE_REQUIRED_DEFINITIONS ${_file_map_saved_required_definitions})

if(NOT SYNCTERM_FILE_MAP_PLATFORM_BASELINE)
	message(FATAL_ERROR
		"The selected programming environment does not provide the required read-only file-mapping APIs")
endif()

add_library(syncterm_file_map STATIC ${SYNCTERM_FILE_MAP_PLATFORM_SOURCE})
target_include_directories(syncterm_file_map PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(syncterm_file_map
	PUBLIC syncterm_io_error
	PRIVATE syncterm_filesystem)
if(SYNCTERM_FILE_MAP_DEFINITIONS)
	target_compile_definitions(syncterm_file_map PRIVATE
		${SYNCTERM_FILE_MAP_DEFINITIONS})
endif()
set_target_properties(syncterm_file_map PROPERTIES
	C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)

function(syncterm_file_map_warnings target)
	if(MSVC)
		target_compile_options(${target} PRIVATE /W4 /WX)
	else()
		target_compile_options(${target} PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
endfunction()
syncterm_file_map_warnings(syncterm_file_map)

if(BUILD_TESTING)
	add_executable(file_map_test tests/file_map_test.c)
	target_link_libraries(file_map_test PRIVATE
		syncterm_file_map syncterm_filesystem)
	if(SYNCTERM_FILE_MAP_DEFINITIONS)
		target_compile_definitions(file_map_test PRIVATE
			${SYNCTERM_FILE_MAP_DEFINITIONS})
	endif()
	set_target_properties(file_map_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	syncterm_file_map_warnings(file_map_test)
	add_test(NAME file_map COMMAND file_map_test)
	set_tests_properties(file_map PROPERTIES TIMEOUT 10)
endif()

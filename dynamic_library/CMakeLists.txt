# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(SyncTERMDynamicLibrary LANGUAGES C)
	include(CTest)
endif()

if(NOT TARGET syncterm_io_error)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../io_error"
		"${CMAKE_CURRENT_BINARY_DIR}/io_error")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/SynctermCheckC17Source.cmake")

if(WIN32)
	set(SYNCTERM_DYNAMIC_LIBRARY_PLATFORM_SOURCE dynamic_library_windows.c)
	syncterm_check_c17_source([=[
#include <windows.h>
int main(void)
{
	HMODULE library = LoadLibraryW(L"kernel32.dll");
	FARPROC symbol = GetProcAddress(library, "GetCurrentProcessId");
	(void)symbol;
	return FreeLibrary(library) ? 0 : 1;
}
]=] SYNCTERM_DYNAMIC_LIBRARY_PLATFORM_BASELINE)
else()
	set(SYNCTERM_DYNAMIC_LIBRARY_PLATFORM_SOURCE dynamic_library_posix.c)
	set(_dynamic_library_saved_required_libraries
		${CMAKE_REQUIRED_LIBRARIES})
	set(CMAKE_REQUIRED_LIBRARIES
		${_dynamic_library_saved_required_libraries} ${CMAKE_DL_LIBS})
	syncterm_check_c17_source([=[
#include <dlfcn.h>
int main(void)
{
	void *library = dlopen("libc.so", RTLD_NOW | RTLD_LOCAL);
	void *symbol = dlsym(library, "malloc");
	int global = RTLD_GLOBAL;
	(void)symbol;
	(void)global;
	return library == 0 ? 0 : dlclose(library);
}
]=] SYNCTERM_DYNAMIC_LIBRARY_PLATFORM_BASELINE)
	set(CMAKE_REQUIRED_LIBRARIES
		${_dynamic_library_saved_required_libraries})
endif()

if(NOT SYNCTERM_DYNAMIC_LIBRARY_PLATFORM_BASELINE)
	message(FATAL_ERROR
		"The selected platform does not provide the required dynamic-library APIs")
endif()

add_library(syncterm_dynamic_library STATIC
	${SYNCTERM_DYNAMIC_LIBRARY_PLATFORM_SOURCE})
if(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND WIN32)
	set_source_files_properties(dynamic_library_windows.c PROPERTIES
		COMPILE_OPTIONS -Wno-pedantic)
endif()
target_include_directories(syncterm_dynamic_library PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(syncterm_dynamic_library
	PUBLIC syncterm_io_error
	PRIVATE ${CMAKE_DL_LIBS})
if(APPLE)
	target_compile_definitions(syncterm_dynamic_library PRIVATE
		SYNCTERM_DYNAMIC_LIBRARY_DYLIB_NAMES=1)
endif()
set_target_properties(syncterm_dynamic_library PROPERTIES
	C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)

function(syncterm_dynamic_library_warnings target)
	if(MSVC)
		target_compile_options(${target} PRIVATE /W4 /WX)
	else()
		target_compile_options(${target} PRIVATE
		-Wall -Wextra -Wpedantic -Werror)
	endif()
endfunction()
syncterm_dynamic_library_warnings(syncterm_dynamic_library)

if(BUILD_TESTING)
	add_library(dynamic_library_fixture MODULE
		tests/dynamic_library_fixture.c)
	add_library(dynamic_library_versioned_fixture MODULE
		tests/dynamic_library_fixture.c)
	add_library(dynamic_library_bare_fixture MODULE
		tests/dynamic_library_fixture.c)
	add_library(dynamic_library_provider_fixture MODULE
		tests/dynamic_library_provider_fixture.c)

	if(WIN32)
		set_target_properties(dynamic_library_fixture
			dynamic_library_versioned_fixture
			dynamic_library_bare_fixture
			dynamic_library_provider_fixture PROPERTIES
			PREFIX ""
			WINDOWS_EXPORT_ALL_SYMBOLS YES)
	else()
		add_library(dynamic_library_consumer_fixture MODULE
			tests/dynamic_library_consumer_fixture.c)
		if(APPLE)
			target_link_options(dynamic_library_consumer_fixture PRIVATE
				"LINKER:-undefined,dynamic_lookup")
			set_target_properties(dynamic_library_versioned_fixture
				PROPERTIES SUFFIX ".2.dylib")
		else()
			set_target_properties(dynamic_library_versioned_fixture
				PROPERTIES SUFFIX ".so.2")
		endif()
		set_target_properties(dynamic_library_bare_fixture PROPERTIES
			PREFIX "")
	endif()

	foreach(_fixture IN ITEMS
	    dynamic_library_fixture
	    dynamic_library_versioned_fixture
	    dynamic_library_bare_fixture
	    dynamic_library_provider_fixture)
		set_target_properties(${_fixture} PROPERTIES
			C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
		syncterm_dynamic_library_warnings(${_fixture})
	endforeach()
	if(TARGET dynamic_library_consumer_fixture)
		set_target_properties(dynamic_library_consumer_fixture PROPERTIES
			C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
		syncterm_dynamic_library_warnings(
			dynamic_library_consumer_fixture)
	endif()

	add_executable(dynamic_library_test tests/dynamic_library_test.c)
	if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
		set_source_files_properties(tests/dynamic_library_test.c PROPERTIES
			COMPILE_OPTIONS -Wno-pedantic)
	endif()
	target_link_libraries(dynamic_library_test PRIVATE
		syncterm_dynamic_library)
	if(NOT WIN32)
		target_compile_definitions(dynamic_library_test PRIVATE
			SYNCTERM_DYNAMIC_LIBRARY_TEST_POSIX=1)
	endif()
	set_target_properties(dynamic_library_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO
		BUILD_RPATH "$<TARGET_FILE_DIR:dynamic_library_fixture>")
	syncterm_dynamic_library_warnings(dynamic_library_test)
	add_dependencies(dynamic_library_test
		dynamic_library_fixture
		dynamic_library_versioned_fixture
		dynamic_library_bare_fixture
		dynamic_library_provider_fixture)
	if(TARGET dynamic_library_consumer_fixture)
		add_dependencies(dynamic_library_test
			dynamic_library_consumer_fixture)
	endif()

	add_test(NAME dynamic_library COMMAND dynamic_library_test)
	set_tests_properties(dynamic_library PROPERTIES TIMEOUT 10)
	if(APPLE)
		set_tests_properties(dynamic_library PROPERTIES
			ENVIRONMENT_MODIFICATION
			"DYLD_LIBRARY_PATH=path_list_prepend:$<TARGET_FILE_DIR:dynamic_library_fixture>")
	elseif(UNIX)
		set_tests_properties(dynamic_library PROPERTIES
			ENVIRONMENT_MODIFICATION
			"LD_LIBRARY_PATH=path_list_prepend:$<TARGET_FILE_DIR:dynamic_library_fixture>")
	endif()
endif()

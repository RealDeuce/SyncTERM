# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
find_package(Python3 REQUIRED COMPONENTS Interpreter)
file(GLOB _amiga_inputs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/data/*.yaff")
set(_amiga_generated "${CMAKE_CURRENT_BINARY_DIR}/amiga_font_data.c")
add_custom_command(OUTPUT "${_amiga_generated}"
	COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/generate_fonts.py"
		"${CMAKE_CURRENT_SOURCE_DIR}/data/manifest.json" "${_amiga_generated}"
	DEPENDS generate_fonts.py data/manifest.json ${_amiga_inputs}
	VERBATIM)
if(WIN32)
	set(_amiga_directory amiga_directory_windows.c)
else()
	include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCheckC17Source.cmake")
	syncterm_check_c17_source([=[
#include <dirent.h>
#include <errno.h>
int main(void) {
	DIR *directory = opendir(".");
	if (directory == 0) return errno;
	struct dirent *entry = readdir(directory);
	int result = entry == 0 ? 0 : entry->d_name[0];
	return closedir(directory) + result;
}
]=] ST_AMIGA_HAVE_DIRECTORY_ENUMERATION)
	if(NOT ST_AMIGA_HAVE_DIRECTORY_ENUMERATION)
		message(FATAL_ERROR "Amiga font management requires POSIX directory enumeration")
	endif()
	set(_amiga_directory amiga_directory_posix.c)
endif()
add_library(st_amiga_font STATIC st_amiga_font.c st_amiga_fonts.c
	${_amiga_directory} "${_amiga_generated}")
target_include_directories(st_amiga_font PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_amiga_font PRIVATE syncterm_filesystem syncterm_path syncterm_string_compat st_unicode_compare st_unicode_text)
set_target_properties(st_amiga_font PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(MSVC)
	target_compile_definitions(st_amiga_font PRIVATE _CRT_SECURE_NO_WARNINGS)
	target_compile_options(st_amiga_font PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
else()
	target_compile_options(st_amiga_font PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
endif()
if(BUILD_TESTING)
	add_executable(st_amiga_font_test tests/st_amiga_font_test.c)
	target_link_libraries(st_amiga_font_test PRIVATE st_amiga_font syncterm_io_error syncterm_filesystem syncterm_path)
	target_compile_definitions(st_amiga_font_test PRIVATE ST_AMIGA_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/test")
	set_target_properties(st_amiga_font_test PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	add_test(NAME amiga_font COMMAND st_amiga_font_test)
endif()

if(WIN32)
	target_link_libraries(st_amiga_font PRIVATE st_windows_utf)
endif()

if(BUILD_TESTING)
	add_executable(st_amiga_font_fault_test tests/st_amiga_font_fault_test.c)
	target_link_libraries(st_amiga_font_fault_test PRIVATE st_amiga_font
		syncterm_filesystem syncterm_path syncterm_string_compat st_unicode_compare st_unicode_text)
	target_compile_definitions(st_amiga_font_fault_test PRIVATE
		ST_AMIGA_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/fault-test")
	set_target_properties(st_amiga_font_fault_test PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	add_test(NAME amiga_font_transactions COMMAND st_amiga_font_fault_test)
endif()

if(BUILD_TESTING)
	foreach(_test IN ITEMS st_amiga_font_test st_amiga_font_fault_test)
		if(MSVC)
			target_compile_options(${_test} PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
		else()
			target_compile_options(${_test} PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
		endif()
	endforeach()
endif()

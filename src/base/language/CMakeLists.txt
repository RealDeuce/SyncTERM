# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(STLanguage LANGUAGES C)
	include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCStandard.cmake")
	include(CTest)
endif()

if(NOT TARGET syncterm_io_error)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../io_error"
		"${CMAKE_CURRENT_BINARY_DIR}/io_error")
endif()
if(NOT TARGET syncterm_string_compat)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../string_compat"
		"${CMAKE_CURRENT_BINARY_DIR}/string_compat")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCheckC17Source.cmake")

set(_st_language_libraries)
if(WIN32)
	set(_st_language_platform st_language_windows.c)
	syncterm_check_c17_source([=[
#include <windows.h>
int main(void)
{
	ULONG count = 0, size = 0;
	WCHAR locale[LOCALE_NAME_MAX_LENGTH];
	BOOL ok = GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, NULL, &size);
	return LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT),
	    locale, LOCALE_NAME_MAX_LENGTH, 0) == 0 || !ok;
}
]=] ST_LANGUAGE_HAVE_WINDOWS_PREFERENCES)
	if(NOT ST_LANGUAGE_HAVE_WINDOWS_PREFERENCES)
		message(FATAL_ERROR "Windows UI language preference APIs are required")
	endif()
else()
	set(_st_language_platform st_language_posix.c)
	if(APPLE)
		find_library(ST_LANGUAGE_COREFOUNDATION CoreFoundation REQUIRED)
		mark_as_advanced(ST_LANGUAGE_COREFOUNDATION)
		set(_st_language_saved_libraries ${CMAKE_REQUIRED_LIBRARIES})
		list(APPEND CMAKE_REQUIRED_LIBRARIES ${ST_LANGUAGE_COREFOUNDATION})
		syncterm_check_c17_source([=[
#include <CoreFoundation/CoreFoundation.h>
int main(void)
{
	CFArrayRef languages = CFLocaleCopyPreferredLanguages();
	char tag[256];
	if (languages == NULL) return 0;
	if (CFArrayGetCount(languages) > 0) {
		CFStringRef value = CFArrayGetValueAtIndex(languages, 0);
		if (CFGetTypeID(value) == CFStringGetTypeID())
			(void)CFStringGetCString(value, tag, sizeof(tag), kCFStringEncodingASCII);
	}
	CFRelease(languages);
	return 0;
}
]=] ST_LANGUAGE_HAVE_MACOS_PREFERENCES)
		set(CMAKE_REQUIRED_LIBRARIES ${_st_language_saved_libraries})
		if(NOT ST_LANGUAGE_HAVE_MACOS_PREFERENCES)
			message(FATAL_ERROR "macOS UI language preference APIs are required")
		endif()
		list(APPEND _st_language_platform st_language_macos.c)
		list(APPEND _st_language_libraries ${ST_LANGUAGE_COREFOUNDATION})
	endif()
endif()

configure_file(st_language_config.h.in st_language_config.h)
add_library(st_language STATIC st_language.c ${_st_language_platform})
target_include_directories(st_language PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
	PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(st_language PUBLIC syncterm_io_error
	PRIVATE syncterm_string_compat ${_st_language_libraries})

function(st_language_target_settings target)
	set_target_properties(${target} PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(${target} PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(${target} PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
endfunction()
st_language_target_settings(st_language)

if(BUILD_TESTING)
	add_executable(st_language_test tests/st_language_test.c)
	target_link_libraries(st_language_test PRIVATE st_language)
	st_language_target_settings(st_language_test)
	add_test(NAME language COMMAND st_language_test)
	add_executable(st_language_fault_test tests/st_language_fault_test.c)
	target_link_libraries(st_language_fault_test PRIVATE
		syncterm_io_error syncterm_string_compat)
	target_include_directories(st_language_fault_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
	st_language_target_settings(st_language_fault_test)
	add_test(NAME language_allocation_failure COMMAND st_language_fault_test)
	if(NOT WIN32 AND NOT ST_LANGUAGE_HAVE_MACOS_PREFERENCES)
		add_test(NAME language_environment COMMAND ${CMAKE_COMMAND} -E env
			--unset=LC_ALL --unset=LC_MESSAGES LANG=en_US.UTF-8
			LANGUAGE=fr_CA:de:fr-CA $<TARGET_FILE:st_language_test>
			--expect fr-CA de)
		add_test(NAME language_c_locale COMMAND ${CMAKE_COMMAND} -E env
			LC_ALL=C LC_MESSAGES=ja_JP.UTF-8 LANG=de_DE.UTF-8
			LANGUAGE=fr $<TARGET_FILE:st_language_test> --expect en)
		add_test(NAME language_invalid_locale COMMAND ${CMAKE_COMMAND} -E env
			--unset=LANGUAGE --unset=LC_MESSAGES --unset=LANG LC_ALL=invalid/locale
			$<TARGET_FILE:st_language_test> --expect en)
	endif()
endif()

# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(SyncTERMValueConvert LANGUAGES C)
	include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCStandard.cmake")
	include(CTest)
endif()

if(NOT TARGET syncterm_string_util)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../string_util"
		"${CMAKE_CURRENT_BINARY_DIR}/string_util")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCheckC17Source.cmake")

if(WIN32)
	set(SYNCTERM_VALUE_CONVERT_TIME_SOURCE value_convert_time_windows.c)
	syncterm_check_c17_source([=[
#include <time.h>
int main(void)
{
	time_t value = 0;
	struct tm result;
	return localtime_s(&result, &value) != 0;
}
]=] SYNCTERM_VALUE_CONVERT_HAVE_WINDOWS_LOCALTIME_S)
	if(NOT SYNCTERM_VALUE_CONVERT_HAVE_WINDOWS_LOCALTIME_S)
		message(FATAL_ERROR
			"The target Windows C library does not provide localtime_s")
	endif()
else()
	set(SYNCTERM_VALUE_CONVERT_TIME_SOURCE value_convert_time_posix.c)
	set(SYNCTERM_VALUE_CONVERT_POSIX_DEFINITIONS)
	set(_value_convert_probe_definitions)
	if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
		list(APPEND SYNCTERM_VALUE_CONVERT_POSIX_DEFINITIONS _GNU_SOURCE)
		list(APPEND _value_convert_probe_definitions -D_GNU_SOURCE)
	endif()
	set(_value_convert_saved_required_definitions
		${CMAKE_REQUIRED_DEFINITIONS})
	set(CMAKE_REQUIRED_DEFINITIONS
		${_value_convert_saved_required_definitions}
		${_value_convert_probe_definitions})
	syncterm_check_c17_source([=[
#include <time.h>
int main(void)
{
	time_t value = 0;
	struct tm result;
	return localtime_r(&value, &result) == 0;
}
]=] SYNCTERM_VALUE_CONVERT_ENV_HAVE_LOCALTIME_R)
	set(CMAKE_REQUIRED_DEFINITIONS
		${_value_convert_saved_required_definitions})
	unset(_value_convert_saved_required_definitions)
	unset(_value_convert_probe_definitions)
	if(NOT SYNCTERM_VALUE_CONVERT_ENV_HAVE_LOCALTIME_R)
		message(FATAL_ERROR
			"The selected programming environment does not declare localtime_r")
	endif()
endif()

add_library(syncterm_value_convert STATIC value_convert.c decimal_double.c
	${SYNCTERM_VALUE_CONVERT_TIME_SOURCE})
target_include_directories(syncterm_value_convert PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(syncterm_value_convert PUBLIC syncterm_string_util)
if(NOT MSVC)
	target_link_libraries(syncterm_value_convert PRIVATE m)
endif()
if(SYNCTERM_VALUE_CONVERT_POSIX_DEFINITIONS)
	target_compile_definitions(syncterm_value_convert PRIVATE
		${SYNCTERM_VALUE_CONVERT_POSIX_DEFINITIONS})
endif()
set_target_properties(syncterm_value_convert PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)

function(syncterm_value_convert_warnings target)
	if(MSVC)
		# Use the portable C runtime APIs, not Microsoft-only _s replacements.
		target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(${target} PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(${target} PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
endfunction()
syncterm_value_convert_warnings(syncterm_value_convert)

if(BUILD_TESTING)
	add_executable(value_convert_test tests/value_convert_test.c)
	target_link_libraries(value_convert_test PRIVATE syncterm_value_convert)
	set_target_properties(value_convert_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	syncterm_value_convert_warnings(value_convert_test)
	add_test(NAME value_convert COMMAND value_convert_test)
	set_tests_properties(value_convert PROPERTIES TIMEOUT 30)

	add_executable(value_estimate_test tests/value_estimate_test.c)
	target_link_libraries(value_estimate_test PRIVATE syncterm_value_convert)
	set_target_properties(value_estimate_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	syncterm_value_convert_warnings(value_estimate_test)
	add_test(NAME value_estimate COMMAND value_estimate_test)
	set_tests_properties(value_estimate PROPERTIES TIMEOUT 30)
endif()

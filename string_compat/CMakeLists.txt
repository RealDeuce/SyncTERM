# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(SyncTERMStringCompat LANGUAGES C)
	include(CTest)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/SynctermCheckC17Source.cmake")

option(STRING_COMPAT_TEST_FALLBACKS
	"Build tests that force every string compatibility fallback" OFF)

set(STRING_COMPAT_COMPILE_DEFINITIONS)
set(_string_compat_probe_definitions)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	list(APPEND STRING_COMPAT_COMPILE_DEFINITIONS _GNU_SOURCE)
	list(APPEND _string_compat_probe_definitions -D_GNU_SOURCE)
elseif(WIN32)
	list(APPEND STRING_COMPAT_COMPILE_DEFINITIONS _CRT_NONSTDC_NO_WARNINGS)
	list(APPEND _string_compat_probe_definitions -D_CRT_NONSTDC_NO_WARNINGS)
endif()

set(_string_compat_saved_required_definitions ${CMAKE_REQUIRED_DEFINITIONS})
set(CMAKE_REQUIRED_DEFINITIONS
	${_string_compat_saved_required_definitions}
	${_string_compat_probe_definitions})

syncterm_check_c17_source([=[
#include <stdlib.h>
#include <string.h>

int main(void)
{
	char *(*duplicate)(const char *) = strdup;
	char *copy = duplicate("string");
	int failed = copy == NULL;
	free(copy);
	return failed;
}
]=] STRING_COMPAT_ENV_HAVE_STRDUP)

syncterm_check_c17_source([=[
#include <stdlib.h>
#include <string.h>

int main(void)
{
	char *(*duplicate)(const char *, size_t) = strndup;
	char *copy = duplicate("string", 3);
	int failed = copy == NULL;
	free(copy);
	return failed;
}
]=] STRING_COMPAT_ENV_HAVE_STRNDUP)

syncterm_check_c17_source([=[
#include <stddef.h>
#include <string.h>

int main(void)
{
	size_t (*copy)(char *, const char *, size_t) = strlcpy;
	char destination[4];
	return copy(destination, "string", sizeof(destination)) != 6;
}
]=] STRING_COMPAT_ENV_HAVE_STRLCPY)

syncterm_check_c17_source([=[
#include <stddef.h>
#include <string.h>

int main(void)
{
	size_t (*append)(char *, const char *, size_t) = strlcat;
	char destination[8] = "one";
	return append(destination, "two", sizeof(destination)) != 6;
}
]=] STRING_COMPAT_ENV_HAVE_STRLCAT)

set(CMAKE_REQUIRED_DEFINITIONS
	${_string_compat_saved_required_definitions})
unset(_string_compat_saved_required_definitions)
unset(_string_compat_probe_definitions)

set(STRING_COMPAT_HAVE_STRDUP ${STRING_COMPAT_ENV_HAVE_STRDUP})
set(STRING_COMPAT_HAVE_STRNDUP ${STRING_COMPAT_ENV_HAVE_STRNDUP})
set(STRING_COMPAT_HAVE_STRLCPY ${STRING_COMPAT_ENV_HAVE_STRLCPY})
set(STRING_COMPAT_HAVE_STRLCAT ${STRING_COMPAT_ENV_HAVE_STRLCAT})

configure_file(string_compat_config.h.in string_compat_config.h @ONLY)

add_library(syncterm_string_compat STATIC string_compat.c)
target_include_directories(syncterm_string_compat PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(syncterm_string_compat PUBLIC
	${STRING_COMPAT_COMPILE_DEFINITIONS})
set_target_properties(syncterm_string_compat PROPERTIES
	C_STANDARD 17
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS NO)

function(syncterm_string_compat_warnings target)
	if(MSVC)
		target_compile_options(${target} PRIVATE /W4 /WX)
	else()
		target_compile_options(${target} PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
endfunction()

syncterm_string_compat_warnings(syncterm_string_compat)

if(BUILD_TESTING)
	add_executable(string_compat_test tests/string_compat_test.c)
	target_link_libraries(string_compat_test PRIVATE syncterm_string_compat)
	set_target_properties(string_compat_test PROPERTIES
		C_STANDARD 17
		C_STANDARD_REQUIRED YES
		C_EXTENSIONS NO)
	syncterm_string_compat_warnings(string_compat_test)
	add_test(NAME string_compat COMMAND string_compat_test)

	if(STRING_COMPAT_TEST_FALLBACKS)
		add_executable(string_compat_fallback_test
			string_compat.c
			tests/string_compat_test.c)
		target_include_directories(string_compat_fallback_test PRIVATE
			${CMAKE_CURRENT_SOURCE_DIR}
			${CMAKE_CURRENT_BINARY_DIR})
		target_compile_definitions(string_compat_fallback_test PRIVATE
			${STRING_COMPAT_COMPILE_DEFINITIONS}
			STRING_COMPAT_FORCE_FALLBACKS=1)
		set_target_properties(string_compat_fallback_test PROPERTIES
			C_STANDARD 17
			C_STANDARD_REQUIRED YES
			C_EXTENSIONS NO)
		syncterm_string_compat_warnings(string_compat_fallback_test)
		add_test(NAME string_compat_fallback
			COMMAND string_compat_fallback_test)
	endif()
endif()

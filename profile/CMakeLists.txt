# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(STProfile LANGUAGES C)
	include("${CMAKE_CURRENT_LIST_DIR}/../cmake/SynctermCStandard.cmake")
	include(CTest)
endif()

if(NOT TARGET syncterm_string_compat)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../string_compat"
		"${CMAKE_CURRENT_BINARY_DIR}/string_compat")
endif()
if(NOT TARGET st_codepage)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../codepage"
		"${CMAKE_CURRENT_BINARY_DIR}/codepage")
endif()

add_library(st_profile STATIC st_profile.c)
target_include_directories(st_profile PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_profile PUBLIC st_codepage PRIVATE
	syncterm_string_compat)
set_target_properties(st_profile PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)

function(st_profile_warnings target)
	if(MSVC)
		# Use the portable C runtime APIs, not Microsoft-only _s replacements.
		target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(${target} PRIVATE /W4 /WX)
	else()
		target_compile_options(${target} PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
endfunction()
st_profile_warnings(st_profile)

if(BUILD_TESTING)
	add_executable(st_profile_test tests/st_profile_test.c)
	target_link_libraries(st_profile_test PRIVATE
		st_profile syncterm_string_compat)
	set_target_properties(st_profile_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	st_profile_warnings(st_profile_test)
	add_test(NAME profile COMMAND st_profile_test)
endif()

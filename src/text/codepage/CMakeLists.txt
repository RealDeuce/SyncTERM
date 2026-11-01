# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(STCodepage LANGUAGES C)
	include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCStandard.cmake")
endif()

add_library(st_codepage STATIC st_codepage.c)
target_include_directories(st_codepage PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(st_codepage PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)

if(MSVC)
	target_compile_options(st_codepage PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
else()
	target_compile_options(st_codepage PRIVATE
		-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
endif()

if(BUILD_TESTING)
	add_executable(st_codepage_test tests/st_codepage_test.c)
	target_link_libraries(st_codepage_test PRIVATE st_codepage)
	set_target_properties(st_codepage_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_codepage_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_codepage_test PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME codepage COMMAND st_codepage_test)
endif()

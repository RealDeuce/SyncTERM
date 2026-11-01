# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(STHex LANGUAGES C)
	include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCStandard.cmake")
	include(CTest)
endif()

add_library(st_hex STATIC st_hex.c)
target_include_directories(st_hex PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(st_hex PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)

function(st_hex_warnings target)
	if(MSVC)
		target_compile_options(${target} PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(${target} PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
endfunction()
st_hex_warnings(st_hex)

if(BUILD_TESTING)
	add_executable(st_hex_test tests/st_hex_test.c)
	target_link_libraries(st_hex_test PRIVATE st_hex)
	set_target_properties(st_hex_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	st_hex_warnings(st_hex_test)
	add_test(NAME hex COMMAND st_hex_test)
endif()

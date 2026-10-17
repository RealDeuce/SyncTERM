# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(STBase64 LANGUAGES C)
	include("${CMAKE_CURRENT_LIST_DIR}/../cmake/SynctermCStandard.cmake")
	include(CTest)
endif()

add_library(st_base64 STATIC st_base64.c)
target_include_directories(st_base64 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(st_base64 PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)

if(MSVC)
	target_compile_options(st_base64 PRIVATE /W4 /WX)
else()
	target_compile_options(st_base64 PRIVATE
		-Wall -Wextra -Wpedantic -Werror)
endif()

if(BUILD_TESTING)
	add_executable(st_base64_test tests/st_base64_test.c)
	target_link_libraries(st_base64_test PRIVATE st_base64)
	set_target_properties(st_base64_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_base64_test PRIVATE /W4 /WX)
	else()
		target_compile_options(st_base64_test PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME base64 COMMAND st_base64_test)
endif()

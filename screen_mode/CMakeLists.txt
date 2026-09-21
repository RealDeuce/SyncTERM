# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(STScreenMode LANGUAGES C)
	include(CTest)
endif()

add_library(st_screen_mode STATIC st_screen_mode.c)
target_include_directories(st_screen_mode PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_screen_mode PUBLIC st_console st_profile)
set_target_properties(st_screen_mode PROPERTIES
	C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)

if(MSVC)
	target_compile_options(st_screen_mode PRIVATE /W4 /WX)
else()
	target_compile_options(st_screen_mode PRIVATE
		-Wall -Wextra -Wpedantic -Werror)
endif()

if(BUILD_TESTING)
	add_executable(st_screen_mode_test tests/st_screen_mode_test.c)
	target_link_libraries(st_screen_mode_test PRIVATE st_screen_mode)
	set_target_properties(st_screen_mode_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_screen_mode_test PRIVATE /W4 /WX)
	else()
		target_compile_options(st_screen_mode_test PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME screen_mode COMMAND st_screen_mode_test)
endif()

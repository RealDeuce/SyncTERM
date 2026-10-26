# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

add_library(st_unifont STATIC generated/st_unifont_data.c)
target_include_directories(st_unifont PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_unifont PUBLIC st_console)
set_target_properties(st_unifont PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(MSVC)
	target_compile_options(st_unifont PRIVATE /W4 /WX)
else()
	target_compile_options(st_unifont PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(BUILD_TESTING)
	add_executable(st_unifont_test tests/st_unifont_test.c)
	target_link_libraries(st_unifont_test PRIVATE st_unifont st_fonts)
	set_target_properties(st_unifont_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_unifont_test PRIVATE /W4 /WX)
	else()
		target_compile_options(st_unifont_test PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME unifont COMMAND st_unifont_test)
endif()

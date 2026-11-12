# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
add_library(st_big STATIC st_big_number.c)
target_include_directories(st_big PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_big PUBLIC syncterm_io_error)
set_target_properties(st_big PROPERTIES
	C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(MSVC)
	target_compile_options(st_big PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
else()
	target_compile_options(st_big PRIVATE
		-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
endif()

if(BUILD_TESTING)
	add_executable(st_big_number_test tests/st_big_number_test.c)
	target_link_libraries(st_big_number_test PRIVATE st_big st_graphics_protocol)
	set_target_properties(st_big_number_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_big_number_test PRIVATE /W4)
	else()
		target_compile_options(st_big_number_test PRIVATE -Wall -Wextra -Wpedantic)
	endif()
	add_test(NAME big_number COMMAND st_big_number_test)
	set_tests_properties(big_number PROPERTIES TIMEOUT 20)
endif()

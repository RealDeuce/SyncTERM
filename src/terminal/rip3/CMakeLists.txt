# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
add_library(st_rip3 STATIC st_rip3_number.c)
target_include_directories(st_rip3 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_rip3 PUBLIC syncterm_io_error)
set_target_properties(st_rip3 PROPERTIES
	C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(MSVC)
	target_compile_options(st_rip3 PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
else()
	target_compile_options(st_rip3 PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
endif()

if(BUILD_TESTING)
	add_executable(st_rip3_number_test tests/st_rip3_number_test.c)
	target_link_libraries(st_rip3_number_test PRIVATE st_rip3)
	set_target_properties(st_rip3_number_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_rip3_number_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_rip3_number_test PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME rip3_number COMMAND st_rip3_number_test)
endif()

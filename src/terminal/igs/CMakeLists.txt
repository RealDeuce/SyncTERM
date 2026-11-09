# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
add_library(st_igs STATIC st_igs_image.c)
target_include_directories(st_igs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_igs PUBLIC st_pixels syncterm_io_error)
set_target_properties(st_igs PROPERTIES
	C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(MSVC)
	target_compile_options(st_igs PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
else()
	target_compile_options(st_igs PRIVATE
		-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
endif()

if(BUILD_TESTING)
	foreach(part IN ITEMS image)
		add_executable(st_igs_${part}_test tests/st_igs_${part}_test.c)
		target_link_libraries(st_igs_${part}_test PRIVATE st_igs)
		set_target_properties(st_igs_${part}_test PROPERTIES
			C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
		if(MSVC)
			target_compile_options(st_igs_${part}_test PRIVATE /W4)
		else()
			target_compile_options(st_igs_${part}_test PRIVATE -Wall -Wextra -Wpedantic)
		endif()
		add_test(NAME igs_${part} COMMAND st_igs_${part}_test)
		set_tests_properties(igs_${part} PROPERTIES TIMEOUT 20)
	endforeach()
endif()

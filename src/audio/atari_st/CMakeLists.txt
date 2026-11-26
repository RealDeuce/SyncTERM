# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
add_library(st_atari_st_audio STATIC st_igs_gist.c st_igs_psg.c)
target_include_directories(st_atari_st_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(st_atari_st_audio PROPERTIES
	C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(MSVC)
	target_compile_options(st_atari_st_audio PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
else()
	target_compile_options(st_atari_st_audio PRIVATE
		-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
endif()

if(BUILD_TESTING)
	foreach(part IN ITEMS gist psg)
		add_executable(st_igs_${part}_test tests/st_igs_${part}_test.c)
		target_link_libraries(st_igs_${part}_test PRIVATE st_atari_st_audio)
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

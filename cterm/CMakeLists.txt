# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

add_library(st_cterm STATIC
	cterm.c
	cterm_atascii.c
	cterm_cterm.c
	cterm_dec.c
	cterm_ecma48.c
	cterm_render.c
	cterm_petscii.c
	cterm_prestel.c
	st_cterm.c
	cterm_vt52.c)
target_include_directories(st_cterm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_cterm PUBLIC st_console st_codepage st_font_catalog st_hyperlink
	st_audio st_base64 st_time st_utf8 st_unicode_text)
set_target_properties(st_cterm PROPERTIES C_STANDARD 17
	C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(MSVC)
	target_compile_options(st_cterm PRIVATE /W4 /WX)
else()
	target_compile_options(st_cterm PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(BUILD_TESTING)
	add_executable(st_cterm_test tests/st_cterm_test.c)
	target_link_libraries(st_cterm_test PRIVATE st_cterm)
	set_target_properties(st_cterm_test PROPERTIES C_STANDARD 17
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_test PRIVATE /W4 /WX)
	else()
		target_compile_options(st_cterm_test PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME cterm COMMAND st_cterm_test)
endif()

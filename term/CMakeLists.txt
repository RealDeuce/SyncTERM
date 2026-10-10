# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

add_library(st_term STATIC st_term.c)
target_include_directories(st_term PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_term PUBLIC st_connection st_cterm st_font_catalog
	st_session_history PRIVATE st_audio st_unicode_text st_utf8
	syncterm_io_error)
set_target_properties(st_term PROPERTIES C_STANDARD 17
	C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(MSVC)
	target_compile_options(st_term PRIVATE /W4 /WX)
else()
	target_compile_options(st_term PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(BUILD_TESTING)
	add_executable(st_term_test tests/st_term_test.c)
	target_link_libraries(st_term_test PRIVATE st_term)
	set_target_properties(st_term_test PROPERTIES C_STANDARD 17
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_term_test PRIVATE /W4 /WX)
	else()
		target_compile_options(st_term_test PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME term COMMAND st_term_test)
endif()

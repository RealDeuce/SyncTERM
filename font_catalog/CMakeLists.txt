# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

add_library(st_font_catalog STATIC st_font_catalog.c)
target_include_directories(st_font_catalog PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_font_catalog PUBLIC st_console st_codepage
	PRIVATE st_fonts syncterm_filesystem syncterm_io_error)
set_target_properties(st_font_catalog PROPERTIES C_STANDARD 17
	C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(MSVC)
	target_compile_options(st_font_catalog PRIVATE /W4 /WX)
else()
	target_compile_options(st_font_catalog PRIVATE
		-Wall -Wextra -Wpedantic -Werror)
endif()

if(BUILD_TESTING)
	add_executable(st_font_catalog_test tests/st_font_catalog_test.c)
	target_link_libraries(st_font_catalog_test PRIVATE st_font_catalog)
	set_target_properties(st_font_catalog_test PROPERTIES C_STANDARD 17
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_font_catalog_test PRIVATE /W4 /WX)
	else()
		target_compile_options(st_font_catalog_test PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME font_catalog COMMAND st_font_catalog_test)
endif()

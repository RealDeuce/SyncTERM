# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermTestRunner.cmake")

add_library(st_font_catalog STATIC st_font_catalog.c)
target_include_directories(st_font_catalog PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_font_catalog PUBLIC st_console st_codepage st_amiga_font
	PRIVATE st_fonts syncterm_filesystem syncterm_io_error)
set_target_properties(st_font_catalog PROPERTIES C_STANDARD ${SYNCTERM_C_STANDARD}
	C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(MSVC)
	# Use the portable C runtime APIs, not Microsoft-only _s replacements.
	target_compile_definitions(st_font_catalog PRIVATE _CRT_SECURE_NO_WARNINGS)
	target_compile_options(st_font_catalog PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
else()
	target_compile_options(st_font_catalog PRIVATE
		-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
endif()

if(BUILD_TESTING)
	syncterm_add_test_case(st_terminal_tests st_font_catalog_test tests/st_font_catalog_test.c)
	target_link_libraries(st_font_catalog_test PRIVATE st_font_catalog st_fonts)
	set_target_properties(st_font_catalog_test PROPERTIES C_STANDARD ${SYNCTERM_C_STANDARD}
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_font_catalog_test PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_font_catalog_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_font_catalog_test PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME font_catalog COMMAND st_terminal_tests st_font_catalog_test)
endif()

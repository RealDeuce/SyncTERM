# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermTestRunner.cmake")
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(STEmojiCatalog LANGUAGES C)
	include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCStandard.cmake")
	include(CTest)
endif()

if(NOT TARGET st_unicode_compare)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../unicode_compare"
		"${CMAKE_CURRENT_BINARY_DIR}/unicode_compare")
endif()
add_library(st_emoji_catalog STATIC st_emoji_catalog.c st_emoji_locale.c
	generated/st_emoji_catalog_data.c generated/st_emoji_locale_data.c)
target_include_directories(st_emoji_catalog PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_emoji_catalog PRIVATE st_unicode_compare)
set_target_properties(st_emoji_catalog PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(MSVC)
	target_compile_options(st_emoji_catalog PRIVATE /W4 /WX)
else()
	target_compile_options(st_emoji_catalog PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
if(BUILD_TESTING)
	find_package(Python3 COMPONENTS Interpreter QUIET)
	if(Python3_Interpreter_FOUND)
		add_test(NAME emoji_catalog_generated COMMAND ${Python3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/update_emoji_catalog.py --check)
		add_test(NAME emoji_catalog_generator COMMAND ${Python3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/tests/test_generator.py)
		add_test(NAME emoji_locales_generated COMMAND ${Python3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/update_emoji_locales.py --check)
		add_test(NAME emoji_locales_generator COMMAND ${Python3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/tests/test_locales.py)
	endif()
	syncterm_add_test_case(st_terminal_tests st_emoji_locale_test tests/st_emoji_locale_test.c)
	target_link_libraries(st_emoji_locale_test PRIVATE st_emoji_catalog st_unicode_compare syncterm_io_error)
	set_target_properties(st_emoji_locale_test PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_emoji_locale_test PRIVATE /W4 /WX /utf-8)
	else()
		target_compile_options(st_emoji_locale_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME emoji_locales COMMAND st_terminal_tests st_emoji_locale_test)
endif()

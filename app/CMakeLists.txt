# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(SyncTERMApplication LANGUAGES C)
endif()

add_library(st_app_startup STATIC
	st_command_line.c
	st_console_startup.c)
target_include_directories(st_app_startup PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_app_startup PUBLIC st_console st_profile st_settings
	syncterm_io_error)
set_target_properties(st_app_startup PROPERTIES
	C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)

add_executable(syncterm_app syncterm.c st_terminfo.c)
set_target_properties(syncterm_app PROPERTIES
	OUTPUT_NAME syncterm
	C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
target_compile_definitions(syncterm_app PRIVATE
	$<$<CONFIG:Debug>:ST_DEBUG_BUILD>)
target_link_libraries(syncterm_app PRIVATE st_wren_host st_settings
	st_app_startup st_fonts)
if(SYNCTERM_ENABLE_UNIFONT)
	target_link_libraries(syncterm_app PRIVATE st_unifont)
	target_compile_definitions(syncterm_app PRIVATE ST_HAVE_UNIFONT=1)
endif()
if(TARGET st_console_main)
	target_link_libraries(syncterm_app PRIVATE st_console_main)
endif()
if(MSVC)
	target_compile_options(st_app_startup PRIVATE /W4 /WX)
	target_compile_options(syncterm_app PRIVATE /W4 /WX)
else()
	target_compile_options(st_app_startup PRIVATE
		-Wall -Wextra -Wpedantic -Werror)
	target_compile_options(syncterm_app PRIVATE
		-Wall -Wextra -Wpedantic -Werror)
endif()

if(BUILD_TESTING)
	add_test(NAME syncterm_help COMMAND syncterm_app --help)
	set_tests_properties(syncterm_help PROPERTIES
		PASS_REGULAR_EXPRESSION "usage:.*syncterm.*options")
	add_test(NAME syncterm_version COMMAND syncterm_app -v)
	set_tests_properties(syncterm_version PROPERTIES
		PASS_REGULAR_EXPRESSION "SyncTERM 1\\.10a")
	add_test(NAME syncterm_terminfo COMMAND syncterm_app -T)
	set_tests_properties(syncterm_terminfo PROPERTIES
		PASS_REGULAR_EXPRESSION "syncterm\\|SyncTERM")
	add_executable(st_app_startup_test tests/st_app_startup_test.c)
	target_link_libraries(st_app_startup_test PRIVATE st_app_startup)
	set_target_properties(st_app_startup_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_app_startup_test PRIVATE /W4 /WX)
	else()
		target_compile_options(st_app_startup_test PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME app_startup COMMAND st_app_startup_test)
endif()

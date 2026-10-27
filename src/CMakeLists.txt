# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(SyncTERMApplication LANGUAGES C)
	include("${CMAKE_CURRENT_LIST_DIR}/../cmake/SynctermCStandard.cmake")
endif()

add_library(st_app_startup STATIC
	st_app_state.c
	st_command_line.c
	st_console_startup.c)
target_include_directories(st_app_startup PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_app_startup PUBLIC st_console st_profile st_settings
	st_screen_mode syncterm_io_error)
set_target_properties(st_app_startup PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)

add_library(st_application STATIC syncterm.c st_syncterm_icon.c st_terminfo.c)
target_include_directories(st_application PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(st_application PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
target_compile_definitions(st_application PRIVATE
	$<$<CONFIG:Debug>:ST_DEBUG_BUILD>)
target_link_libraries(st_application PUBLIC st_console st_audio
	PRIVATE st_wren_host st_settings
	st_app_startup st_audio st_fonts st_font_catalog st_term
	st_session_history st_hyperlink st_bbs_list)
target_link_libraries(st_application PRIVATE st_time syncterm_threads
	syncterm_value_convert syncterm_filesystem syncterm_path)
syncterm_target_atomics(st_application)
if(SYNCTERM_ENABLE_UNIFONT)
	target_link_libraries(st_application PRIVATE st_unifont)
	target_compile_definitions(st_application PRIVATE ST_HAVE_UNIFONT=1)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/SynctermSharedApplication.cmake")
syncterm_shared_dependencies(st_application)

if(WIN32)
	add_library(syncterm_core SHARED st_libretro.c st_libretro_input.c
		st_windows_application.c)
	set_target_properties(syncterm_core PROPERTIES
		OUTPUT_NAME syncterm PREFIX "" PDB_NAME syncterm-core
		WINDOWS_EXPORT_ALL_SYMBOLS OFF)
	include(GenerateExportHeader)
	generate_export_header(syncterm_core BASE_NAME ST_APPLICATION
		EXPORT_MACRO_NAME ST_APPLICATION_API
		EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/st_application_export.h")
	target_include_directories(syncterm_core PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
	if(TARGET st_console_main)
		target_compile_definitions(syncterm_core PRIVATE ST_APP_USE_SDL_DISPATCH=1)
	endif()
	add_executable(syncterm_app WIN32 st_windows_launcher.c)
	target_compile_definitions(syncterm_app PRIVATE ST_LAUNCHER_GUI=1)
	add_executable(syncterm_console st_windows_launcher.c)
	set_target_properties(syncterm_console PROPERTIES
		OUTPUT_NAME syncterm SUFFIX .com PDB_NAME syncterm-console)
	set_target_properties(syncterm_app PROPERTIES PDB_NAME syncterm-gui)
	foreach(_launcher syncterm_app syncterm_console)
		target_include_directories(${_launcher} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
		target_link_libraries(${_launcher} PRIVATE syncterm_core st_windows_utf shell32)
		set_target_properties(${_launcher} PROPERTIES
			C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	endforeach()
	set(_cli syncterm_console)
else()
	add_library(syncterm_core MODULE st_libretro.c st_libretro_input.c)
	set_target_properties(syncterm_core PROPERTIES OUTPUT_NAME syncterm_libretro PREFIX "")
	if(APPLE)
		set_target_properties(syncterm_core PROPERTIES SUFFIX .dylib)
	endif()
	add_executable(syncterm_app main.c)
	target_link_libraries(syncterm_app PRIVATE st_application)
	if(TARGET st_console_main)
		target_link_libraries(syncterm_app PRIVATE st_console_main)
	endif()
	set(_cli syncterm_app)
endif()
set_target_properties(syncterm_app PROPERTIES OUTPUT_NAME syncterm
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
target_link_libraries(syncterm_core PRIVATE st_application syncterm_string_compat
	syncterm_path syncterm_filesystem syncterm_threads st_time)
syncterm_target_atomics(syncterm_core)
set_target_properties(syncterm_core PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO
	C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden)
# Keep application products independent of the source directory layout.
set(_st_products syncterm_app syncterm_core)
if(WIN32)
	list(APPEND _st_products syncterm_console)
endif()
foreach(_product IN LISTS _st_products)
	set_target_properties(${_product} PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
		LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
		PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endforeach()
set(_artifacts "$<TARGET_FILE:syncterm_app>\n$<TARGET_FILE:syncterm_core>\n")
if(WIN32)
	string(APPEND _artifacts "$<TARGET_FILE:syncterm_console>\n")
endif()
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/syncterm-artifacts-$<CONFIG>.txt" CONTENT "${_artifacts}")
if(MSVC)
	# Use the portable C runtime APIs, not Microsoft-only _s replacements.
	target_compile_definitions(st_app_startup PRIVATE _CRT_SECURE_NO_WARNINGS)
	target_compile_options(st_app_startup PRIVATE /W4 /WX)
	target_compile_options(syncterm_app PRIVATE /W4 /WX)
	target_compile_options(st_application PRIVATE /W4 /WX)
	target_compile_options(syncterm_core PRIVATE /W4 /WX)
	if(WIN32)
		target_compile_options(syncterm_console PRIVATE /W4 /WX)
	endif()
else()
	target_compile_options(st_app_startup PRIVATE
		-Wall -Wextra -Wpedantic -Werror)
	target_compile_options(syncterm_app PRIVATE
		-Wall -Wextra -Wpedantic -Werror)
	target_compile_options(st_application PRIVATE -Wall -Wextra -Wpedantic -Werror)
	target_compile_options(syncterm_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
	if(WIN32)
		target_compile_options(syncterm_console PRIVATE -Wall -Wextra -Wpedantic -Werror)
	endif()
endif()

if(BUILD_TESTING)
	if(ST_CONSOLE_TEST)
		find_package(Python3 REQUIRED COMPONENTS Interpreter)
		if(Python3_Interpreter_FOUND)
			foreach(_variant basic cp437 utf8 protocol transfer)
				add_test(NAME syncterm_control_${_variant}
					COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_test_control_test.py
					$<TARGET_FILE:syncterm_app> ${_variant})
				set_tests_properties(syncterm_control_${_variant} PROPERTIES TIMEOUT 90)
			endforeach()
			if(WIN32)
				add_test(NAME syncterm_control_console
					COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_test_control_test.py
					$<TARGET_FILE:syncterm_console> basic)
				set_tests_properties(syncterm_control_console PROPERTIES TIMEOUT 90)
			endif()
		endif()
	endif()
	add_executable(st_libretro_input_test tests/st_libretro_input_test.c st_libretro_input.c)
	target_link_libraries(st_libretro_input_test PRIVATE st_console)
	target_include_directories(st_libretro_input_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
	target_compile_definitions(st_libretro_input_test PRIVATE RETRO_IMPORT_SYMBOLS)
	add_test(NAME libretro_input COMMAND st_libretro_input_test)
	add_executable(st_libretro_test tests/st_libretro_test.c)
	target_include_directories(st_libretro_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
		${CMAKE_CURRENT_BINARY_DIR}/..)
	target_link_libraries(st_libretro_test PRIVATE syncterm_filesystem syncterm_threads ${CMAKE_DL_LIBS})
	if(WIN32)
		target_link_libraries(st_libretro_test PRIVATE st_windows_utf)
	endif()
	target_compile_definitions(st_libretro_test PRIVATE
		ST_LIBRETRO_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/libretro-test")
	add_dependencies(st_libretro_test syncterm_core)
	foreach(_test st_libretro_test st_libretro_input_test)
		set_target_properties(${_test} PROPERTIES
			C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
		if(MSVC)
			target_compile_definitions(${_test} PRIVATE _CRT_SECURE_NO_WARNINGS)
			target_compile_options(${_test} PRIVATE /W4 /WX)
		else()
			target_compile_options(${_test} PRIVATE -Wall -Wextra -Wpedantic -Werror)
		endif()
	endforeach()
	add_test(NAME libretro_core COMMAND st_libretro_test $<TARGET_FILE:syncterm_core>)
	set_tests_properties(libretro_core PROPERTIES TIMEOUT 180 RESOURCE_LOCK libretro_files)
	add_test(NAME syncterm_help COMMAND ${_cli} --help)
	set_tests_properties(syncterm_help PROPERTIES
		PASS_REGULAR_EXPRESSION "usage:.*syncterm.*options")
	add_test(NAME syncterm_version COMMAND ${_cli} -v)
	set_tests_properties(syncterm_version PROPERTIES
		PASS_REGULAR_EXPRESSION "SyncTERM 2\\.0a")
	add_test(NAME syncterm_terminfo COMMAND ${_cli} -T)
	set_tests_properties(syncterm_terminfo PROPERTIES
		PASS_REGULAR_EXPRESSION "syncterm\\|SyncTERM")
	if(NOT WIN32)
		find_package(Python3 QUIET COMPONENTS Interpreter)
		if(Python3_Interpreter_FOUND)
			foreach(_mode IN ITEMS A C F I)
				if(_mode STREQUAL "A" OR ST_CONSOLE_HAVE_CURSES_H)
					add_test(NAME syncterm_text_startup_${_mode}
						COMMAND ${Python3_EXECUTABLE}
						${CMAKE_CURRENT_SOURCE_DIR}/tests/st_text_startup_test.py
						$<TARGET_FILE:${_cli}> ${_mode})
					set_tests_properties(syncterm_text_startup_${_mode}
						PROPERTIES TIMEOUT 25)
					add_test(NAME syncterm_text_scaling_${_mode}
						COMMAND ${Python3_EXECUTABLE}
						${CMAKE_CURRENT_SOURCE_DIR}/tests/st_text_startup_test.py
						$<TARGET_FILE:${_cli}> ${_mode} scaling)
					set_tests_properties(syncterm_text_scaling_${_mode}
						PROPERTIES TIMEOUT 25)
					foreach(_layout IN ITEMS center left)
						add_test(NAME syncterm_text_${_layout}_${_mode}
							COMMAND ${Python3_EXECUTABLE}
							${CMAKE_CURRENT_SOURCE_DIR}/tests/st_text_startup_test.py
							$<TARGET_FILE:${_cli}> ${_mode} ${_layout})
						set_tests_properties(syncterm_text_${_layout}_${_mode}
							PROPERTIES TIMEOUT 25)
					endforeach()
				endif()
			endforeach()
		endif()
	endif()
	add_executable(st_app_startup_test tests/st_app_startup_test.c)
	target_link_libraries(st_app_startup_test PRIVATE st_app_startup)
	set_target_properties(st_app_startup_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_app_startup_test PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_app_startup_test PRIVATE /W4 /WX)
	else()
		target_compile_options(st_app_startup_test PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME app_startup COMMAND st_app_startup_test)
	if(TARGET st_console_x11_test)
		find_package(X11 REQUIRED)
		add_executable(st_app_scaling_test tests/st_app_scaling_test.c)
		target_link_libraries(st_app_scaling_test PRIVATE st_application
			X11::X11 syncterm_ini syncterm_filesystem syncterm_threads)
		set_target_properties(st_app_scaling_test PROPERTIES
			C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
		target_compile_options(st_app_scaling_test PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
		add_test(NAME app_scaling COMMAND st_app_scaling_test
			${CMAKE_CURRENT_BINARY_DIR}/scaling-test)
		set_tests_properties(app_scaling PROPERTIES TIMEOUT 30
			SKIP_RETURN_CODE 77 RESOURCE_LOCK x11_display)
	endif()
	if(NOT WIN32)
		add_executable(st_app_session_mode_test
			tests/st_app_session_mode_test.c st_syncterm_icon.c st_terminfo.c)
		target_link_libraries(st_app_session_mode_test PRIVATE st_wren_host
			st_settings st_app_startup st_audio st_fonts st_font_catalog st_term
			st_session_history st_hyperlink st_bbs_list st_time syncterm_threads syncterm_path
			syncterm_filesystem syncterm_value_convert)
		target_compile_definitions(st_app_session_mode_test PRIVATE
			ST_APP_SESSION_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/session-mode-test")
		if(SYNCTERM_ENABLE_UNIFONT)
			target_link_libraries(st_app_session_mode_test PRIVATE st_unifont)
			target_compile_definitions(st_app_session_mode_test PRIVATE ST_HAVE_UNIFONT=1)
		endif()
		set_target_properties(st_app_session_mode_test PROPERTIES
			C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
		target_compile_options(st_app_session_mode_test PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
		add_test(NAME app_session_mode COMMAND st_app_session_mode_test)
		set_tests_properties(app_session_mode PROPERTIES TIMEOUT 60)
	endif()
	add_executable(st_command_line_empty_backend_test
		tests/st_command_line_empty_backend_test.c)
	target_link_libraries(st_command_line_empty_backend_test PRIVATE
		st_app_startup)
	set_target_properties(st_command_line_empty_backend_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_command_line_empty_backend_test PRIVATE
			/W4 /WX)
	else()
		target_compile_options(st_command_line_empty_backend_test PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME command_line_empty_backend
		COMMAND st_command_line_empty_backend_test)
endif()

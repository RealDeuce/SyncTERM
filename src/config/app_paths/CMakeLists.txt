# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(SyncTERMAppPaths LANGUAGES C)
	include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCStandard.cmake")
	include(CTest)
endif()

if(NOT TARGET syncterm_filesystem)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../io/filesystem"
		"${CMAKE_CURRENT_BINARY_DIR}/filesystem")
endif()
if(NOT TARGET syncterm_string_compat)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../base/string_compat"
		"${CMAKE_CURRENT_BINARY_DIR}/string_compat")
endif()

if(WIN32)
	set(ST_APP_PATHS_PLATFORM_SOURCE st_app_paths_windows.c)
elseif(APPLE)
	set(ST_APP_PATHS_PLATFORM_SOURCE st_app_paths_macos.c)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Haiku")
	set(ST_APP_PATHS_PLATFORM_SOURCE st_app_paths_haiku.c)
else()
	set(ST_APP_PATHS_PLATFORM_SOURCE st_app_paths_posix.c)
endif()

add_library(st_app_paths STATIC st_app_paths.c
	${ST_APP_PATHS_PLATFORM_SOURCE})
target_include_directories(st_app_paths PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_app_paths PUBLIC syncterm_filesystem
	syncterm_io_error syncterm_path PRIVATE syncterm_string_compat)
if(WIN32)
	if(NOT TARGET st_windows_utf)
		add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../text/windows_utf"
			"${CMAKE_CURRENT_BINARY_DIR}/windows_utf")
	endif()
	target_link_libraries(st_app_paths PRIVATE shell32 ole32)
	target_link_libraries(st_app_paths PRIVATE st_windows_utf)
endif()
set_target_properties(st_app_paths PROPERTIES C_STANDARD ${SYNCTERM_C_STANDARD}
	C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(MSVC)
	# Use the portable C runtime APIs, not Microsoft-only _s replacements.
	target_compile_definitions(st_app_paths PRIVATE _CRT_SECURE_NO_WARNINGS)
	target_compile_options(st_app_paths PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
else()
	target_compile_options(st_app_paths PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
endif()

if(BUILD_TESTING)
	add_executable(st_app_paths_test tests/st_app_paths_test.c)
	target_link_libraries(st_app_paths_test PRIVATE st_app_paths
		syncterm_filesystem)
	if(WIN32)
		target_link_libraries(st_app_paths_test PRIVATE shell32 ole32)
	endif()
	target_compile_definitions(st_app_paths_test PRIVATE
		ST_APP_PATHS_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/paths-test")
	set_target_properties(st_app_paths_test PROPERTIES C_STANDARD ${SYNCTERM_C_STANDARD}
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_app_paths_test PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_app_paths_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_app_paths_test PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME app_paths COMMAND st_app_paths_test)
	if(NOT WIN32 AND NOT APPLE AND
	    NOT CMAKE_SYSTEM_NAME STREQUAL "Haiku")
		add_executable(st_app_paths_xdg_test
			tests/st_app_paths_xdg_test.c)
		target_link_libraries(st_app_paths_xdg_test PRIVATE st_app_paths)
		target_compile_definitions(st_app_paths_xdg_test PRIVATE
			ST_APP_PATHS_XDG_TEST_HOME="${CMAKE_CURRENT_BINARY_DIR}/xdg-home")
		set_target_properties(st_app_paths_xdg_test PROPERTIES
			C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
		if(MSVC)
			target_compile_definitions(st_app_paths_xdg_test PRIVATE _CRT_SECURE_NO_WARNINGS)
			target_compile_options(st_app_paths_xdg_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
		else()
			target_compile_options(st_app_paths_xdg_test PRIVATE
				-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
		endif()
		add_test(NAME app_paths_xdg COMMAND st_app_paths_xdg_test)
		set_tests_properties(app_paths_xdg PROPERTIES ENVIRONMENT
			"HOME=${CMAKE_CURRENT_BINARY_DIR}/xdg-home;XDG_CONFIG_HOME=relative;XDG_CACHE_HOME=relative;XDG_DATA_HOME=relative;XDG_DOWNLOAD_DIR=relative-download")
	endif()
endif()

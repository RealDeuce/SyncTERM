# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(STTheme LANGUAGES C)
	include(CTest)
endif()

if(NOT TARGET syncterm_ini)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../ini"
		"${CMAKE_CURRENT_BINARY_DIR}/ini")
endif()
if(NOT TARGET st_settings)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../settings"
		"${CMAKE_CURRENT_BINARY_DIR}/settings")
endif()
if(NOT TARGET st_app_paths)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../app_paths"
		"${CMAKE_CURRENT_BINARY_DIR}/app_paths")
endif()
if(NOT TARGET st_hex)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../hex"
		"${CMAKE_CURRENT_BINARY_DIR}/hex")
endif()
if(NOT TARGET st_utf8)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../utf8"
		"${CMAKE_CURRENT_BINARY_DIR}/utf8")
endif()
if(NOT TARGET st_codepage)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../codepage"
		"${CMAKE_CURRENT_BINARY_DIR}/codepage")
endif()
if(NOT TARGET st_webget)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../webget"
		"${CMAKE_CURRENT_BINARY_DIR}/webget")
endif()

if(WIN32)
	set(_st_theme_directory theme_directory_windows.c)
else()
	set(_st_theme_directory theme_directory_posix.c)
endif()

add_library(st_theme STATIC st_theme.c st_theme_document.c st_theme_cloud.c
	${_st_theme_directory})
target_include_directories(st_theme PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_theme
	PUBLIC st_settings
	PRIVATE st_app_paths st_codepage st_hex st_utf8 st_webget xptls_crypto syncterm_ini
		syncterm_filesystem syncterm_path syncterm_string_compat
		syncterm_string_util)
if(WIN32)
	target_link_libraries(st_theme PRIVATE st_windows_utf)
endif()
set_target_properties(st_theme PROPERTIES
	C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)

if(MSVC)
	target_compile_options(st_theme PRIVATE /W4 /WX)
else()
	target_compile_options(st_theme PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(BUILD_TESTING)
	add_executable(st_theme_test tests/st_theme_test.c)
	target_link_libraries(st_theme_test PRIVATE st_theme xptls_crypto
		syncterm_filesystem syncterm_string_compat)
	target_compile_definitions(st_theme_test PRIVATE
		ST_THEME_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/theme-test")
	set_target_properties(st_theme_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_theme_test PRIVATE /W4 /WX)
	else()
		target_compile_options(st_theme_test PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME theme COMMAND st_theme_test)
endif()

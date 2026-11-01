# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(SyncTERMINI LANGUAGES C)
	include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCStandard.cmake")
	include(CTest)
endif()

foreach(_st_component_path IN ITEMS base/array_list io/file_map base/io_error base/name_value_list
	base/str_list base/string_compat base/string_util base/value_convert)
	get_filename_component(_dependency "${_st_component_path}" NAME)
	if(NOT TARGET syncterm_${_dependency})
		add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../${_st_component_path}"
			"${CMAKE_CURRENT_BINARY_DIR}/${_dependency}")
	endif()
endforeach()
if(NOT TARGET st_hex)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../base/hex"
		"${CMAKE_CURRENT_BINARY_DIR}/hex")
endif()
if(NOT TARGET st_unicode_text)
	if(NOT TARGET st_utf8)
		add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../text/utf8"
			"${CMAKE_CURRENT_BINARY_DIR}/utf8")
	endif()
	if(NOT TARGET st_unicode_width)
		add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../text/unicode_width"
			"${CMAKE_CURRENT_BINARY_DIR}/unicode_width")
	endif()
	if(NOT TARGET st_unicode_normalization)
		add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../text/unicode_normalization"
			"${CMAKE_CURRENT_BINARY_DIR}/unicode_normalization")
	endif()
	if(NOT TARGET st_unicode_grapheme)
		add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../text/unicode_grapheme"
			"${CMAKE_CURRENT_BINARY_DIR}/unicode_grapheme")
	endif()
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../text/unicode_text"
		"${CMAKE_CURRENT_BINARY_DIR}/unicode_text")
endif()
if(NOT TARGET st_unicode_compare)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../text/unicode_compare"
		"${CMAKE_CURRENT_BINARY_DIR}/unicode_compare")
endif()

add_library(syncterm_ini STATIC ini.c)
target_include_directories(syncterm_ini PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(syncterm_ini
	PUBLIC syncterm_io_error syncterm_name_value_list syncterm_str_list
	PRIVATE syncterm_array_list syncterm_file_map st_hex syncterm_string_compat
		syncterm_string_util syncterm_value_convert st_unicode_text
		st_unicode_compare)
set_target_properties(syncterm_ini PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)

function(syncterm_ini_warnings target)
	if(MSVC)
		# Use the portable C runtime APIs, not Microsoft-only _s replacements.
		target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(${target} PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(${target} PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
endfunction()
syncterm_ini_warnings(syncterm_ini)

if(BUILD_TESTING)
	add_executable(ini_test tests/ini_test.c)
	target_link_libraries(ini_test PRIVATE syncterm_ini)
	set_target_properties(ini_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	syncterm_ini_warnings(ini_test)
	add_test(NAME ini COMMAND ini_test)

	add_executable(ini_interface_test tests/ini_interface_test.c)
	target_link_libraries(ini_interface_test PRIVATE syncterm_ini)
	set_target_properties(ini_interface_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	syncterm_ini_warnings(ini_interface_test)
	add_test(NAME ini_interface COMMAND ini_interface_test)
endif()

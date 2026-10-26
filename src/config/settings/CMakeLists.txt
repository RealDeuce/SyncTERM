# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(STSettings LANGUAGES C)
	include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCStandard.cmake")
	include(CTest)
endif()

foreach(_st_component_path IN ITEMS base/string_compat base/string_util base/name_value_list)
	get_filename_component(_dependency "${_st_component_path}" NAME)
	if(NOT TARGET syncterm_${_dependency})
		add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../${_st_component_path}"
			"${CMAKE_CURRENT_BINARY_DIR}/${_dependency}")
	endif()
endforeach()

foreach(_st_component_path IN ITEMS config/app_paths io/file_lock)
	get_filename_component(_dependency "${_st_component_path}" NAME)
	if(NOT TARGET st_${_dependency})
		add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../${_st_component_path}"
			"${CMAKE_CURRENT_BINARY_DIR}/${_dependency}")
	endif()
endforeach()
foreach(_st_component_path IN ITEMS io/filesystem config/ini base/io_error)
	get_filename_component(_dependency "${_st_component_path}" NAME)
	if(NOT TARGET syncterm_${_dependency})
		add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../${_st_component_path}"
			"${CMAKE_CURRENT_BINARY_DIR}/${_dependency}")
	endif()
endforeach()
if(NOT TARGET st_unicode_compare)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../text/unicode_compare"
		"${CMAKE_CURRENT_BINARY_DIR}/unicode_compare")
endif()

add_library(st_settings STATIC st_settings.c st_settings_store.c)
target_include_directories(st_settings PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_settings PUBLIC
	syncterm_name_value_list st_app_paths syncterm_io_error PRIVATE
	st_file_lock syncterm_filesystem syncterm_ini syncterm_string_compat
	syncterm_string_util st_unicode_compare)
set_target_properties(st_settings PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)

function(st_settings_warnings target)
	if(MSVC)
		# Use the portable C runtime APIs, not Microsoft-only _s replacements.
		target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(${target} PRIVATE /W4 /WX)
	else()
		target_compile_options(${target} PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
endfunction()
st_settings_warnings(st_settings)

if(BUILD_TESTING)
	add_executable(st_settings_test tests/st_settings_test.c)
	target_link_libraries(st_settings_test PRIVATE
		st_settings syncterm_string_compat)
	set_target_properties(st_settings_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	st_settings_warnings(st_settings_test)
	add_test(NAME settings COMMAND st_settings_test)

	add_executable(st_settings_store_test tests/st_settings_store_test.c)
	target_link_libraries(st_settings_store_test PRIVATE st_settings
		syncterm_filesystem syncterm_ini)
	target_compile_definitions(st_settings_store_test PRIVATE
		ST_SETTINGS_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/store-test")
	set_target_properties(st_settings_store_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	st_settings_warnings(st_settings_store_test)
	add_test(NAME settings_store COMMAND st_settings_store_test)

	add_library(st_settings_faultable STATIC st_settings.c
		st_settings_store.c)
	target_include_directories(st_settings_faultable PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR} PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/tests)
	target_compile_definitions(st_settings_faultable PRIVATE
		ST_SETTINGS_STORE_TEST_COPY=1)
	target_link_libraries(st_settings_faultable PUBLIC
		syncterm_name_value_list st_app_paths syncterm_io_error PRIVATE
		st_file_lock syncterm_filesystem syncterm_ini
		syncterm_string_compat syncterm_string_util st_unicode_compare)
	set_target_properties(st_settings_faultable PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	st_settings_warnings(st_settings_faultable)

	add_executable(st_settings_failure_test
		tests/st_settings_failure_test.c
		tests/st_settings_test_copy.c)
	target_include_directories(st_settings_failure_test PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/tests)
	target_link_libraries(st_settings_failure_test PRIVATE
		st_settings_faultable syncterm_filesystem)
	target_compile_definitions(st_settings_failure_test PRIVATE
		ST_SETTINGS_FAILURE_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/failure-test")
	set_target_properties(st_settings_failure_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	st_settings_warnings(st_settings_failure_test)
	add_test(NAME settings_failures COMMAND st_settings_failure_test)
endif()

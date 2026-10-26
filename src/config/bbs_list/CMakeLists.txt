# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(STBBSList LANGUAGES C)
	include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCStandard.cmake")
	include(CTest)
endif()

foreach(_st_component_path IN ITEMS config/profile config/list_crypto config/app_paths net/webget
	io/file_lock base/hex config/settings base/time
	text/unicode_compare)
	get_filename_component(_dependency "${_st_component_path}" NAME)
	if(NOT TARGET st_${_dependency})
		add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../${_st_component_path}"
			"${CMAKE_CURRENT_BINARY_DIR}/${_dependency}")
	endif()
endforeach()
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR AND NOT TARGET st_fonts)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../media/fonts"
		"${CMAKE_CURRENT_BINARY_DIR}/fonts")
endif()
if(NOT TARGET syncterm_threads)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../base/threads"
		"${CMAKE_CURRENT_BINARY_DIR}/threads")
endif()
foreach(_st_component_path IN ITEMS base/array_list io/file_map io/filesystem config/ini
	base/io_error io/path base/string_compat base/string_util)
	get_filename_component(_dependency "${_st_component_path}" NAME)
	if(NOT TARGET syncterm_${_dependency})
		add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../${_st_component_path}"
			"${CMAKE_CURRENT_BINARY_DIR}/${_dependency}")
	endif()
endforeach()

if(WIN32)
	if(NOT TARGET st_windows_utf)
		add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../text/windows_utf"
			"${CMAKE_CURRENT_BINARY_DIR}/windows_utf")
	endif()
	set(ST_BBS_CACHE_SOURCE st_bbs_cache_windows.c)
else()
	set(ST_BBS_CACHE_SOURCE st_bbs_cache_posix.c)
endif()

set(ST_BBS_LIST_SOURCES st_bbs_list.c st_bbs_cache_key.c
	st_bbs_profile_codec.c st_bbs_store.c st_bbs_sort.c st_bbs_web.c
	${ST_BBS_CACHE_SOURCE})

add_library(st_bbs_list STATIC ${ST_BBS_LIST_SOURCES})
target_include_directories(st_bbs_list PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_bbs_list PUBLIC st_profile st_list_crypto st_app_paths
	st_webget
	syncterm_io_error PRIVATE st_fonts st_file_lock st_settings syncterm_array_list
	syncterm_file_map syncterm_filesystem syncterm_ini
	syncterm_string_compat syncterm_path st_hex
	syncterm_string_util st_consent_token xptls::crypto st_time
	syncterm_threads st_unicode_compare)
if(WIN32)
	target_link_libraries(st_bbs_list PRIVATE st_windows_utf)
endif()
set_target_properties(st_bbs_list PROPERTIES C_STANDARD ${SYNCTERM_C_STANDARD}
	C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(MSVC)
	# Use the portable C runtime APIs, not Microsoft-only _s replacements.
	target_compile_definitions(st_bbs_list PRIVATE _CRT_SECURE_NO_WARNINGS)
	target_compile_options(st_bbs_list PRIVATE /W4 /WX)
else()
	target_compile_options(st_bbs_list PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(BUILD_TESTING)
	add_executable(st_bbs_list_test tests/st_bbs_list_test.c)
	target_link_libraries(st_bbs_list_test PRIVATE st_bbs_list
		syncterm_filesystem syncterm_string_compat)
	target_compile_definitions(st_bbs_list_test PRIVATE
		ST_BBS_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/list-test")
	set_target_properties(st_bbs_list_test PROPERTIES C_STANDARD ${SYNCTERM_C_STANDARD}
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_bbs_list_test PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_bbs_list_test PRIVATE /W4 /WX)
	else()
		target_compile_options(st_bbs_list_test PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME bbs_list COMMAND st_bbs_list_test)

	add_library(st_bbs_list_faultable STATIC ${ST_BBS_LIST_SOURCES})
	target_include_directories(st_bbs_list_faultable PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR} PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/tests)
	target_compile_definitions(st_bbs_list_faultable PRIVATE
		ST_BBS_LIST_TEST_ALLOCATOR=1)
target_link_libraries(st_bbs_list_faultable PUBLIC st_profile st_list_crypto
		st_app_paths st_webget syncterm_io_error PRIVATE st_file_lock
		st_settings st_fonts syncterm_array_list syncterm_file_map
		syncterm_filesystem syncterm_ini
		syncterm_string_compat syncterm_path st_hex syncterm_string_util
		st_consent_token xptls::crypto st_time syncterm_threads
		st_unicode_compare)
	if(WIN32)
		target_link_libraries(st_bbs_list_faultable PRIVATE st_windows_utf)
	endif()
	set_target_properties(st_bbs_list_faultable PROPERTIES C_STANDARD ${SYNCTERM_C_STANDARD}
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_bbs_list_faultable PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_bbs_list_faultable PRIVATE /W4 /WX)
	else()
		target_compile_options(st_bbs_list_faultable PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()

	add_executable(st_bbs_failure_test tests/st_bbs_failure_test.c
		tests/st_bbs_test_allocator.c)
	target_include_directories(st_bbs_failure_test PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/tests)
	target_link_libraries(st_bbs_failure_test PRIVATE st_bbs_list_faultable
		syncterm_filesystem)
	target_compile_definitions(st_bbs_failure_test PRIVATE
		ST_BBS_FAILURE_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/failure-test")
	set_target_properties(st_bbs_failure_test PROPERTIES C_STANDARD ${SYNCTERM_C_STANDARD}
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_bbs_failure_test PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_bbs_failure_test PRIVATE /W4 /WX)
	else()
		target_compile_options(st_bbs_failure_test PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME bbs_failures COMMAND st_bbs_failure_test)

	add_executable(st_bbs_cache_safety_test
		tests/st_bbs_cache_safety_test.c)
	target_link_libraries(st_bbs_cache_safety_test PRIVATE st_bbs_list
		syncterm_filesystem)
	target_compile_definitions(st_bbs_cache_safety_test PRIVATE
		ST_BBS_CACHE_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/cache-safety-test")
	set_target_properties(st_bbs_cache_safety_test PROPERTIES C_STANDARD ${SYNCTERM_C_STANDARD}
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_bbs_cache_safety_test PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_bbs_cache_safety_test PRIVATE /W4 /WX)
	else()
		target_compile_options(st_bbs_cache_safety_test PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME bbs_cache_safety COMMAND st_bbs_cache_safety_test)

	add_executable(st_bbs_web_test tests/st_bbs_web_test.c)
	target_link_libraries(st_bbs_web_test PRIVATE st_bbs_list st_settings
		st_time syncterm_filesystem syncterm_socket syncterm_threads)
	target_compile_definitions(st_bbs_web_test PRIVATE
		ST_BBS_WEB_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/web-test")
	set_target_properties(st_bbs_web_test PROPERTIES C_STANDARD ${SYNCTERM_C_STANDARD}
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_bbs_web_test PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_bbs_web_test PRIVATE /W4 /WX)
	else()
		target_compile_options(st_bbs_web_test PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME bbs_web COMMAND st_bbs_web_test)
	set_tests_properties(bbs_web PROPERTIES TIMEOUT 15)
endif()

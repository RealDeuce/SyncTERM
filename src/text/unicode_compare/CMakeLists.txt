# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(STUnicodeCompare LANGUAGES C)
	include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCStandard.cmake")
	include(CTest)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../base/io_error"
		"${CMAKE_CURRENT_BINARY_DIR}/io_error")
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../utf8"
		"${CMAKE_CURRENT_BINARY_DIR}/utf8")
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../unicode_normalization"
		"${CMAKE_CURRENT_BINARY_DIR}/unicode_normalization")
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../unicode_text"
		"${CMAKE_CURRENT_BINARY_DIR}/unicode_text")
endif()

add_library(st_unicode_compare STATIC
	st_unicode_compare.c generated/st_unicode_compare_data.c)
target_include_directories(st_unicode_compare PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_unicode_compare PUBLIC
	syncterm_io_error st_utf8 st_unicode_normalization st_unicode_text)
set_target_properties(st_unicode_compare PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(MSVC)
	# Use the portable C runtime APIs, not Microsoft-only _s replacements.
	target_compile_definitions(st_unicode_compare PRIVATE _CRT_SECURE_NO_WARNINGS)
	target_compile_options(st_unicode_compare PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
else()
	target_compile_options(st_unicode_compare PRIVATE
		-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
endif()

if(BUILD_TESTING)
	add_executable(st_unicode_compare_test tests/st_unicode_compare_test.c)
	target_link_libraries(st_unicode_compare_test PRIVATE st_unicode_compare)
	set_target_properties(st_unicode_compare_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_unicode_compare_test PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_unicode_compare_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_unicode_compare_test PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME unicode_compare COMMAND st_unicode_compare_test)

	add_executable(st_unicode_collation_conformance
		tests/st_unicode_collation_conformance.c)
	target_link_libraries(st_unicode_collation_conformance PRIVATE
		st_unicode_compare)
	set_target_properties(st_unicode_collation_conformance PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_unicode_collation_conformance PRIVATE
			_CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_unicode_collation_conformance PRIVATE
			/W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_unicode_collation_conformance PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
endif()

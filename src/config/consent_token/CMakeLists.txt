# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(STConsentToken LANGUAGES C)
	include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCStandard.cmake")
	include(CTest)
	if(NOT TARGET st_base64)
		add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../base/base64"
			"${CMAKE_CURRENT_BINARY_DIR}/base64")
	endif()
	if(NOT TARGET syncterm_file_map)
		add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../io/file_map"
			"${CMAKE_CURRENT_BINARY_DIR}/file_map")
	endif()
	if(NOT TARGET xptls_crypto)
		add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../net/xptls_crypto"
			"${CMAKE_CURRENT_BINARY_DIR}/xptls_crypto")
	endif()
endif()

add_library(st_consent_token STATIC st_consent_token.c)
target_include_directories(st_consent_token PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_consent_token PUBLIC xptls::crypto
	PRIVATE st_base64 syncterm_file_map)
set_target_properties(st_consent_token PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)

if(MSVC)
	# Use the portable C runtime APIs, not Microsoft-only _s replacements.
	target_compile_definitions(st_consent_token PRIVATE _CRT_SECURE_NO_WARNINGS)
	target_compile_options(st_consent_token PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
else()
	target_compile_options(st_consent_token PRIVATE
		-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
endif()

if(BUILD_TESTING)
	add_executable(st_consent_token_test tests/st_consent_token_test.c)
	target_link_libraries(st_consent_token_test PRIVATE st_consent_token
		syncterm_filesystem)
	target_compile_definitions(st_consent_token_test PRIVATE
		ST_CONSENT_TOKEN_TEST_DIRECTORY=\"${CMAKE_CURRENT_BINARY_DIR}/token-test\")
	set_target_properties(st_consent_token_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_consent_token_test PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_consent_token_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_consent_token_test PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME consent_token COMMAND st_consent_token_test)
endif()

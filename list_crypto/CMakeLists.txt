# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(STListCrypto LANGUAGES C)
	include(CTest)
endif()

if(NOT TARGET st_hex)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../hex"
		"${CMAKE_CURRENT_BINARY_DIR}/hex")
endif()

foreach(_dependency IN ITEMS file_map ini io_error xptls_crypto)
	if(_dependency STREQUAL "xptls_crypto")
		set(_target xptls_crypto)
	else()
		set(_target syncterm_${_dependency})
	endif()
	if(NOT TARGET ${_target})
		add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../${_dependency}"
			"${CMAKE_CURRENT_BINARY_DIR}/${_dependency}")
	endif()
endforeach()

add_library(st_list_crypto STATIC
	st_list_crypto.c legacy_idea.c legacy_rc2.c)
target_include_directories(st_list_crypto PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_list_crypto
	PUBLIC syncterm_ini syncterm_io_error
	PRIVATE st_hex syncterm_file_map xptls::crypto)
set_target_properties(st_list_crypto PROPERTIES
	C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)

function(st_list_crypto_warnings target)
	if(MSVC)
		target_compile_options(${target} PRIVATE /W4 /WX)
	else()
		target_compile_options(${target} PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
endfunction()
st_list_crypto_warnings(st_list_crypto)

if(BUILD_TESTING)
	add_executable(st_list_crypto_test tests/st_list_crypto_test.c)
	target_link_libraries(st_list_crypto_test PRIVATE st_list_crypto)
	set_target_properties(st_list_crypto_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	st_list_crypto_warnings(st_list_crypto_test)
	add_test(NAME list_crypto COMMAND st_list_crypto_test)
endif()

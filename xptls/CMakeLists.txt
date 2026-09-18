# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(STTLS LANGUAGES C)
	include(CTest)
endif()

if(NOT TARGET xptls_crypto)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../xptls_crypto"
		"${CMAKE_CURRENT_BINARY_DIR}/xptls_crypto")
endif()
foreach(_dependency IN ITEMS io_error socket string_compat)
	if(NOT TARGET syncterm_${_dependency})
		add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../${_dependency}"
			"${CMAKE_CURRENT_BINARY_DIR}/${_dependency}")
	endif()
endforeach()
if(NOT TARGET st_time)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../time"
		"${CMAKE_CURRENT_BINARY_DIR}/time")
endif()

get_property(ST_TLS_PROVIDER TARGET xptls_crypto PROPERTY
	XPTLS_CRYPTO_PROVIDER)
if(ST_TLS_PROVIDER STREQUAL "Botan")
	enable_language(CXX)
	find_package(PkgConfig REQUIRED)
	pkg_check_modules(ST_TLS_BOTAN REQUIRED IMPORTED_TARGET "botan-3>=3.13.0")
	set(ST_TLS_SOURCE st_tls_botan.cpp)
	set(ST_TLS_LIBRARY PkgConfig::ST_TLS_BOTAN)
elseif(ST_TLS_PROVIDER STREQUAL "OpenSSL")
	find_package(OpenSSL 3.0 REQUIRED)
	set(ST_TLS_SOURCE st_tls_openssl.c)
	set(ST_TLS_LIBRARY OpenSSL::SSL)
else()
	set(ST_TLS_SOURCE st_tls_none.c)
	set(ST_TLS_LIBRARY)
endif()

add_library(st_tls STATIC ${ST_TLS_SOURCE})
target_include_directories(st_tls PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_tls PUBLIC syncterm_io_error syncterm_socket
	PRIVATE xptls_crypto syncterm_string_compat ${ST_TLS_LIBRARY})
target_link_libraries(st_tls PRIVATE st_time)
set_target_properties(st_tls PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES
	C_EXTENSIONS NO)
if(ST_TLS_PROVIDER STREQUAL "Botan")
	set_target_properties(st_tls PROPERTIES CXX_STANDARD 20
		CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO LINKER_LANGUAGE CXX)
endif()
if(MSVC)
	target_compile_options(st_tls PRIVATE /W4 /WX)
else()
	target_compile_options(st_tls PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

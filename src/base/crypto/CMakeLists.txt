# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(STCrypto LANGUAGES C)
	include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCStandard.cmake")
	include(CTest)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCheckC17Source.cmake")

option(ST_CRYPTO_TEST_CLEAR_FALLBACK
	"Build the best-effort clear fallback test even when it is not selected" OFF)

set(_st_crypto_definitions)
set(_st_crypto_probe_definitions)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	list(APPEND _st_crypto_definitions _GNU_SOURCE)
	list(APPEND _st_crypto_probe_definitions -D_GNU_SOURCE)
endif()

set(_st_crypto_saved_required_definitions ${CMAKE_REQUIRED_DEFINITIONS})
set(CMAKE_REQUIRED_DEFINITIONS ${_st_crypto_saved_required_definitions}
	${_st_crypto_probe_definitions})
set(CMAKE_C_STANDARD ${SYNCTERM_C_STANDARD})
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
syncterm_check_c17_source([=[
#include <stddef.h>
#include <string.h>
int main(void)
{
	void *(*clear)(void *, int, size_t) = memset_explicit;
	char value = 1;
	clear(&value, 0, sizeof(value));
	return value;
}
]=] ST_CRYPTO_HAVE_MEMSET_EXPLICIT)
if(NOT ST_CRYPTO_HAVE_MEMSET_EXPLICIT AND NOT WIN32)
	syncterm_check_c17_source([=[
#include <stddef.h>
#include <string.h>
#include <strings.h>
int main(void)
{
	void (*clear)(void *, size_t) = explicit_bzero;
	char value = 1;
	clear(&value, sizeof(value));
	return value;
}
]=] ST_CRYPTO_HAVE_EXPLICIT_BZERO)
endif()
set(CMAKE_REQUIRED_DEFINITIONS ${_st_crypto_saved_required_definitions})

set(CRYPTO_BACKEND "" CACHE STRING
	"Crypto provider: Botan, OpenSSL, none, or empty for auto-detect")
set_property(CACHE CRYPTO_BACKEND PROPERTY STRINGS "" Botan OpenSSL none)

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
	pkg_check_modules(ST_CRYPTO_BOTAN QUIET IMPORTED_TARGET "botan-3>=3.13.0")
endif()
find_package(OpenSSL 3.0 QUIET)

if(OpenSSL_FOUND)
	include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCheckC17Source.cmake")
	set(_st_crypto_saved_required_libraries "${CMAKE_REQUIRED_LIBRARIES}")
	set(CMAKE_REQUIRED_LIBRARIES OpenSSL::Crypto)
	syncterm_check_c17_source([=[
#include <openssl/evp.h>
int main(void)
{
	return EVP_sha256() == 0;
}
]=] ST_CRYPTO_OPENSSL_USABLE)
	set(CMAKE_REQUIRED_LIBRARIES "${_st_crypto_saved_required_libraries}")
endif()

if(TARGET PkgConfig::ST_CRYPTO_BOTAN
    AND (CRYPTO_BACKEND STREQUAL "Botan"
    OR (NOT CRYPTO_BACKEND AND NOT CMAKE_CROSSCOMPILING)))
	include(CheckLanguage)
	check_language(CXX)
	if(CMAKE_CXX_COMPILER)
		enable_language(CXX)
		include(CheckCXXSourceCompiles)
		set(_st_crypto_saved_required_libraries "${CMAKE_REQUIRED_LIBRARIES}")
		set(_st_crypto_saved_cxx_standard "${CMAKE_CXX_STANDARD}")
		set(CMAKE_REQUIRED_LIBRARIES PkgConfig::ST_CRYPTO_BOTAN)
		set(CMAKE_CXX_STANDARD 20)
		check_cxx_source_compiles([=[
#include <botan/version.h>
#include <string>
int main()
{
	std::string version = Botan::version_string();
	return version.empty();
}
]=] ST_CRYPTO_BOTAN_USABLE)
		set(CMAKE_REQUIRED_LIBRARIES "${_st_crypto_saved_required_libraries}")
		set(CMAKE_CXX_STANDARD "${_st_crypto_saved_cxx_standard}")
	endif()
endif()

set(_st_crypto_backend "${CRYPTO_BACKEND}")
if(NOT _st_crypto_backend)
	if(ST_CRYPTO_BOTAN_USABLE)
		set(_st_crypto_backend Botan)
	elseif(ST_CRYPTO_OPENSSL_USABLE)
		set(_st_crypto_backend OpenSSL)
	else()
		set(_st_crypto_backend none)
	endif()
endif()

if(_st_crypto_backend STREQUAL "Botan")
	if(NOT TARGET PkgConfig::ST_CRYPTO_BOTAN)
		message(FATAL_ERROR "CRYPTO_BACKEND=Botan requires Botan 3.13 or newer")
	endif()
	if(NOT ST_CRYPTO_BOTAN_USABLE)
		message(FATAL_ERROR
			"CRYPTO_BACKEND=Botan is not usable with the selected C++ toolchain")
	endif()
	enable_language(CXX)
	set(_st_crypto_provider st_crypto_botan.cpp)
	set(_st_crypto_library PkgConfig::ST_CRYPTO_BOTAN)
elseif(_st_crypto_backend STREQUAL "OpenSSL")
	if(NOT ST_CRYPTO_OPENSSL_USABLE)
		message(FATAL_ERROR "CRYPTO_BACKEND=OpenSSL requires OpenSSL 3.0 or newer")
	endif()
	set(_st_crypto_provider st_crypto_openssl.c)
	set(_st_crypto_library OpenSSL::Crypto)
elseif(_st_crypto_backend STREQUAL "none")
	set(_st_crypto_provider st_crypto_none.c)
	set(_st_crypto_library)
else()
	message(FATAL_ERROR "Unknown CRYPTO_BACKEND '${_st_crypto_backend}'")
endif()

if(ST_CRYPTO_HAVE_MEMSET_EXPLICIT)
	set(_st_crypto_clear st_crypto_clear_memset_explicit.c)
elseif(ST_CRYPTO_HAVE_EXPLICIT_BZERO)
	set(_st_crypto_clear st_crypto_clear_explicit_bzero.c)
elseif(WIN32)
	set(_st_crypto_clear st_crypto_clear_windows.c)
elseif(_st_crypto_backend STREQUAL "OpenSSL")
	set(_st_crypto_clear st_crypto_clear_openssl.c)
elseif(_st_crypto_backend STREQUAL "Botan")
	set(_st_crypto_clear st_crypto_clear_botan.cpp)
else()
	set(_st_crypto_clear st_crypto_clear_fallback.c)
endif()

message(STATUS "crypto provider: ${_st_crypto_backend}")
add_library(st_crypto STATIC st_crypto_common.c ${_st_crypto_provider}
	${_st_crypto_clear})
set_property(TARGET st_crypto PROPERTY ST_CRYPTO_PROVIDER
	"${_st_crypto_backend}")
target_include_directories(st_crypto PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_crypto PRIVATE ${_st_crypto_library})
target_compile_definitions(st_crypto PRIVATE ${_st_crypto_definitions})
set_target_properties(st_crypto PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(_st_crypto_backend STREQUAL "Botan")
	set_target_properties(st_crypto PROPERTIES
		CXX_STANDARD 20 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO
		LINKER_LANGUAGE CXX)
	# st_crypto has a C ABI, but its Botan provider is C++.  Static-library
	# consumers therefore need the runtime libraries CMake detected for the
	# selected C++ toolchain even when all of their own sources are C.
	target_link_libraries(st_crypto INTERFACE
		${CMAKE_CXX_IMPLICIT_LINK_LIBRARIES})
	target_link_directories(st_crypto INTERFACE
		${CMAKE_CXX_IMPLICIT_LINK_DIRECTORIES})
endif()

if(MSVC)
	target_compile_options(st_crypto PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
else()
	target_compile_options(st_crypto PRIVATE
		-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
endif()

if(BUILD_TESTING)
	add_executable(st_crypto_test tests/st_crypto_test.c)
	target_link_libraries(st_crypto_test PRIVATE st_crypto)
	set_target_properties(st_crypto_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_crypto_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_crypto_test PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME st_crypto COMMAND st_crypto_test)

	if(ST_CRYPTO_TEST_CLEAR_FALLBACK
	    AND NOT _st_crypto_clear STREQUAL "st_crypto_clear_fallback.c")
		add_executable(st_crypto_clear_fallback_test
			st_crypto_clear_fallback.c tests/st_crypto_clear_test.c)
		target_include_directories(st_crypto_clear_fallback_test PRIVATE
			${CMAKE_CURRENT_SOURCE_DIR})
		set_target_properties(st_crypto_clear_fallback_test PROPERTIES
			C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
		if(MSVC)
			target_compile_options(st_crypto_clear_fallback_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
		else()
			target_compile_options(st_crypto_clear_fallback_test PRIVATE
				-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
		endif()
		add_test(NAME st_crypto_clear_fallback
			COMMAND st_crypto_clear_fallback_test)
	endif()
endif()

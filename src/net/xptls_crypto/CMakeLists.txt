# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(XPTLSCrypto LANGUAGES C)
	include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCStandard.cmake")
	include(CTest)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCheckC17Source.cmake")

option(XPTLS_CRYPTO_TEST_CLEAR_FALLBACK
	"Build the best-effort clear fallback test even when it is not selected" OFF)

set(_xptls_crypto_definitions)
set(_xptls_crypto_probe_definitions)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	list(APPEND _xptls_crypto_definitions _GNU_SOURCE)
	list(APPEND _xptls_crypto_probe_definitions -D_GNU_SOURCE)
endif()

set(_xptls_saved_required_definitions ${CMAKE_REQUIRED_DEFINITIONS})
set(CMAKE_REQUIRED_DEFINITIONS ${_xptls_saved_required_definitions}
	${_xptls_crypto_probe_definitions})
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
]=] XPTLS_CRYPTO_HAVE_MEMSET_EXPLICIT)
if(NOT XPTLS_CRYPTO_HAVE_MEMSET_EXPLICIT AND NOT WIN32)
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
]=] XPTLS_CRYPTO_HAVE_EXPLICIT_BZERO)
endif()
set(CMAKE_REQUIRED_DEFINITIONS ${_xptls_saved_required_definitions})

set(CRYPTO_BACKEND "" CACHE STRING
	"Crypto provider: Botan, OpenSSL, none, or empty for auto-detect")
set_property(CACHE CRYPTO_BACKEND PROPERTY STRINGS "" Botan OpenSSL none)

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
	pkg_check_modules(XPTLS_BOTAN QUIET IMPORTED_TARGET "botan-3>=3.13.0")
endif()
find_package(OpenSSL 3.0 QUIET)

if(OpenSSL_FOUND)
	include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCheckC17Source.cmake")
	set(_xptls_saved_required_libraries "${CMAKE_REQUIRED_LIBRARIES}")
	set(CMAKE_REQUIRED_LIBRARIES OpenSSL::Crypto)
	syncterm_check_c17_source([=[
#include <openssl/evp.h>
int main(void)
{
	return EVP_sha256() == 0;
}
]=] XPTLS_OPENSSL_USABLE)
	set(CMAKE_REQUIRED_LIBRARIES "${_xptls_saved_required_libraries}")
endif()

if(TARGET PkgConfig::XPTLS_BOTAN
    AND (CRYPTO_BACKEND STREQUAL "Botan"
    OR (NOT CRYPTO_BACKEND AND NOT CMAKE_CROSSCOMPILING)))
	include(CheckLanguage)
	check_language(CXX)
	if(CMAKE_CXX_COMPILER)
		enable_language(CXX)
		include(CheckCXXSourceCompiles)
		set(_xptls_saved_required_libraries "${CMAKE_REQUIRED_LIBRARIES}")
		set(_xptls_saved_cxx_standard "${CMAKE_CXX_STANDARD}")
		set(CMAKE_REQUIRED_LIBRARIES PkgConfig::XPTLS_BOTAN)
		set(CMAKE_CXX_STANDARD 20)
		check_cxx_source_compiles([=[
#include <botan/version.h>
#include <string>
int main()
{
	std::string version = Botan::version_string();
	return version.empty();
}
]=] XPTLS_BOTAN_USABLE)
		set(CMAKE_REQUIRED_LIBRARIES "${_xptls_saved_required_libraries}")
		set(CMAKE_CXX_STANDARD "${_xptls_saved_cxx_standard}")
	endif()
endif()

set(_xptls_crypto_backend "${CRYPTO_BACKEND}")
if(NOT _xptls_crypto_backend)
	if(XPTLS_BOTAN_USABLE)
		set(_xptls_crypto_backend Botan)
	elseif(XPTLS_OPENSSL_USABLE)
		set(_xptls_crypto_backend OpenSSL)
	else()
		set(_xptls_crypto_backend none)
	endif()
endif()

if(_xptls_crypto_backend STREQUAL "Botan")
	if(NOT TARGET PkgConfig::XPTLS_BOTAN)
		message(FATAL_ERROR "CRYPTO_BACKEND=Botan requires Botan 3.13 or newer")
	endif()
	if(NOT XPTLS_BOTAN_USABLE)
		message(FATAL_ERROR
			"CRYPTO_BACKEND=Botan is not usable with the selected C++ toolchain")
	endif()
	enable_language(CXX)
	set(_xptls_crypto_provider xp_crypto_botan.cpp)
	set(_xptls_crypto_library PkgConfig::XPTLS_BOTAN)
elseif(_xptls_crypto_backend STREQUAL "OpenSSL")
	if(NOT XPTLS_OPENSSL_USABLE)
		message(FATAL_ERROR "CRYPTO_BACKEND=OpenSSL requires OpenSSL 3.0 or newer")
	endif()
	set(_xptls_crypto_provider xp_crypto_openssl.c)
	set(_xptls_crypto_library OpenSSL::Crypto)
elseif(_xptls_crypto_backend STREQUAL "none")
	set(_xptls_crypto_provider xp_crypto_none.c)
	set(_xptls_crypto_library)
else()
	message(FATAL_ERROR "Unknown CRYPTO_BACKEND '${_xptls_crypto_backend}'")
endif()

if(XPTLS_CRYPTO_HAVE_MEMSET_EXPLICIT)
	set(_xptls_crypto_clear xp_crypto_clear_memset_explicit.c)
elseif(XPTLS_CRYPTO_HAVE_EXPLICIT_BZERO)
	set(_xptls_crypto_clear xp_crypto_clear_explicit_bzero.c)
elseif(WIN32)
	set(_xptls_crypto_clear xp_crypto_clear_windows.c)
elseif(_xptls_crypto_backend STREQUAL "OpenSSL")
	set(_xptls_crypto_clear xp_crypto_clear_openssl.c)
elseif(_xptls_crypto_backend STREQUAL "Botan")
	set(_xptls_crypto_clear xp_crypto_clear_botan.cpp)
else()
	set(_xptls_crypto_clear xp_crypto_clear_fallback.c)
endif()

message(STATUS "xptls crypto provider: ${_xptls_crypto_backend}")
add_library(xptls_crypto STATIC xp_crypto_common.c ${_xptls_crypto_provider}
	${_xptls_crypto_clear})
set_property(TARGET xptls_crypto PROPERTY XPTLS_CRYPTO_PROVIDER
	"${_xptls_crypto_backend}")
add_library(xptls::crypto ALIAS xptls_crypto)
target_include_directories(xptls_crypto PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(xptls_crypto PRIVATE ${_xptls_crypto_library})
target_compile_definitions(xptls_crypto PRIVATE ${_xptls_crypto_definitions})
set_target_properties(xptls_crypto PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(_xptls_crypto_backend STREQUAL "Botan")
	set_target_properties(xptls_crypto PROPERTIES
		CXX_STANDARD 20 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO
		LINKER_LANGUAGE CXX)
	# xptls_crypto has a C ABI, but its Botan provider is C++.  Static-library
	# consumers therefore need the runtime libraries CMake detected for the
	# selected C++ toolchain even when all of their own sources are C.
	target_link_libraries(xptls_crypto INTERFACE
		${CMAKE_CXX_IMPLICIT_LINK_LIBRARIES})
	target_link_directories(xptls_crypto INTERFACE
		${CMAKE_CXX_IMPLICIT_LINK_DIRECTORIES})
endif()

if(MSVC)
	target_compile_options(xptls_crypto PRIVATE /W4 /WX)
else()
	target_compile_options(xptls_crypto PRIVATE
		-Wall -Wextra -Wpedantic -Werror)
endif()

if(BUILD_TESTING)
	add_executable(xp_crypto_test tests/xp_crypto_test.c)
	target_link_libraries(xp_crypto_test PRIVATE xptls_crypto)
	set_target_properties(xp_crypto_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(xp_crypto_test PRIVATE /W4 /WX)
	else()
		target_compile_options(xp_crypto_test PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME xp_crypto COMMAND xp_crypto_test)

	if(XPTLS_CRYPTO_TEST_CLEAR_FALLBACK
	    AND NOT _xptls_crypto_clear STREQUAL "xp_crypto_clear_fallback.c")
		add_executable(xp_crypto_clear_fallback_test
			xp_crypto_clear_fallback.c tests/xp_crypto_clear_test.c)
		target_include_directories(xp_crypto_clear_fallback_test PRIVATE
			${CMAKE_CURRENT_SOURCE_DIR})
		set_target_properties(xp_crypto_clear_fallback_test PROPERTIES
			C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
		if(MSVC)
			target_compile_options(xp_crypto_clear_fallback_test PRIVATE /W4 /WX)
		else()
			target_compile_options(xp_crypto_clear_fallback_test PRIVATE
				-Wall -Wextra -Wpedantic -Werror)
		endif()
		add_test(NAME xp_crypto_clear_fallback
			COMMAND xp_crypto_clear_fallback_test)
	endif()
endif()

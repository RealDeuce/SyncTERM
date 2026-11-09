# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(POLICY CMP0156)
	cmake_policy(SET CMP0156 NEW)
	set(CMAKE_POLICY_DEFAULT_CMP0156 NEW)
endif()
if(POLICY CMP0179)
	cmake_policy(SET CMP0179 NEW)
	set(CMAKE_POLICY_DEFAULT_CMP0179 NEW)
endif()

project(SyncTERM LANGUAGES C)
include("${CMAKE_CURRENT_LIST_DIR}/cmake/SynctermCStandard.cmake")

option(SYNCTERM_ENABLE_UNIFONT
	"Build and use SyncTERM Unicode (SIL OFL 1.1)" ON)

include(CTest)
include("${CMAKE_CURRENT_LIST_DIR}/cmake/SynctermCheckC17Source.cmake")

if(WIN32)
	set(SYNCTERM_PLATFORM_WINDOWS TRUE)
	set(SYNCTERM_PLATFORM_POSIX FALSE)
else()
	set(SYNCTERM_PLATFORM_WINDOWS FALSE)
	set(SYNCTERM_PLATFORM_POSIX TRUE)
endif()

syncterm_check_c17_source([=[
#include <sys/types.h>

int main(void)
{
	ssize_t value = (ssize_t)-1;
	return value < 0 ? 0 : 1;
}
]=] SYNCTERM_HAVE_SSIZE_T)

if(NOT SYNCTERM_HAVE_SSIZE_T AND SYNCTERM_PLATFORM_POSIX)
	message(FATAL_ERROR "The target POSIX platform does not provide ssize_t")
endif()

configure_file(src/syncterm_config.h.in syncterm_config.h @ONLY)

set(_syncterm_saved_required_includes ${CMAKE_REQUIRED_INCLUDES})
set(CMAKE_REQUIRED_INCLUDES
	"${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_BINARY_DIR}")
syncterm_check_c17_source([=[
#include "syncterm_types.h"

_Static_assert((ssize_t)-1 < (ssize_t)0, "ssize_t must be signed");

int main(void)
{
	ssize_t value = 0;
	return value != 0;
}
]=] SYNCTERM_TYPES_HEADER_VALID)
set(CMAKE_REQUIRED_INCLUDES ${_syncterm_saved_required_includes})
unset(_syncterm_saved_required_includes)

if(NOT SYNCTERM_TYPES_HEADER_VALID)
	message(FATAL_ERROR "syncterm_types.h is not usable with the target C17 toolchain")
endif()

add_subdirectory(src/base/io_error)
add_subdirectory(src/base/hex)
add_subdirectory(src/base/md5)
add_subdirectory(src/base/base64)
add_subdirectory(src/text/utf8)
add_subdirectory(src/text/unicode_width)
add_subdirectory(src/text/unicode_normalization)
add_subdirectory(src/text/unicode_grapheme)
add_subdirectory(src/text/unicode_text)
add_subdirectory(src/text/unicode_compare)
add_subdirectory(src/text/codepage)
add_subdirectory(src/text/emoji)
if(WIN32)
	add_subdirectory(src/text/windows_utf)
endif()
add_subdirectory(src/base/time)
add_subdirectory(src/io/path)
add_subdirectory(src/base/string_compat)
add_subdirectory(src/base/language)
add_subdirectory(src/base/array_list)
add_subdirectory(src/base/string_util)
add_subdirectory(src/base/value_convert)
add_subdirectory(src/base/str_list)
add_subdirectory(src/base/name_value_list)
add_subdirectory(src/io/filesystem)
add_subdirectory(src/config/app_paths)
add_subdirectory(src/io/file_map)
add_subdirectory(src/base/crypto)
add_subdirectory(src/config/consent_token)
add_subdirectory(src/base/threads)
add_subdirectory(src/net/socket)
add_subdirectory(src/net/tcp_client)
add_subdirectory(src/terminal/hyperlink)
add_subdirectory(src/terminal/graphics_protocol)
add_subdirectory(src/io/file_lock)
add_subdirectory(src/net/tls)
add_subdirectory(src/config/profile)
add_subdirectory(src/config/ini)
add_subdirectory(src/config/settings)
add_subdirectory(src/config/list_crypto)
add_subdirectory(src/net/webget)
add_subdirectory(src/config/theme)
add_subdirectory(src/config/bbs_list)
add_subdirectory(src/io/serial)
add_subdirectory(src/base/dynamic_library)
add_subdirectory(src/base/event)
add_subdirectory(src/base/semaphore)
add_subdirectory(src/media/audio_mixer)
add_subdirectory(src/media/audio)
add_subdirectory(src/media/music)
add_subdirectory(src/media/audio_file)
add_subdirectory(src/media/pixels)
add_subdirectory(src/terminal/igs)
add_subdirectory(src/media/pixel_image)
add_subdirectory(src/media/console)
add_subdirectory(src/terminal/screen_mode)
add_subdirectory(src/terminal/session_history)
add_subdirectory(src/media/fonts)
add_subdirectory(src/media/amiga_font)
add_subdirectory(src/media/font_catalog)
add_subdirectory(src/terminal/cterm)
add_subdirectory(src/terminal/ooii)
if(SYNCTERM_ENABLE_UNIFONT)
	add_subdirectory(src/media/unifont)
endif()
add_subdirectory(src/net/connection)
add_subdirectory(src/net/transfer)
add_subdirectory(src/terminal/term)
add_subdirectory(src/scripting/wren_vm)
add_subdirectory(src/scripting/re1)
add_subdirectory(src/scripting/wren_host)
add_subdirectory(src)
add_subdirectory(addons/modern_ui)

syncterm_print_configuration()
if(BUILD_TESTING)
	add_test(NAME build_test_runner COMMAND "${CMAKE_COMMAND}"
		"-DTEST_ROOT=${PROJECT_BINARY_DIR}/test-runner-check"
		-P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/tests/test_runner.cmake")
	add_test(NAME build_configuration_summary COMMAND "${CMAKE_COMMAND}"
		"-DBUILD_DIR=${PROJECT_BINARY_DIR}" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/tests/build_features.cmake")
	add_test(NAME dependency_policy COMMAND "${CMAKE_COMMAND}"
		"-DTEST_ROOT=${PROJECT_BINARY_DIR}/dependency-policy-tests"
		-P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/tests/dependency_policy.cmake")
	find_package(Python3 QUIET COMPONENTS Interpreter)
	if(Python3_Interpreter_FOUND)
		add_test(NAME wren_api_accounting_checks COMMAND "${Python3_EXECUTABLE}"
			"${CMAKE_CURRENT_SOURCE_DIR}/tools/audit/test_wren_c_api_coverage.py"
			"${PROJECT_BINARY_DIR}/wren-api-check-tests")
		add_test(NAME dependency_sources COMMAND "${Python3_EXECUTABLE}"
			"${CMAKE_CURRENT_SOURCE_DIR}/cmake/tests/dependency_sources.py"
			"${CMAKE_COMMAND}" "${PROJECT_BINARY_DIR}/dependency-source-tests")
	else()
		add_test(NAME dependency_sources COMMAND "${CMAKE_COMMAND}"
			"-DTEST_ROOT=${PROJECT_BINARY_DIR}/dependency-source-tests"
			-P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/tests/dependency_sources.cmake")
	endif()
endif()

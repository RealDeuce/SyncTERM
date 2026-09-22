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

option(SYNCTERM_ENABLE_UNIFONT
	"Build and use the GPLv2+ GNU Unifont-derived composite" ON)

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

configure_file(syncterm_config.h.in syncterm_config.h @ONLY)

set(_syncterm_saved_required_includes ${CMAKE_REQUIRED_INCLUDES})
set(CMAKE_REQUIRED_INCLUDES
	"${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")
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

add_subdirectory(io_error)
add_subdirectory(hex)
add_subdirectory(base64)
add_subdirectory(utf8)
add_subdirectory(unicode_width)
add_subdirectory(unicode_normalization)
add_subdirectory(unicode_grapheme)
add_subdirectory(codepage)
if(WIN32)
	add_subdirectory(windows_utf)
endif()
add_subdirectory(time)
add_subdirectory(path)
add_subdirectory(string_compat)
add_subdirectory(array_list)
add_subdirectory(string_util)
add_subdirectory(value_convert)
add_subdirectory(str_list)
add_subdirectory(name_value_list)
add_subdirectory(filesystem)
add_subdirectory(app_paths)
add_subdirectory(file_map)
add_subdirectory(xptls_crypto)
add_subdirectory(consent_token)
add_subdirectory(socket)
add_subdirectory(tcp_client)
add_subdirectory(threads)
add_subdirectory(hyperlink)
add_subdirectory(file_lock)
add_subdirectory(xptls)
add_subdirectory(profile)
add_subdirectory(ini)
add_subdirectory(settings)
add_subdirectory(list_crypto)
add_subdirectory(webget)
add_subdirectory(theme)
add_subdirectory(bbs_list)
add_subdirectory(serial)
add_subdirectory(dynamic_library)
add_subdirectory(event)
add_subdirectory(semaphore)
add_subdirectory(audio_mixer)
add_subdirectory(audio)
add_subdirectory(pixels)
add_subdirectory(pixel_image)
add_subdirectory(console)
add_subdirectory(screen_mode)
add_subdirectory(session_history)
add_subdirectory(fonts)
if(SYNCTERM_ENABLE_UNIFONT)
	add_subdirectory(unifont)
endif()
add_subdirectory(connection)
add_subdirectory(wren_vm)
add_subdirectory(wren_host)
add_subdirectory(app)

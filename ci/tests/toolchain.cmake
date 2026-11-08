# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)
if(NOT DEFINED TEST_ROOT)
	message(FATAL_ERROR "TEST_ROOT is required")
endif()
find_program(_pkg_config NAMES pkg-config pkgconf)
set(_target "${TEST_ROOT}/target")
set(_host "${TEST_ROOT}/host")
file(MAKE_DIRECTORY "${_target}/lib/pkgconfig" "${_host}/lib/pkgconfig")
file(WRITE "${_target}/lib/pkgconfig/ci-target.pc"
	"Name: CI target\nDescription: Target fixture\nVersion: 1\n")
file(WRITE "${_host}/lib/pkgconfig/ci-host.pc"
	"Name: CI host\nDescription: Host fixture\nVersion: 1\n")
foreach(CI_MINGW_BITS IN ITEMS 32 64)
	set(CMAKE_FIND_ROOT_PATH "${_target}")
	set(ENV{PKG_CONFIG_PATH} "${_host}/lib/pkgconfig")
	set(ENV{PKG_CONFIG_LIBDIR} "${_host}/lib/pkgconfig")
	include("${CMAKE_CURRENT_LIST_DIR}/../mingw.cmake")
	set(_expected_processor i686)
	if(CI_MINGW_BITS STREQUAL "64")
		set(_expected_processor x86_64)
	endif()
	if(NOT CMAKE_SYSTEM_PROCESSOR STREQUAL _expected_processor)
		message(FATAL_ERROR "MinGW toolchain did not declare its target processor")
	endif()
	if(NOT "${_target}" IN_LIST CMAKE_FIND_ROOT_PATH
	    OR NOT "$ENV{PKG_CONFIG_PATH}" STREQUAL ""
	    OR PKG_CONFIG_USE_CMAKE_PREFIX_PATH
	    OR NOT CMAKE_FIND_ROOT_PATH_MODE_LIBRARY STREQUAL "ONLY"
	    OR NOT CMAKE_FIND_ROOT_PATH_MODE_INCLUDE STREQUAL "ONLY"
	    OR NOT CMAKE_FIND_ROOT_PATH_MODE_PACKAGE STREQUAL "ONLY"
	    OR NOT CMAKE_FIND_ROOT_PATH_MODE_PROGRAM STREQUAL "NEVER")
		message(FATAL_ERROR "Cross-build search isolation failed")
	endif()
	if(_pkg_config)
		execute_process(COMMAND "${_pkg_config}" --exists ci-target RESULT_VARIABLE _target_result)
		execute_process(COMMAND "${_pkg_config}" --exists ci-host RESULT_VARIABLE _host_result)
		if(NOT "${_target_result}" STREQUAL "0" OR "${_host_result}" STREQUAL "0")
			message(FATAL_ERROR "pkg-config did not isolate target packages")
		endif()
	endif()
endforeach()
message(STATUS "Both MinGW toolchains retain target roots and reject host pkg-config metadata")

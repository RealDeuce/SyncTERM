# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)
project(SyncTERMCITests NONE)
include(CTest)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/reports")
if(BUILD_TESTING)
	add_test(NAME ci_c_standard COMMAND "${CMAKE_COMMAND}"
		-P "${CMAKE_CURRENT_SOURCE_DIR}/c_standard.cmake")
	add_test(NAME ci_c_capabilities COMMAND "${CMAKE_COMMAND}"
		"-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/capability tests"
		-P "${CMAKE_CURRENT_SOURCE_DIR}/capabilities.cmake")
	add_test(NAME ci_driver COMMAND "${CMAKE_COMMAND}"
		"-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/driver tests"
		-P "${CMAKE_CURRENT_SOURCE_DIR}/driver.cmake")
	set_tests_properties(ci_driver PROPERTIES TIMEOUT 1800)
	if(NOT WIN32)
		add_test(NAME ci_mingw_toolchain COMMAND "${CMAKE_COMMAND}"
			"-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/toolchain tests"
			-P "${CMAKE_CURRENT_SOURCE_DIR}/toolchain.cmake")
		add_test(NAME ci_posix COMMAND "${CMAKE_COMMAND}"
			"-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/posix tests"
			-P "${CMAKE_CURRENT_SOURCE_DIR}/posix.cmake")
		set_tests_properties(ci_posix PROPERTIES TIMEOUT 600)
	endif()
endif()

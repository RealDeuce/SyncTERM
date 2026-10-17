# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)
project(CIDriverFixture C)
include(CTest)
if(CI_FIXTURE_CONFIGURE_FAIL)
	message(FATAL_ERROR "fixture configure failure")
endif()
add_executable(syncterm_app main.c)
set_target_properties(syncterm_app PROPERTIES
	OUTPUT_NAME syncterm RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/app"
	C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(CI_FIXTURE_BUILD_FAIL)
	add_custom_target(fail_build ALL COMMAND "${CMAKE_COMMAND}" -E false)
endif()
if(BUILD_TESTING AND NOT CI_FIXTURE_NO_TESTS)
	if(CI_FIXTURE_TEST_FAIL)
		add_test(NAME fixture COMMAND syncterm_app fail)
	else()
		add_test(NAME fixture COMMAND syncterm_app)
	endif()
	add_test(NAME fixture_unavailable COMMAND syncterm_app skip)
	set_tests_properties(fixture_unavailable PROPERTIES SKIP_RETURN_CODE 77)
endif()

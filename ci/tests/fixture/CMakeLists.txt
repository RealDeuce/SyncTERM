# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)
project(CIDriverFixture C)
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCStandard.cmake")
include(CTest)
option(ST_CONSOLE_TEST "Fixture test instrumentation" OFF)
if(CI_FIXTURE_CONFIGURE_FAIL OR (CI_FIXTURE_ARTIFACT_CONFIGURE_FAIL AND NOT ST_CONSOLE_TEST))
	file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/CMakeFiles")
	file(WRITE "${CMAKE_BINARY_DIR}/CMakeFiles/CMakeError.log" "fixture probe diagnostic\n")
	message(FATAL_ERROR "fixture configure failure")
endif()
add_executable(syncterm_app main.c)
set_target_properties(syncterm_app PROPERTIES
	OUTPUT_NAME syncterm RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermMacOSBundle.cmake")
syncterm_macos_bundle(syncterm_app)
target_compile_definitions(syncterm_app PRIVATE
	ST_CONSOLE_TEST=$<BOOL:${ST_CONSOLE_TEST}> BUILD_TESTING=$<BOOL:${BUILD_TESTING}>)
add_library(syncterm_core MODULE core.c)
set_target_properties(syncterm_core PROPERTIES OUTPUT_NAME syncterm_libretro PREFIX ""
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
set_target_properties(syncterm_core PROPERTIES
	LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(_app "$<TARGET_FILE:syncterm_app>")
if(APPLE)
	set(_app "$<TARGET_BUNDLE_DIR:syncterm_app>")
elseif(CI_FIXTURE_BUNDLE)
	# Exercise recursive bundle collection on hosts without an Apple SDK.
	set(_app "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/SyncTERM.app")
	add_custom_command(TARGET syncterm_app POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E make_directory
			"${_app}/Contents/MacOS" "${_app}/Contents/Resources"
		COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:syncterm_app>"
			"${_app}/Contents/MacOS/SyncTERM"
		COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_LIST_DIR}/main.c"
			"${_app}/Contents/Info.plist"
		COMMAND "${CMAKE_COMMAND}" -E copy
			"${CMAKE_CURRENT_LIST_DIR}/../../../resources/macos/SyncTERM.icns"
			"${_app}/Contents/Resources/SyncTERM.icns" VERBATIM)
endif()
if(CI_FIXTURE_INCOMPLETE_BUNDLE)
	add_custom_command(TARGET syncterm_app POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E rm -f
			"${_app}/Contents/Resources/SyncTERM.icns" VERBATIM)
endif()
if(CI_FIXTURE_DIRECTORY_PRODUCT)
	set(_app "${CMAKE_BINARY_DIR}/bin")
endif()
set(_products "${_app}\n$<TARGET_FILE:syncterm_core>\n")
if(CI_FIXTURE_MISSING_PRODUCT)
	string(APPEND _products "${CMAKE_BINARY_DIR}/missing-product\n")
elseif(CI_FIXTURE_EMPTY_MANIFEST)
	set(_products "")
endif()
if(NOT CI_FIXTURE_MISSING_MANIFEST)
	file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/syncterm-artifacts-$<CONFIG>.txt"
		CONTENT "${_products}")
endif()
if(CI_FIXTURE_BUILD_FAIL OR (CI_FIXTURE_ARTIFACT_BUILD_FAIL AND NOT ST_CONSOLE_TEST))
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

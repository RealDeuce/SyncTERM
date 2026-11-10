# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)
project(MacOSBundleFixture C)
include("${CMAKE_CURRENT_LIST_DIR}/../../SynctermCStandard.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../../SynctermMacOSBundle.cmake")
add_executable(application "${CMAKE_CURRENT_LIST_DIR}/../../../ci/tests/fixture/main.c")
target_compile_definitions(application PRIVATE ST_CONSOLE_TEST=0 BUILD_TESTING=0)
set_target_properties(application PROPERTIES OUTPUT_NAME syncterm
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
syncterm_macos_bundle(application)
if(APPLE)
	set(_product "$<TARGET_BUNDLE_DIR:application>")
else()
	set(_product "$<TARGET_FILE:application>")
endif()
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/product-$<CONFIG>.txt" CONTENT "${_product}\n")

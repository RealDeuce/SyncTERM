# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)
if(NOT DEFINED CI_BUILD_DIR OR NOT PACKAGE_NAME MATCHES "^syncterm-[a-z0-9_-]+$")
	message(FATAL_ERROR "CI_BUILD_DIR and a syncterm-<platform> PACKAGE_NAME are required")
endif()
get_filename_component(CI_BUILD_DIR "${CI_BUILD_DIR}" ABSOLUTE)
file(GLOB _products "${CI_BUILD_DIR}/artifacts/*")
if(NOT _products)
	message(FATAL_ERROR "Missing CI products")
endif()
set(_stage "${CI_BUILD_DIR}/packages/${PACKAGE_NAME}")
file(REMOVE_RECURSE "${_stage}")
file(MAKE_DIRECTORY "${_stage}")
file(COPY ${_products} DESTINATION "${_stage}")
file(GLOB _entries RELATIVE "${_stage}" "${_stage}/*")
# Both CI services use this layout. libarchive retains executable permissions
# and the complete macOS bundle.
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${CI_BUILD_DIR}/packages/${PACKAGE_NAME}.zip"
	--format=zip -- ${_entries}
	WORKING_DIRECTORY "${_stage}" COMMAND_ERROR_IS_FATAL ANY)

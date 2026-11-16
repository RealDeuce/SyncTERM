# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)
get_filename_component(_project "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(NOT DEFINED CI_BUILD_DIR OR NOT PACKAGE_NAME MATCHES "^syncterm-[a-z0-9_-]+$")
	message(FATAL_ERROR "CI_BUILD_DIR and a syncterm-<platform> PACKAGE_NAME are required")
endif()
get_filename_component(CI_BUILD_DIR "${CI_BUILD_DIR}" ABSOLUTE)
file(GLOB _products "${CI_BUILD_DIR}/artifacts/*")
if(NOT _products OR NOT EXISTS "${CI_BUILD_DIR}/reports/artifact-configuration.txt")
	message(FATAL_ERROR "Missing CI products or configuration report")
endif()
set(_stage "${CI_BUILD_DIR}/packages/${PACKAGE_NAME}")
file(REMOVE_RECURSE "${_stage}")
file(MAKE_DIRECTORY "${_stage}")
file(COPY ${_products} "${_project}/LICENSE" DESTINATION "${_stage}")
file(COPY "${CI_BUILD_DIR}/reports/artifact-configuration.txt" DESTINATION "${_stage}")
# Preserve paths for notices with identical basenames and include notices for
# bundled dependencies whose code is statically linked into these products.
file(GLOB_RECURSE _notices RELATIVE "${_project}/src"
	"${_project}/src/LICENSE*" "${_project}/src/COPYING*"
	"${_project}/src/NOTICE*" "${_project}/src/*LICENSES*")
foreach(_notice IN LISTS _notices)
	get_filename_component(_directory "${_notice}" DIRECTORY)
	file(MAKE_DIRECTORY "${_stage}/licenses/src/${_directory}")
	file(COPY "${_project}/src/${_notice}" DESTINATION "${_stage}/licenses/src/${_directory}")
endforeach()
file(GLOB _dependency_license_dirs RELATIVE "${CI_BUILD_DIR}/dependencies"
	"${CI_BUILD_DIR}/dependencies/*/*/share/licenses")
foreach(_directory IN LISTS _dependency_license_dirs)
	file(MAKE_DIRECTORY "${_stage}/licenses/dependencies/${_directory}")
	file(COPY "${CI_BUILD_DIR}/dependencies/${_directory}/"
		DESTINATION "${_stage}/licenses/dependencies/${_directory}")
endforeach()
file(GLOB _ssh_notices "${CI_BUILD_DIR}/_deps/deucessh-src/LICENSE*")
if(_ssh_notices)
	file(COPY ${_ssh_notices} DESTINATION "${_stage}/licenses/DeuceSSH")
endif()
file(WRITE "${_stage}/BUILD.txt"
	"Development build; not a stable release.\nCommit: $ENV{GITHUB_SHA}\n"
	"Build: $ENV{GITHUB_SERVER_URL}/$ENV{GITHUB_REPOSITORY}/actions/runs/$ENV{GITHUB_RUN_ID}\n"
	"See artifact-configuration.txt for compiled features and dependencies.\n"
	"Optional runtime libraries may need to be installed separately.\n")
# libarchive retains executable permissions and the complete macOS bundle.
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${PACKAGE_NAME}.zip"
	--format=zip "${PACKAGE_NAME}"
	WORKING_DIRECTORY "${CI_BUILD_DIR}/packages" COMMAND_ERROR_IS_FATAL ANY)

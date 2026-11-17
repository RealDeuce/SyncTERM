# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)
if(NOT DEFINED TEST_ROOT)
	message(FATAL_ERROR "TEST_ROOT is required")
endif()
set(_build "${TEST_ROOT}/build with spaces")
file(MAKE_DIRECTORY "${_build}/artifacts" "${_build}/reports"
	"${_build}/dependencies/example-1/Release/share/licenses/EXAMPLE")
file(WRITE "${_build}/artifacts/syncterm" "fixture binary\n")
file(CHMOD "${_build}/artifacts/syncterm"
	PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
file(WRITE "${_build}/artifacts/syncterm_libretro.so" "fixture core\n")
file(WRITE "${_build}/reports/artifact-configuration.txt" "fixture configuration\n")
file(WRITE "${_build}/dependencies/example-1/Release/share/licenses/EXAMPLE/LICENSE" "fixture license\n")
set(_bundle "${_build}/artifacts/SyncTERM.app/Contents")
file(MAKE_DIRECTORY "${_bundle}/MacOS" "${_bundle}/Resources")
file(WRITE "${_bundle}/MacOS/SyncTERM" "fixture app\n")
file(WRITE "${_bundle}/Info.plist" "fixture plist\n")
file(WRITE "${_bundle}/Resources/SyncTERM.icns" "fixture icon\n")
set(_script "${CMAKE_CURRENT_LIST_DIR}/../package.cmake")
set(_stage "${_build}/packages/syncterm-fixture")
file(MAKE_DIRECTORY "${_stage}/licenses/src/old")
file(WRITE "${_stage}/licenses/src/old/LICENSE" "stale license\n")
file(WRITE "${_stage}/BUILD.txt" "stale build information\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DCI_BUILD_DIR=${_build}"
	-DPACKAGE_NAME=syncterm-fixture -P "${_script}"
	COMMAND_ERROR_IS_FATAL ANY)
set(_extract "${TEST_ROOT}/extract")
file(REMOVE_RECURSE "${_extract}")
file(MAKE_DIRECTORY "${_extract}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar xf "${_build}/packages/syncterm-fixture.zip"
	WORKING_DIRECTORY "${_extract}" COMMAND_ERROR_IS_FATAL ANY)
set(_expected
	SyncTERM.app SyncTERM.app/Contents SyncTERM.app/Contents/MacOS SyncTERM.app/Contents/Resources
	SyncTERM.app/Contents/MacOS/SyncTERM SyncTERM.app/Contents/Info.plist
	SyncTERM.app/Contents/Resources/SyncTERM.icns syncterm syncterm_libretro.so)
file(GLOB_RECURSE _contents LIST_DIRECTORIES TRUE RELATIVE "${_extract}" "${_extract}/*")
list(SORT _expected)
list(SORT _contents)
if(NOT _contents STREQUAL _expected)
	message(FATAL_ERROR "Package must contain only products at its root: ${_contents}")
endif()
if(NOT WIN32)
	execute_process(COMMAND test -x "${_extract}/syncterm" COMMAND_ERROR_IS_FATAL ANY)
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" "-DCI_BUILD_DIR=${TEST_ROOT}/empty-build"
	-DPACKAGE_NAME=syncterm-fixture -P "${_script}"
	RESULT_VARIABLE _result OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(_result STREQUAL "0" OR NOT _err MATCHES "Missing CI products")
	message(FATAL_ERROR "Packaging accepted an incomplete CI result: ${_out}${_err}")
endif()

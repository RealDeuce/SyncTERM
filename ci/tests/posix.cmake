# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)
get_filename_component(_ci "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
find_program(_sh sh REQUIRED)
find_program(_kill kill REQUIRED)
foreach(_profile IN ITEMS native invalid)
	set(_build "${TEST_ROOT}/${_profile}")
	file(MAKE_DIRECTORY "${_build}/reports")
	file(WRITE "${_build}/reports/xvfb-display" "do-not-use\n")
	execute_process(COMMAND "${CMAKE_COMMAND}" -E env
		"CI_SOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}/fixture"
		"CI_BUILD_DIR=${_build}" "CI_PROFILE=${_profile}"
		"CI_EXTRA_CMAKE_ARGS=" "DISPLAY=:do-not-use"
		"WAYLAND_DISPLAY=do-not-use" "CTEST_PARALLEL_LEVEL=1"
		"${_sh}" "${_ci}/posix.sh"
		RESULT_VARIABLE _result OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
	if(_profile STREQUAL native)
		if(NOT "${_result}" STREQUAL "0")
			message(FATAL_ERROR "POSIX wrapper: ${_out}\n${_err}")
		endif()
		file(READ "${_build}/reports/environment.txt" _environment)
		if(_environment MATCHES "do-not-use")
			message(FATAL_ERROR "POSIX wrapper used an inherited desktop")
		endif()
		if(NOT _environment MATCHES "XDG_RUNTIME_DIR: ([^\n]+)")
			message(FATAL_ERROR "POSIX wrapper did not create a private runtime directory")
		endif()
		if(EXISTS "${CMAKE_MATCH_1}")
			message(FATAL_ERROR "POSIX wrapper left its runtime directory behind")
		endif()
	elseif("${_result}" STREQUAL "0" OR NOT "${_err}" MATCHES "Unknown CI_PROFILE")
		message(FATAL_ERROR "POSIX wrapper did not propagate failure: ${_out}\n${_err}")
	endif()
	foreach(_server IN ITEMS xvfb weston)
		if(EXISTS "${_build}/reports/${_server}.pid")
			file(STRINGS "${_build}/reports/${_server}.pid" _pid)
			execute_process(COMMAND "${_kill}" -0 "${_pid}"
				RESULT_VARIABLE _alive OUTPUT_QUIET ERROR_QUIET)
			if("${_alive}" STREQUAL "0")
				message(FATAL_ERROR "POSIX wrapper left its ${_server} running")
			endif()
		endif()
	endforeach()
endforeach()
message(STATUS "POSIX wrapper clears inherited displays and cleans up on success/failure")

# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)
get_filename_component(_ci "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
find_program(_sh sh REQUIRED)
find_program(_kill kill REQUIRED)
find_program(_rmdir rmdir REQUIRED)
set(_tools "${TEST_ROOT}/tools")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/fixture/ccache"
	"${CMAKE_CURRENT_LIST_DIR}/fixture/rmdir" DESTINATION "${_tools}"
	FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ
	GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
get_filename_component(_parent "${TEST_ROOT}" DIRECTORY)
foreach(_case IN ITEMS native invalid cleanup-failure build-only)
	set(_profile "${_case}")
	set(_mode full-test)
	if(_case STREQUAL build-only)
		set(_profile native)
		# An empty mode exercises the default used by ordinary pipelines.
		set(_mode "")
	endif()
	set(_fail_cleanup 0)
	if(_case STREQUAL cleanup-failure)
		set(_profile native)
		set(_fail_cleanup 1)
	endif()
	set(_build "${TEST_ROOT}/ccache-${_case}")
	# Exercise the launcher on every run, even in a reused fixture build.
	if(EXISTS "${_build}/CMakeCache.txt")
		execute_process(COMMAND "${CMAKE_COMMAND}" --build "${_build}" --target clean
			RESULT_VARIABLE _clean OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
		if(NOT "${_clean}" STREQUAL "0")
			message(FATAL_ERROR "Fixture clean failed: ${_out}\n${_err}")
		endif()
	endif()
	file(REMOVE "${_build}/ccache-tmp/fixture-invocations.log")
	file(MAKE_DIRECTORY "${_build}/reports")
	file(WRITE "${_build}/reports/xvfb-display" "do-not-use\n")
	execute_process(COMMAND "${CMAKE_COMMAND}" -E env
		"PATH=${_tools}:$ENV{PATH}" "CCACHE_TEMPDIR="
		"CI_TEST_REAL_RMDIR=${_rmdir}"
		"CI_TEST_FAIL_RUNTIME_CLEANUP=${_fail_cleanup}"
		"CI_SOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}/fixture"
		"CI_BUILD_DIR=${_build}" "CI_PROFILE=${_profile}"
		"SYNCTERM_CI_MODE=${_mode}"
		"CI_EXTRA_CMAKE_ARGS=" "DISPLAY=:do-not-use"
		"WAYLAND_DISPLAY=do-not-use" "CTEST_PARALLEL_LEVEL=1"
		"${_sh}" "${_ci}/posix.sh"
		RESULT_VARIABLE _result OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
	# Keep nested wrapper diagnostics in the check job's artifact directory.
	file(COPY "${_build}/reports/" DESTINATION "${_parent}/reports/posix-${_case}")
	if(_case STREQUAL native OR _case STREQUAL build-only)
		if(NOT "${_result}" STREQUAL "0")
			message(FATAL_ERROR "POSIX wrapper: ${_out}\n${_err}")
		endif()
		file(READ "${_build}/reports/environment.txt" _environment)
		if(_environment MATCHES "do-not-use")
			message(FATAL_ERROR "POSIX wrapper used an inherited desktop")
		endif()
		if(_case STREQUAL build-only)
			if(_environment MATCHES "XDG_RUNTIME_DIR: [^\n]+" OR EXISTS "${_build}/reports/junit.xml")
				message(FATAL_ERROR "Build-only wrapper started test infrastructure")
			endif()
		elseif(NOT _environment MATCHES "XDG_RUNTIME_DIR: ([^\n]+)")
			message(FATAL_ERROR "POSIX wrapper did not create a private runtime directory")
		else()
			set(_runtime "${CMAKE_MATCH_1}")
			if(EXISTS "${_runtime}")
				file(GLOB _leftovers LIST_DIRECTORIES TRUE "${_runtime}/*" "${_runtime}/.[!.]*")
				message(FATAL_ERROR "POSIX wrapper left ${_runtime} behind: ${_leftovers}\n${_err}")
			endif()
		endif()
		if(NOT EXISTS "${_build}/ccache-tmp/fixture-invocations.log")
			message(FATAL_ERROR "ccache was not exercised outside the runtime directory")
		endif()
	elseif(_case STREQUAL cleanup-failure)
		if("${_result}" STREQUAL "0" OR NOT _err MATCHES "fixture: forced runtime cleanup failure"
		    OR NOT _err MATCHES "Runtime directory:")
			message(FATAL_ERROR "Cleanup failure was hidden: ${_out}\n${_err}")
		endif()
		file(READ "${_parent}/reports/posix-${_case}/runtime-cleanup.log" _cleanup_log)
		if(NOT _cleanup_log MATCHES "fixture: forced runtime cleanup failure"
		    OR NOT _cleanup_log MATCHES "Runtime directory:")
			message(FATAL_ERROR "Cleanup failure diagnostics were not retained")
		endif()
		file(READ "${_build}/reports/environment.txt" _environment)
		if(NOT _environment MATCHES "XDG_RUNTIME_DIR: ([^\n]+)")
			message(FATAL_ERROR "Missing failed cleanup path")
		endif()
		set(_runtime "${CMAKE_MATCH_1}")
		if(NOT IS_ABSOLUTE "${_runtime}" OR NOT _runtime MATCHES "/syncterm-ci\\.[^/]+$")
			message(FATAL_ERROR "Unexpected fixture runtime directory: ${_runtime}")
		endif()
		# Only the empty, freshly-created fixture directory; no recursion.
		execute_process(COMMAND "${_rmdir}" "${_runtime}/ccache-tmp" "${_runtime}"
			RESULT_VARIABLE _removed)
		if(NOT "${_removed}" STREQUAL "0")
			message(FATAL_ERROR "Could not remove failed-cleanup fixture ${_runtime}")
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

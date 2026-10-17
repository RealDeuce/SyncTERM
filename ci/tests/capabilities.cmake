# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)
foreach(_case IN ITEMS native c11 atomics_switch atomics_missing)
	set(_build "${TEST_ROOT}/${_case}")
	execute_process(COMMAND "${CMAKE_COMMAND}"
		-S "${CMAKE_CURRENT_LIST_DIR}/capability_fixture" -B "${_build}"
		"-DCAPABILITY_CASE=${_case}"
		RESULT_VARIABLE _result OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
	if(_case STREQUAL atomics_missing)
		if("${_result}" STREQUAL "0" OR NOT _err MATCHES "required atomic operations")
			message(FATAL_ERROR "Missing atomics wrongly accepted: ${_out}\n${_err}")
		endif()
	elseif(NOT "${_result}" STREQUAL "0")
		message(FATAL_ERROR "${_case}: ${_out}\n${_err}")
	elseif(NOT _case STREQUAL atomics_switch)
		execute_process(COMMAND "${CMAKE_COMMAND}" --build "${_build}" --config Release
			RESULT_VARIABLE _result OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
		if(NOT "${_result}" STREQUAL "0")
			message(FATAL_ERROR "${_case}: ${_out}\n${_err}")
		endif()
	endif()
endforeach()

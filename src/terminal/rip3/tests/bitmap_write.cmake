# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
file(REMOVE_RECURSE "${TEST_DIRECTORY}")
execute_process(COMMAND "${TEST_PROGRAM}" "${TEST_DIRECTORY}"
	RESULT_VARIABLE _result OUTPUT_VARIABLE _output ERROR_VARIABLE _error)
file(REMOVE_RECURSE "${TEST_DIRECTORY}")
if(NOT _result STREQUAL "0")
	message(FATAL_ERROR "bitmap export failed (${_result}): ${_output}${_error}")
endif()

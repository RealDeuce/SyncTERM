# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
file(REMOVE_RECURSE "${TEST_DIRECTORY}")
foreach(name IN ITEMS shared search fonts outside)
	file(MAKE_DIRECTORY "${TEST_DIRECTORY}/${name}")
endforeach()
file(WRITE "${TEST_DIRECTORY}/outside/Keep.BMP" "outside")
file(CREATE_LINK "${TEST_DIRECTORY}/outside/Keep.BMP"
	"${TEST_DIRECTORY}/Link.BMP" SYMBOLIC RESULT _existing)
file(CREATE_LINK "${TEST_DIRECTORY}/outside/Missing.BMP"
	"${TEST_DIRECTORY}/Dangling.BMP" SYMBOLIC RESULT _dangling)
set(_links "no-links")
if(_existing STREQUAL "0" AND _dangling STREQUAL "0")
	set(_links "links")
endif()
message(STATUS "Symbolic-link fixture: ${_links}")
execute_process(COMMAND "${TEST_PROGRAM}" "${TEST_DIRECTORY}" "${_links}"
	RESULT_VARIABLE _result OUTPUT_VARIABLE _output ERROR_VARIABLE _error)
file(REMOVE_RECURSE "${TEST_DIRECTORY}")
message(STATUS "${_output}")
if(NOT _result STREQUAL "0")
	message(FATAL_ERROR "resource writer failed (${_result}): ${_error}")
endif()

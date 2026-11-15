# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
file(MAKE_DIRECTORY "${TEST_DIRECTORY}")
set(payload "font data\n")
string(SHA256 checksum "${payload}")
file(WRITE "${TEST_DIRECTORY}/font.bin" "${payload}")
file(WRITE "${TEST_DIRECTORY}/manifest.json"
	"{\"files\":[{\"name\":\"font.bin\",\"size\":10,\"sha256\":\"${checksum}\"}]}\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DASSET_DIRECTORY=${TEST_DIRECTORY}"
	"-DOUTPUT=${TEST_DIRECTORY}/good.c" -P "${GENERATOR}"
	RESULT_VARIABLE result ERROR_VARIABLE error)
if(NOT result EQUAL 0)
	message(FATAL_ERROR "Valid pinned asset rejected: ${error}")
endif()
foreach(changed IN ITEMS "font datA\n" "truncated")
	file(WRITE "${TEST_DIRECTORY}/font.bin" "${changed}")
	execute_process(COMMAND "${CMAKE_COMMAND}" "-DASSET_DIRECTORY=${TEST_DIRECTORY}"
		"-DOUTPUT=${TEST_DIRECTORY}/bad.c" -P "${GENERATOR}"
		RESULT_VARIABLE result ERROR_VARIABLE error)
	if(result EQUAL 0 OR NOT error MATCHES "Pinned RIP3 font asset changed")
		message(FATAL_ERROR "Modified pinned asset not rejected: ${error}")
	endif()
	if(EXISTS "${TEST_DIRECTORY}/bad.c")
		message(FATAL_ERROR "Failed verification published a generated asset")
	endif()
endforeach()

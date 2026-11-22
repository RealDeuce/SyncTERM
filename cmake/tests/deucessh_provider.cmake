# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
file(MAKE_DIRECTORY "${TEST_ROOT}")
find_package(Git REQUIRED)
function(run)
	execute_process(COMMAND ${ARGV}
		RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
	if(NOT result EQUAL 0)
		message(FATAL_ERROR "DeuceSSH provider command failed: ${ARGV}\n${output}\n${error}")
	endif()
endfunction()

# Exercise the real FetchContent declaration with a local repository so an
# upgrade never needs network access. Preserve all options except URL and pin.
set(repository "${TEST_ROOT}/repository")
file(MAKE_DIRECTORY "${repository}" "${TEST_ROOT}/modules")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/deucessh_provider/dependency/"
	DESTINATION "${repository}")
run("${GIT_EXECUTABLE}" init "${repository}")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/../deucessh" DESTINATION "${TEST_ROOT}/modules")
file(READ "${CMAKE_CURRENT_LIST_DIR}/../DeuceSSH.cmake" integration)
string(REPLACE "https://github.com/RealDeuce/DeuceSSH.git" "${repository}"
	integration "${integration}")
foreach(revision first second)
	file(APPEND "${repository}/CMakeLists.txt" "\n# ${revision} revision\n")
	run("${GIT_EXECUTABLE}" -C "${repository}" add CMakeLists.txt consumer.c)
	run("${GIT_EXECUTABLE}" -C "${repository}"
		-c user.name=Fixture -c user.email=fixture@example.invalid
		-c commit.gpgsign=false commit -m "${revision}")
	execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${repository}" rev-parse HEAD
		OUTPUT_VARIABLE pin OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
	string(REGEX REPLACE "GIT_TAG [0-9a-f]+" "GIT_TAG ${pin}" current "${integration}")
	file(WRITE "${TEST_ROOT}/modules/DeuceSSH.cmake" "${current}")
	file(REMOVE "${TEST_ROOT}/build/provider-include/provider.h")
	run("${CMAKE_COMMAND}" -S "${CMAKE_CURRENT_LIST_DIR}/deucessh_provider"
		-B "${TEST_ROOT}/build" "-DMODULES=${TEST_ROOT}/modules")
	run("${CMAKE_COMMAND}" --build "${TEST_ROOT}/build" --config Debug)
	execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${TEST_ROOT}/build/_deps/deucessh-src"
		status --porcelain OUTPUT_VARIABLE changes COMMAND_ERROR_IS_FATAL ANY)
	if(changes)
		message(FATAL_ERROR "DeuceSSH ${revision} checkout was modified: ${changes}")
	endif()
endforeach()

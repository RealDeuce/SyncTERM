# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)
# Visual Studio is exercised by the main source fixture on Windows. Ninja's
# multi-config generator gives POSIX hosts an independent configuration check.
if(CMAKE_HOST_WIN32)
	return()
endif()
find_program(test_multi_ninja NAMES ninja ninja-build)
if(NOT test_multi_ninja)
	message(STATUS "Skipping multi-config source fixture: Ninja is not available")
	return()
endif()
if(NOT MULTI_ROOT)
	set(MULTI_ROOT "${TEST_ROOT}/multi-config")
endif()
# This check needs an initially unbuilt Debug configuration on every run.
file(REMOVE_RECURSE "${MULTI_ROOT}/build")
file(MAKE_DIRECTORY "${MULTI_ROOT}")
file(WRITE "${MULTI_ROOT}/initial.cmake"
	"set(CMAKE_CONFIGURATION_TYPES Debug RelWithDebInfo CACHE STRING \"\" FORCE)\n")
function(multi_run label)
	execute_process(COMMAND ${ARGN} RESULT_VARIABLE result
		OUTPUT_FILE "${MULTI_ROOT}/${label}.log" ERROR_FILE "${MULTI_ROOT}/${label}.err")
	if(NOT result STREQUAL "0")
		message(FATAL_ERROR "Multi-config ${label} failed (${result}); see ${MULTI_ROOT}/${label}.log/.err")
	endif()
endfunction()
multi_run(configure "${CMAKE_COMMAND}" -S "${TEST_ROOT}/consumer" -B "${MULTI_ROOT}/build"
	-G "Ninja Multi-Config" -C "${MULTI_ROOT}/initial.cmake"
	"-DMODULES=${TEST_ROOT}/modules" -DSYNCTERM_DEPENDENCY_DOWNLOADS=OFF
	"-DST_DEP_FIXTURE_ARCHIVE=${TEST_ROOT}/fixture.tar" "-DST_DEP_CHILD_ARCHIVE=${TEST_ROOT}/child.tar")
multi_run(release-build "${CMAKE_COMMAND}" --build "${MULTI_ROOT}/build" --config RelWithDebInfo --parallel 2)
multi_run(release-execute "${MULTI_ROOT}/build/RelWithDebInfo/consumer")
if(EXISTS "${MULTI_ROOT}/build/dependencies/child-1/Debug/include/child.h")
	message(FATAL_ERROR "RelWithDebInfo built an unrequested Debug dependency")
endif()
multi_run(debug-build "${CMAKE_COMMAND}" --build "${MULTI_ROOT}/build" --config Debug --parallel 2)
multi_run(debug-execute "${MULTI_ROOT}/build/Debug/consumer")

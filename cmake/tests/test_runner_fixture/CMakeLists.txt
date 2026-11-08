# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)
project(TestRunnerFixture C)
include(CTest)
include("${CMAKE_CURRENT_LIST_DIR}/../../SynctermCStandard.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../../SynctermTestRunner.cmake")

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/dependency.c" "int dependency(void) { return 7; }\n")
add_library(dependency STATIC "${CMAKE_CURRENT_BINARY_DIR}/dependency.c")
set_target_properties(dependency PROPERTIES
	C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/first${CMAKE_EXECUTABLE_SUFFIX}" "obsolete executable")
foreach(config IN LISTS CMAKE_CONFIGURATION_TYPES)
	file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${config}")
	file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${config}/first${CMAKE_EXECUTABLE_SUFFIX}" "obsolete executable")
endforeach()
syncterm_add_test_case(runner first first.c)
if(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/first${CMAKE_EXECUTABLE_SUFFIX}")
	message(FATAL_ERROR "Obsolete test executable was retained")
endif()
foreach(config IN LISTS CMAKE_CONFIGURATION_TYPES)
	if(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/${config}/first${CMAKE_EXECUTABLE_SUFFIX}")
		message(FATAL_ERROR "Obsolete ${config} test executable was retained")
	endif()
endforeach()
target_compile_definitions(first PRIVATE CASE_VALUE=1)
target_link_libraries(first PRIVATE dependency)
set_target_properties(first PROPERTIES
	C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
add_test(NAME first COMMAND runner first)
add_test(NAME first_again COMMAND runner first)
add_subdirectory(second)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/working directory")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/working directory/marker" "present")
add_test(NAME arguments COMMAND runner arguments "space argument" --marker)
set_tests_properties(arguments PROPERTIES
	ENVIRONMENT "CASE_ENV=present"
	WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/working directory")

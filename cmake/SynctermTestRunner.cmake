# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

include_guard(GLOBAL)

# Keep each case's compilation environment on its own object target. CTest
# starts a fresh runner process for each case, so globals and overrides do not
# acquire a different lifetime merely because the executable is shared.
# Cases that replace production symbols must remain in separate executables.
function(syncterm_add_test_case runner target)
	cmake_parse_arguments(CASE "ARGS" "" "" ${ARGN})
	if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_CURRENT_BINARY_DIR)
		# Retire the old standalone executables in an existing build tree.
		file(REMOVE "${CMAKE_CURRENT_BINARY_DIR}/${target}${CMAKE_EXECUTABLE_SUFFIX}")
		foreach(_config IN LISTS CMAKE_CONFIGURATION_TYPES)
			file(REMOVE "${CMAKE_CURRENT_BINARY_DIR}/${_config}/${target}${CMAKE_EXECUTABLE_SUFFIX}")
		endforeach()
	endif()
	if(NOT TARGET ${runner})
		set(_source "${CMAKE_BINARY_DIR}/tests/${runner}.c")
		add_executable(${runner} "${_source}")
		set_target_properties(${runner} PROPERTIES
			C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO
			RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
		if(MSVC)
			target_compile_options(${runner} PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
		else()
			target_compile_options(${runner} PRIVATE
				-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
		endif()
		# A runner can collect cases from several component directories.
		cmake_language(EVAL CODE
			"cmake_language(DEFER DIRECTORY [==[${CMAKE_SOURCE_DIR}]==] CALL syncterm_finish_test_runner [==[${runner}]==])")
	endif()
	add_library(${target} OBJECT ${CASE_UNPARSED_ARGUMENTS})
	set_property(TARGET ${target} PROPERTY SYNCTERM_TEST_CASE_ARGS ${CASE_ARGS})
	set_property(TARGET ${runner} APPEND PROPERTY SYNCTERM_TEST_CASES ${target})
	target_link_libraries(${runner} PRIVATE ${target})
endfunction()

function(syncterm_finish_test_runner runner)
	get_property(_cases TARGET ${runner} PROPERTY SYNCTERM_TEST_CASES)
	set(_source "/* Generated test dispatcher. Each invocation runs one case. */\n#include <stdio.h>\n#include <string.h>\n\n")
	foreach(_case IN LISTS _cases)
		get_property(_args TARGET ${_case} PROPERTY SYNCTERM_TEST_CASE_ARGS)
		if(_args)
			string(APPEND _source "int ${_case}_main(int argc, char **argv);\n")
		else()
			string(APPEND _source "int ${_case}_main(void);\n")
		endif()
	endforeach()
	string(APPEND _source "\nint main(int argc, char **argv)\n{\n\tif (argc < 2) {\n\t\tfprintf(stderr, \"A test case is required.\\n\");\n\t\treturn 2;\n\t}\n")
	foreach(_case IN LISTS _cases)
		get_property(_args TARGET ${_case} PROPERTY SYNCTERM_TEST_CASE_ARGS)
		string(APPEND _source "\tif (strcmp(argv[1], \"${_case}\") == 0) {\n")
		if(_args)
			string(APPEND _source "\t\treturn ${_case}_main(argc - 1, argv + 1);\n")
		else()
			string(APPEND _source "\t\tif (argc != 2) {\n\t\t\tfprintf(stderr, \"This test case takes no arguments.\\n\");\n\t\t\treturn 2;\n\t\t}\n\t\treturn ${_case}_main();\n")
		endif()
		string(APPEND _source "\t}\n")
	endforeach()
	string(APPEND _source "\tfprintf(stderr, \"Unknown test case: %s\\n\", argv[1]);\n\treturn 2;\n}\n")
	file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/tests/${runner}.c" CONTENT "${_source}")
endfunction()

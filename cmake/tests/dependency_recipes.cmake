# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)

function(recipe_run label)
	execute_process(COMMAND ${ARGN} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
	if(NOT result STREQUAL "0")
		message(FATAL_ERROR "${label} failed (${result}):\n${output}\n${errors}")
	endif()
endfunction()

function(test_dependency_recipes)
	if(TEST_RECIPE_CASE AND NOT TEST_RECIPE_CASE MATCHES "^(environment|botan_make|botan_patch)$")
		message(FATAL_ERROR "Unknown dependency recipe test case: ${TEST_RECIPE_CASE}")
	endif()
	if(NOT RECIPE_MODULE_DIR)
		set(RECIPE_MODULE_DIR "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../dependencies")
	endif()
	set(root "${TEST_ROOT}/recipe-fixtures")
	file(MAKE_DIRECTORY "${root}")
	if(NOT TEST_RECIPE_CASE OR TEST_RECIPE_CASE STREQUAL "environment")
		# Simulate Windows' path separator while using the native host generator.
		# The child must receive one literal environment value, including spaces.
		file(MAKE_DIRECTORY "${root}/environment/source" "${root}/generator")
		file(WRITE "${root}/generator/CMakeLists.txt"
			"cmake_minimum_required(VERSION 3.22)\nproject(Generator NONE)\n")
		recipe_run(generator "${CMAKE_COMMAND}" -S "${root}/generator" -B "${root}/generator/build")
		file(STRINGS "${root}/generator/build/CMakeCache.txt" generator_line REGEX "^CMAKE_GENERATOR:INTERNAL=")
		string(REPLACE "CMAKE_GENERATOR:INTERNAL=" "" generator "${generator_line}")
		set(pkg_path "${root}/prefix one/lib/pkgconfig;${root}/prefix one/share/pkgconfig;${root}/prefix two/lib/pkgconfig;${root}/prefix two/share/pkgconfig")
		file(WRITE "${root}/environment/source/CMakeLists.txt"
			"cmake_minimum_required(VERSION 3.22)\nproject(Environment NONE)\nset(expected [==[${pkg_path}]==])\n")
		file(APPEND "${root}/environment/source/CMakeLists.txt" [=[
if(NOT "$ENV{PKG_CONFIG_PATH}" STREQUAL "${expected}")
  message(FATAL_ERROR "Dependency pkg-config search path was not preserved")
endif()
]=])
		file(WRITE "${root}/environment/settings.cmake"
			"set(kind cmake)\nset(name FIXTURE)\nset(config Debug)\nset(mode STATIC)\nset(WIN32 TRUE)\nset(CMAKE_HOST_SYSTEM_NAME Windows)\nset(CMAKE_GENERATOR [==[${generator}]==])\nset(prefix [==[${root}/installed]==])\nset(dep_prefixes [==[${root}/prefix one;${root}/prefix two]==])\n")
		recipe_run(environment "${CMAKE_COMMAND}" "-DSETTINGS=${root}/environment/settings.cmake"
			"-DSOURCE=${root}/environment/source" "-DWORK=${root}/environment/work" -DSTEP=configure
			-P "${RECIPE_MODULE_DIR}/Build.cmake")
	endif()
	# The generic source fixtures also run on hosts without optional Python.
	find_program(recipe_python NAMES python3 python)
	if(NOT CMAKE_HOST_WIN32 AND recipe_python AND (NOT TEST_RECIPE_CASE OR TEST_RECIPE_CASE STREQUAL "botan_make"))
		find_program(recipe_make NAMES make gmake REQUIRED)
		get_filename_component(cmake_bin "${CMAKE_COMMAND}" DIRECTORY)
		set(tools "${root}/make/tools")
		file(MAKE_DIRECTORY "${tools}" "${root}/make/source")
		file(CREATE_LINK "${recipe_python}" "${tools}/python3" SYMBOLIC)
		file(CREATE_LINK "${recipe_make}" "${tools}/make" SYMBOLIC)
		file(WRITE "${root}/make/source/configure.py" [=[
from pathlib import Path
import sys
if '--build-tool=make' not in sys.argv:
    raise RuntimeError('Botan must use make when Ninja is unavailable')
Path('Makefile').write_text('all:\n\t@printf built > built\ninstall: all\n\t@printf installed > installed\n')
]=])
		# Limit tool discovery to this fixture, excluding Ninja even if installed.
		file(WRITE "${root}/make/settings.cmake"
			"set(kind botan)\nset(mode STATIC)\nset(config Release)\nset(prefix [==[${root}/make/installed]==])\nset(CMAKE_SYSTEM_NAME ${CMAKE_HOST_SYSTEM_NAME})\nset(CMAKE_SYSTEM_PROCESSOR x86_64)\nset(CMAKE_SIZEOF_VOID_P 8)\nset(CMAKE_PROGRAM_PATH [==[${tools}]==])\nset(CMAKE_IGNORE_PATH [==[${cmake_bin};${cmake_bin}/../../Ninja]==])\nset(CMAKE_FIND_USE_CMAKE_SYSTEM_PATH FALSE)\nset(CMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH FALSE)\n")
		foreach(step configure build install)
			recipe_run("botan-make-${step}" "${CMAKE_COMMAND}" "-DSETTINGS=${root}/make/settings.cmake"
				"-DSOURCE=${root}/make/source" "-DWORK=${root}/make/work" "-DSTEP=${step}"
				-P "${RECIPE_MODULE_DIR}/Build.cmake")
		endforeach()
		foreach(marker built installed)
			file(READ "${root}/make/work/${marker}" value)
			if(NOT value STREQUAL marker)
				message(FATAL_ERROR "Botan make ${marker} did not run")
			endif()
		endforeach()
	elseif(NOT CMAKE_HOST_WIN32 AND (NOT TEST_RECIPE_CASE OR TEST_RECIPE_CASE STREQUAL "botan_make"))
		message(STATUS "Skipping Botan make fixture: Python is not available")
	endif()
	if(NOT TEST_RECIPE_CASE OR TEST_RECIPE_CASE STREQUAL "botan_patch")
		set(source "${root}/patch")
		file(MAKE_DIRECTORY "${source}/src/lib/x509")
		set(path "${source}/src/lib/x509/ocsp.cpp")
		file(WRITE "${path}" "#include <botan/ocsp.h>\n// OCSP implementation\n")
		recipe_run(botan-patch "${CMAKE_COMMAND}" -DNAME=BOTAN "-DSOURCE=${source}"
			-P "${RECIPE_MODULE_DIR}/Patch.cmake")
		file(READ "${path}" patched)
		if(NOT patched STREQUAL "#include <botan/ocsp.h>\n#include <algorithm>\n// OCSP implementation\n")
			message(FATAL_ERROR "Botan OCSP must include its algorithm declaration")
		endif()
		recipe_run(botan-patch-repeat "${CMAKE_COMMAND}" -DNAME=BOTAN "-DSOURCE=${source}"
			-P "${RECIPE_MODULE_DIR}/Patch.cmake")
		file(READ "${path}" repeated)
		if(NOT repeated STREQUAL patched)
			message(FATAL_ERROR "Botan OCSP patch is not idempotent")
		endif()
	endif()
endfunction()

test_dependency_recipes()

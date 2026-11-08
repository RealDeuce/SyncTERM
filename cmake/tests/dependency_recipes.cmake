# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)

function(recipe_run label)
	execute_process(COMMAND ${ARGN} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
	if(NOT result STREQUAL "0")
		message(FATAL_ERROR "${label} failed (${result}):\n${output}\n${errors}")
	endif()
endfunction()

function(test_dependency_recipes)
	if(TEST_RECIPE_CASE AND NOT TEST_RECIPE_CASE MATCHES "^(environment|cmake_install|cmake_paths|mpg_paths|versioned_shared|botan_make|botan_patch)$")
		message(FATAL_ERROR "Unknown dependency recipe test case: ${TEST_RECIPE_CASE}")
	endif()
	if(NOT RECIPE_MODULE_DIR)
		set(RECIPE_MODULE_DIR "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../dependencies")
	endif()
	set(root "${TEST_ROOT}/recipe-fixtures")
	file(MAKE_DIRECTORY "${root}")
	if(NOT TEST_RECIPE_CASE OR TEST_RECIPE_CASE MATCHES "^(environment|cmake_install|cmake_paths|mpg_paths)$")
		file(MAKE_DIRECTORY "${root}/generator")
		file(WRITE "${root}/generator/CMakeLists.txt"
			"cmake_minimum_required(VERSION 3.22)\nproject(Generator NONE)\n")
		recipe_run(generator "${CMAKE_COMMAND}" -S "${root}/generator" -B "${root}/generator/build")
		file(STRINGS "${root}/generator/build/CMakeCache.txt" generator_line REGEX "^CMAKE_GENERATOR:INTERNAL=")
		string(REPLACE "CMAKE_GENERATOR:INTERNAL=" "" generator "${generator_line}")
	endif()
	if(NOT TEST_RECIPE_CASE OR TEST_RECIPE_CASE STREQUAL "environment")
		# Simulate Windows' path separator while using the native host generator.
		# The child must receive one literal environment value, including spaces.
		file(MAKE_DIRECTORY "${root}/environment/source")
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
	if(NOT TEST_RECIPE_CASE OR TEST_RECIPE_CASE STREQUAL "cmake_paths")
		set(source "${root}/paths/source")
		set(prerequisite "${root}/paths/prerequisite")
		file(MAKE_DIRECTORY "${source}" "${prerequisite}/include" "${prerequisite}/syncterm")
		set(suffix .a)
		if(CMAKE_HOST_WIN32)
			set(suffix .lib)
		endif()
		file(WRITE "${prerequisite}/syncterm/libst_test_dependency${suffix}" "normalized library fixture\n")
		file(WRITE "${source}/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.22)
project(DependencyPaths NONE)
find_library(prerequisite NAMES st_test_dependency REQUIRED)
]=])
		file(WRITE "${root}/paths/settings.cmake"
			"set(kind cmake)\nset(name FIXTURE)\nset(config Debug)\nset(mode STATIC)\nset(CMAKE_GENERATOR [==[${generator}]==])\nset(prefix [==[${root}/paths/installed]==])\nset(dep_prefixes [==[${prerequisite}]==])\n")
		recipe_run(normalized-paths "${CMAKE_COMMAND}" "-DSETTINGS=${root}/paths/settings.cmake"
			"-DSOURCE=${source}" "-DWORK=${root}/paths/work" -DSTEP=configure
			-P "${RECIPE_MODULE_DIR}/Build.cmake")
	endif()
	if(NOT TEST_RECIPE_CASE OR TEST_RECIPE_CASE STREQUAL "mpg_paths")
		set(source "${root}/mpg/source")
		file(MAKE_DIRECTORY "${source}/ports/cmake")
		file(WRITE "${source}/ports/cmake/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.22)
project(ArchivePaths NONE)
if(NOT DEFINED CMAKE_OBJECT_PATH_MAX)
  message(FATAL_ERROR "mpg123 needs room for relative archive member paths")
endif()
# ar receives libmpg123/../compat/... rather than the canonical compat/...
string(LENGTH "libmpg123/../" extra)
math(EXPR native_length "${CMAKE_OBJECT_PATH_MAX} + ${extra} + 1")
if(native_length GREATER 260)
  message(FATAL_ERROR "Archive member path can exceed the Windows path limit")
endif()
]=])
		file(WRITE "${root}/mpg/settings.cmake"
			"set(kind cmake)\nset(name MPG123)\nset(config Debug)\nset(mode STATIC)\nset(CMAKE_HOST_WIN32 TRUE)\nset(CMAKE_GENERATOR [==[${generator}]==])\nset(prefix [==[${root}/mpg/installed]==])\n")
		recipe_run(archive-paths "${CMAKE_COMMAND}" "-DSETTINGS=${root}/mpg/settings.cmake"
			"-DSOURCE=${source}" "-DWORK=${root}/mpg/work" -DSTEP=configure
			-P "${RECIPE_MODULE_DIR}/Build.cmake")
	endif()
	if(NOT TEST_RECIPE_CASE OR TEST_RECIPE_CASE STREQUAL "versioned_shared")
		set(source "${root}/versioned/source")
		set(prefix "${root}/versioned/prefix")
		file(MAKE_DIRECTORY "${source}/include" "${prefix}/lib" "${prefix}/syncterm")
		file(WRITE "${prefix}/lib/libst_test_versioned.so.4.2" "versioned shared library fixture\n")
		file(WRITE "${prefix}/lib/libst_test_versioned.a" "static library must not satisfy SHARED\n")
		if(NOT CMAKE_HOST_WIN32)
			file(CREATE_LINK libst_test_versioned.so.4.2 "${prefix}/lib/libst_test_versioned.so.4" SYMBOLIC)
		endif()
		file(WRITE "${root}/versioned/settings.cmake"
			"set(kind headers)\nset(name FIXTURE)\nset(mode SHARED)\nset(WIN32 FALSE)\nset(APPLE FALSE)\nset(prefix [==[${prefix}]==])\nset(libraries st_test_versioned)\n")
		recipe_run(versioned-shared "${CMAKE_COMMAND}" "-DSETTINGS=${root}/versioned/settings.cmake"
			"-DSOURCE=${source}" "-DWORK=${root}/versioned/work" -DSTEP=install
			-P "${RECIPE_MODULE_DIR}/Build.cmake")
		foreach(relative libst_test_versioned.so libst_test_versioned.so.4.2)
			file(READ "${prefix}/syncterm/${relative}" installed)
			if(NOT installed STREQUAL "versioned shared library fixture\n")
				message(FATAL_ERROR "Versioned shared library contents were not preserved: ${relative}")
			endif()
		endforeach()
		file(REMOVE "${prefix}/lib/libst_test_versioned.so.4.2" "${prefix}/lib/libst_test_versioned.so.4")
		execute_process(COMMAND "${CMAKE_COMMAND}" "-DSETTINGS=${root}/versioned/settings.cmake"
			"-DSOURCE=${source}" "-DWORK=${root}/versioned/work" -DSTEP=install
			-P "${RECIPE_MODULE_DIR}/Build.cmake" RESULT_VARIABLE missing
			OUTPUT_VARIABLE output ERROR_VARIABLE errors)
		if(missing STREQUAL "0" OR NOT errors MATCHES "required SHARED library")
			message(FATAL_ERROR "A static archive must not satisfy a missing SHARED dependency: ${output}${errors}")
		endif()
	endif()
	# The generic source fixtures also run on hosts without optional Python.
	find_program(recipe_python NAMES python3 python)
	if(recipe_python AND (NOT TEST_RECIPE_CASE OR TEST_RECIPE_CASE STREQUAL "cmake_install"))
		set(source "${root}/install/source")
		set(prefix "${root}/install/prefix")
		file(MAKE_DIRECTORY "${source}")
		file(WRITE "${source}/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.22)
project(InstallContents NONE)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/payload" DESTINATION share)
]=])
		file(WRITE "${source}/payload" "first\n")
		file(WRITE "${root}/install/settings.cmake"
			"set(kind cmake)\nset(name FIXTURE)\nset(config Debug)\nset(mode STATIC)\nset(CMAKE_GENERATOR [==[${generator}]==])\nset(prefix [==[${prefix}]==])\n")
		set(arguments "${CMAKE_COMMAND}" "-DSETTINGS=${root}/install/settings.cmake"
			"-DSOURCE=${source}" "-DWORK=${root}/install/work")
		set(saved_install_always "$ENV{CMAKE_INSTALL_ALWAYS}")
		set(ENV{CMAKE_INSTALL_ALWAYS} 0)
		recipe_run(install-configure ${arguments} -DSTEP=configure -P "${RECIPE_MODULE_DIR}/Build.cmake")
		recipe_run(install-first ${arguments} -DSTEP=install -P "${RECIPE_MODULE_DIR}/Build.cmake")
		file(WRITE "${source}/payload" "other\n")
		# Equal size and exact timestamps must not conceal changed contents.
		file(WRITE "${root}/install/equal-times.py" [=[
import os
import sys
for path in sys.argv[1:]:
    os.utime(path, ns=(1600000000000000000, 1600000000000000000))
]=])
		recipe_run(install-times "${recipe_python}" "${root}/install/equal-times.py"
			"${source}/payload" "${prefix}/share/payload")
		recipe_run(install-changed ${arguments} -DSTEP=install -P "${RECIPE_MODULE_DIR}/Build.cmake")
		set(ENV{CMAKE_INSTALL_ALWAYS} "${saved_install_always}")
		file(READ "${prefix}/share/payload" installed)
		if(NOT installed STREQUAL "other\n")
			message(FATAL_ERROR "Dependency installation kept stale contents with identical timestamps")
		endif()
	elseif(NOT TEST_RECIPE_CASE OR TEST_RECIPE_CASE STREQUAL "cmake_install")
		message(STATUS "Skipping timestamp collision fixture: Python is not available")
	endif()
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
if Path(sys.argv[0]).is_absolute() or '--with-build-dir=.' not in sys.argv:
    raise RuntimeError('Botan must generate relative build paths')
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
		file(WRITE "${source}/configure.py" [=[
def template_vars(options, osinfo):
    return {
        'build_static_lib_using_cmdline_args': options.build_static_lib and osinfo.basename != 'windows',
        'build_static_lib_using_response_file': options.build_static_lib and osinfo.basename == 'windows',
    }
]=])
		file(MAKE_DIRECTORY "${source}/src/build-data")
		file(WRITE "${source}/src/build-data/ninja.in"
			"  command = %{ar_command} %{ar_options} %{ar_output_to}$out @%{response_file_dir}/static.txt\n")
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
		if(recipe_python)
			file(WRITE "${source}/check-templates.py" [=[
from pathlib import Path
import runpy
from types import SimpleNamespace
vars_for = runpy.run_path(str(Path(__file__).with_name('configure.py')))['template_vars']
for name, uses_response in [('windows', True), ('mingw', True), ('linux', False)]:
    for static in (False, True):
        result = vars_for(SimpleNamespace(build_static_lib=static), SimpleNamespace(basename=name))
        if result['build_static_lib_using_response_file'] != (static and uses_response):
            raise RuntimeError(f'{name} selected the wrong archive response-file policy')
        if result['build_static_lib_using_cmdline_args'] != (static and not uses_response):
            raise RuntimeError(f'{name} selected the wrong archive argument policy')
template = Path(__file__).parent / 'src/build-data/ninja.in'
if '"@%{response_file_dir}/static.txt"' not in template.read_text():
    raise RuntimeError('Archive response-file paths must be quoted for spaces')
]=])
			recipe_run(botan-response-files "${recipe_python}" "${source}/check-templates.py")
		endif()
	endif()
endfunction()

test_dependency_recipes()

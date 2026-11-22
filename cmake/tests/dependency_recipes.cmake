# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)

function(recipe_run label)
	execute_process(COMMAND ${ARGN} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
	if(NOT result STREQUAL "0")
		message(FATAL_ERROR "${label} failed (${result}):\n${output}\n${errors}")
	endif()
endfunction()

function(test_dependency_recipes)
	if(TEST_RECIPE_CASE AND NOT TEST_RECIPE_CASE MATCHES "^(environment|cmake_install|cmake_paths|mpg_paths|architecture_install|versioned_shared|botan_make|botan_patch|vorbis_patch|sndfile_patch|policy_options|pipewire_options)$")
		message(FATAL_ERROR "Unknown dependency recipe test case: ${TEST_RECIPE_CASE}")
	endif()
	if(NOT RECIPE_MODULE_DIR)
		set(RECIPE_MODULE_DIR "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../dependencies")
	endif()
	set(root "${TEST_ROOT}/recipe-fixtures")
	file(MAKE_DIRECTORY "${root}")
	include("${RECIPE_MODULE_DIR}/Recipes.cmake")
	if(NOT TEST_RECIPE_CASE OR TEST_RECIPE_CASE STREQUAL "policy_options")
		set(native_cmake_version "${CMAKE_VERSION}")
		foreach(version 3.22 3.31 4.0)
			set(CMAKE_VERSION "${version}")
			foreach(name VORBIS SNDFILE)
				syncterm_dependency_recipe(${name})
				set(floor 3.5)
				if(name STREQUAL "SNDFILE")
					set(floor 3.18)
				endif()
				if(version VERSION_GREATER_EQUAL 4.0)
					if(NOT "-DCMAKE_POLICY_VERSION_MINIMUM=${floor}" IN_LIST recipe_options)
						message(FATAL_ERROR "${name} must use CMake 4's external policy floor")
					endif()
				elseif("-DCMAKE_POLICY_VERSION_MINIMUM=${floor}" IN_LIST recipe_options)
					message(FATAL_ERROR "${name} passes an unsupported policy setting to CMake ${version}")
				endif()
			endforeach()
		endforeach()
		set(CMAKE_VERSION "${native_cmake_version}")
		# Exercise the no-edit selection even on hosts with CMake 3. Native
		# CMake 4 additionally exercises its real policy behavior below.
		set(source "${root}/policy-floor/source")
		file(MAKE_DIRECTORY "${source}")
		file(WRITE "${source}/CMakeLists.txt" "# Policy fixture must remain untouched.\n")
		file(WRITE "${root}/policy-floor/patch.cmake"
			"set(CMAKE_VERSION 4.0)\ninclude([==[${RECIPE_MODULE_DIR}/Patch.cmake]==])\n")
		foreach(name VORBIS SNDFILE)
			recipe_run("${name}-no-patch" "${CMAKE_COMMAND}" "-DNAME=${name}" "-DSOURCE=${source}"
				-P "${root}/policy-floor/patch.cmake")
		endforeach()
		file(READ "${source}/CMakeLists.txt" unchanged)
		if(NOT unchanged STREQUAL "# Policy fixture must remain untouched.\n")
			message(FATAL_ERROR "External policy floors must not edit dependency sources")
		endif()
	endif()
	if(NOT TEST_RECIPE_CASE OR TEST_RECIPE_CASE STREQUAL "pipewire_options")
		syncterm_dependency_recipe(PIPEWIRE)
		foreach(plugin audiomixer control audiotestsrc videoconvert videotestsrc)
			if(NOT "-D${plugin}=disabled" IN_LIST recipe_options)
				message(FATAL_ERROR "Unused PipeWire ${plugin} must be disabled through its upstream option")
			endif()
		endforeach()
		set(source "${root}/pipewire/source")
		file(MAKE_DIRECTORY "${source}/src/modules" "${source}/src/pipewire"
			"${source}/src/daemon" "${source}/spa/plugins")
		file(WRITE "${source}/src/modules/network-utils.h" "#include <netinet/in.h>\n")
		file(WRITE "${source}/src/pipewire/thread.c" "#ifdef SCHED_RESET_ON_FORK\n#endif\n")
		file(WRITE "${source}/src/pipewire/context.c" "/* SyncTERM: available resource limits */\n")
		set(modules "")
		foreach(module rt protocol_native client_node metadata adapter)
			string(APPEND modules "pipewire_module_${module} = shared_library(\n)\n")
		endforeach()
		file(WRITE "${source}/src/modules/meson.build" "${modules}")
		file(WRITE "${source}/spa/meson.build" "subdir('tools')\n")
		set(plugins [=[# Preserve upstream's plugin declarations and conditions.
if get_option('control').allowed()
  subdir('control')
endif
subdir('support')
subdir('audioconvert')
subdir('aec')
subdir('filter-graph')
]=])
		file(WRITE "${source}/spa/plugins/meson.build" "${plugins}")
		recipe_run(pipewire-options "${CMAKE_COMMAND}" -DNAME=PIPEWIRE "-DSOURCE=${source}"
			-DHAVE_EBADFD=TRUE -DHAVE_ECHRNG=TRUE -P "${RECIPE_MODULE_DIR}/Patch.cmake")
		file(READ "${source}/spa/plugins/meson.build" patched)
		foreach(required "# Preserve upstream's plugin declarations and conditions."
			"if get_option('control').allowed()" "subdir('control')"
			"subdir('support')" "subdir('audioconvert')")
			string(FIND "${patched}" "${required}" found)
			if(found EQUAL -1)
				message(FATAL_ERROR "PipeWire patch removed an upstream declaration with a supported option: ${required}")
			endif()
		endforeach()
		if(patched MATCHES "subdir\\('(aec|filter-graph)'\\)")
			message(FATAL_ERROR "PipeWire plugins without upstream selectors must retain their existing exclusion")
		endif()
	endif()
	if(NOT TEST_RECIPE_CASE OR TEST_RECIPE_CASE STREQUAL "vorbis_patch")
		set(source "${root}/vorbis/source")
		file(MAKE_DIRECTORY "${source}")
		file(WRITE "${source}/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 2.8.12)
project(VorbisPolicy NONE)
# CMake 4 rejects policy versions below 3.5. Check an actual policy so
# this regression is also detectable with older supported CMake releases.
cmake_policy(GET CMP0054 quoted_conditions)
if(NOT quoted_conditions STREQUAL "NEW")
  message(FATAL_ERROR "Vorbis must use policies supported by CMake 4")
endif()
]=])
		set(path "${source}/CMakeLists.txt")
		file(READ "${path}" original)
		recipe_run(vorbis-patch "${CMAKE_COMMAND}" -DNAME=VORBIS "-DSOURCE=${source}"
			-P "${RECIPE_MODULE_DIR}/Patch.cmake")
		syncterm_dependency_recipe(VORBIS)
		recipe_run(vorbis-configure "${CMAKE_COMMAND}" -S "${source}" -B "${root}/vorbis/work" ${recipe_options})
		file(READ "${path}" patched)
		recipe_run(vorbis-patch-repeat "${CMAKE_COMMAND}" -DNAME=VORBIS "-DSOURCE=${source}"
			-P "${RECIPE_MODULE_DIR}/Patch.cmake")
		file(READ "${path}" repeated)
		if(NOT repeated STREQUAL patched)
			message(FATAL_ERROR "Vorbis CMake policy patch is not idempotent")
		endif()
		if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.0)
			if(NOT patched STREQUAL original)
				message(FATAL_ERROR "CMake 4 must configure Vorbis without editing its sources")
			endif()
		else()
			file(WRITE "${path}" "cmake_minimum_required(VERSION 3.0)\n")
			execute_process(COMMAND "${CMAKE_COMMAND}" -DNAME=VORBIS "-DSOURCE=${source}"
				-P "${RECIPE_MODULE_DIR}/Patch.cmake" RESULT_VARIABLE result
				OUTPUT_VARIABLE output ERROR_VARIABLE errors)
			if(result STREQUAL "0" OR NOT errors MATCHES "Vorbis CMake policy patch no longer applies")
				message(FATAL_ERROR "Changed Vorbis sources must reject a stale policy patch: ${output}${errors}")
			endif()
		endif()
	endif()
	if(NOT TEST_RECIPE_CASE OR TEST_RECIPE_CASE STREQUAL "sndfile_patch")
		set(source "${root}/sndfile/source")
		file(MAKE_DIRECTORY "${source}")
		file(WRITE "${source}/CMakeLists.txt" [=[
cmake_minimum_required (VERSION 3.1..3.18)
project(SndFilePolicy NONE)
# The intended 3.18 policy range must take effect, including on CMake 3.x.
cmake_policy(GET CMP0077 option_variables)
if(NOT option_variables STREQUAL "NEW")
  message(FATAL_ERROR "libsndfile must use its intended CMake policy range")
endif()
]=])
		set(path "${source}/CMakeLists.txt")
		file(READ "${path}" original)
		recipe_run(sndfile-patch "${CMAKE_COMMAND}" -DNAME=SNDFILE "-DSOURCE=${source}"
			-P "${RECIPE_MODULE_DIR}/Patch.cmake")
		syncterm_dependency_recipe(SNDFILE)
		recipe_run(sndfile-configure "${CMAKE_COMMAND}" -S "${source}" -B "${root}/sndfile/work" ${recipe_options})
		file(READ "${path}" patched)
		recipe_run(sndfile-patch-repeat "${CMAKE_COMMAND}" -DNAME=SNDFILE "-DSOURCE=${source}"
			-P "${RECIPE_MODULE_DIR}/Patch.cmake")
		file(READ "${path}" repeated)
		if(NOT repeated STREQUAL patched)
			message(FATAL_ERROR "libsndfile CMake policy patch is not idempotent")
		endif()
		if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.0)
			if(NOT patched STREQUAL original)
				message(FATAL_ERROR "CMake 4 must configure libsndfile without editing its sources")
			endif()
		else()
			file(WRITE "${path}" "cmake_minimum_required (VERSION 3.1)\n")
			execute_process(COMMAND "${CMAKE_COMMAND}" -DNAME=SNDFILE "-DSOURCE=${source}"
				-P "${RECIPE_MODULE_DIR}/Patch.cmake" RESULT_VARIABLE result
				OUTPUT_VARIABLE output ERROR_VARIABLE errors)
			if(result STREQUAL "0" OR NOT errors MATCHES "libsndfile CMake policy patch no longer applies")
				message(FATAL_ERROR "Changed libsndfile sources must reject a stale policy patch: ${output}${errors}")
			endif()
		endif()
	endif()
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
	if(NOT TEST_RECIPE_CASE OR TEST_RECIPE_CASE STREQUAL "architecture_install")
		set(source "${root}/architecture/source")
		file(REMOVE_RECURSE "${root}/architecture")
		file(MAKE_DIRECTORY "${source}/include")
		# Exercise the slice install paths without requiring an Apple compiler.
		foreach(mode STATIC SHARED)
			set(prefix "${root}/architecture/${mode}/prefix")
			file(MAKE_DIRECTORY "${prefix}/syncterm")
			set(suffix .a)
			if(mode STREQUAL "SHARED")
				set(suffix .dylib)
			endif()
			file(WRITE "${root}/architecture/settings.cmake"
				"set(kind headers)\nset(name FIXTURE)\nset(mode ${mode})\nset(WIN32 FALSE)\nset(MSVC FALSE)\nset(APPLE TRUE)\nset(prefix [==[${prefix}]==])\nset(libraries st_test_slice)\n")
			foreach(slice arm64 x86_64)
				set(slice_prefix "${prefix}/slices/${slice}")
				file(MAKE_DIRECTORY "${slice_prefix}/lib")
				file(WRITE "${slice_prefix}/lib/libst_test_slice${suffix}" "${mode} ${slice}\n")
				recipe_run("${mode}-${slice}-install" "${CMAKE_COMMAND}"
					"-DSETTINGS=${root}/architecture/settings.cmake" "-DARCH=${slice}"
					"-DSOURCE=${source}" "-DWORK=${root}/architecture/${mode}/${slice}" -DSTEP=install
					-P "${RECIPE_MODULE_DIR}/Build.cmake")
				file(READ "${slice_prefix}/syncterm/libst_test_slice${suffix}" installed)
				if(NOT installed STREQUAL "${mode} ${slice}\n")
					message(FATAL_ERROR "${mode} ${slice} library contents were not preserved")
				endif()
			endforeach()
		endforeach()
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

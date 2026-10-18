# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

# One driver for POSIX and Windows: native-command failures must not be hidden
# by shell pipelines or PowerShell's handling of nonzero native exit codes.
get_filename_component(_project "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
foreach(_setting IN ITEMS SOURCE_DIR BUILD_DIR PROFILE BUILD_TYPE)
	if(NOT DEFINED CI_${_setting} AND NOT "$ENV{CI_${_setting}}" STREQUAL "")
		set(CI_${_setting} "$ENV{CI_${_setting}}")
	endif()
endforeach()
if(NOT DEFINED CI_SOURCE_DIR)
	set(CI_SOURCE_DIR "${_project}")
endif()
if(NOT DEFINED CI_BUILD_DIR)
	set(CI_BUILD_DIR "${_project}/build-ci")
endif()
if(NOT DEFINED CI_PROFILE)
	set(CI_PROFILE native)
endif()
if(NOT DEFINED CI_BUILD_TYPE)
	set(CI_BUILD_TYPE RelWithDebInfo)
endif()
get_filename_component(CI_SOURCE_DIR "${CI_SOURCE_DIR}" ABSOLUTE)
get_filename_component(CI_BUILD_DIR "${CI_BUILD_DIR}" ABSOLUTE)
if(CI_SOURCE_DIR STREQUAL CI_BUILD_DIR)
	message(FATAL_ERROR "CI requires an out-of-source build")
endif()
set(_reports "${CI_BUILD_DIR}/reports")
file(MAKE_DIRECTORY "${_reports}")
# Do not publish results or binaries left by an earlier attempt in this tree.
file(REMOVE "${_reports}/junit.xml" "${_reports}/execution.txt"
	"${_reports}/capabilities.txt" "${_reports}/build.log" "${_reports}/test.log"
	"${_reports}/CMakeConfigureLog.yaml" "${_reports}/CMakeError.log"
	"${_reports}/CMakeOutput.log")
file(GLOB _old_sanitizer_reports "${_reports}/asan.*" "${_reports}/ubsan.*"
	"${_reports}/loader.*")
if(_old_sanitizer_reports)
	file(REMOVE ${_old_sanitizer_reports})
endif()
file(GLOB _old_apps "${CI_BUILD_DIR}/artifacts/syncterm*")
if(_old_apps)
	file(REMOVE ${_old_apps})
endif()
file(WRITE "${_reports}/environment.txt"
	"Host: ${CMAKE_HOST_SYSTEM_NAME} ${CMAKE_HOST_SYSTEM_VERSION}\n"
	"CMake: ${CMAKE_VERSION}\nProfile: ${CI_PROFILE}\n"
	"CC: $ENV{CC}\nCXX: $ENV{CXX}\nDISPLAY: $ENV{DISPLAY}\n"
	"WAYLAND_DISPLAY: $ENV{WAYLAND_DISPLAY}\n")
file(APPEND "${_reports}/environment.txt"
	"TERM: $ENV{TERM}\nTERMINFO: $ENV{TERMINFO}\n"
	"XDG_RUNTIME_DIR: $ENV{XDG_RUNTIME_DIR}\n")

function(ci_command name)
	message(STATUS "CI ${name}")
	if(CMAKE_HOST_WIN32)
		execute_process(COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
			-File "${_project}/ci/log.ps1" "${_reports}/${name}.log" ${ARGN}
			WORKING_DIRECTORY "${CI_SOURCE_DIR}" RESULT_VARIABLE _results)
	else()
		find_program(_sh sh REQUIRED)
		find_program(_tee tee REQUIRED)
		# Arguments are positional, never interpolated into shell code. Both
		# pipeline statuses matter: tee must not hide compiler/test failures.
		execute_process(COMMAND "${_sh}" -c [=[exec "$@" 2>&1]=] ci-log ${ARGN}
			COMMAND "${_tee}" "${_reports}/${name}.log"
			WORKING_DIRECTORY "${CI_SOURCE_DIR}" RESULTS_VARIABLE _results)
	endif()
	foreach(_result IN LISTS _results)
		if(NOT "${_result}" STREQUAL "0")
			if(name STREQUAL configure)
				# CMake moved probe diagnostics to YAML in 3.26. Keep either
				# format even when configure fails before capabilities exist.
				foreach(_log IN ITEMS CMakeConfigureLog.yaml CMakeError.log CMakeOutput.log)
					if(EXISTS "${CI_BUILD_DIR}/CMakeFiles/${_log}")
						file(COPY "${CI_BUILD_DIR}/CMakeFiles/${_log}" DESTINATION "${_reports}")
					endif()
				endforeach()
			endif()
			message(FATAL_ERROR "CI ${name} failed (${_result})")
		endif()
	endforeach()
endfunction()

set(_options -DBUILD_TESTING=ON -DST_CTERM_TEST_EXTERNAL_ASSETS=OFF)
set(_cross FALSE)
if(CI_PROFILE STREQUAL minimal)
	list(APPEND _options -DCRYPTO_BACKEND=none -DSYNCTERM_ENABLE_UNIFONT=OFF
		-DST_CONSOLE_X11=OFF -DST_CONSOLE_WAYLAND=OFF -DST_CONSOLE_SDL=OFF
		-DST_AUDIO_FILE_SNDFILE=OFF -DST_PIXEL_IMAGE_JXL=OFF)
	foreach(_backend IN ITEMS ALSA OSS PORTAUDIO PULSE_AUDIO PIPEWIRE SDL CORE_AUDIO WASAPI)
		list(APPEND _options "-DST_AUDIO_${_backend}=OFF")
	endforeach()
elseif(CI_PROFILE STREQUAL sanitizers)
	set(CI_BUILD_TYPE Debug)
	list(APPEND _options -C "${CMAKE_CURRENT_LIST_DIR}/sanitizers.cmake")
elseif(CI_PROFILE MATCHES "^mingw(32|64)-cross$")
	set(_cross TRUE)
	set(CI_MINGW_BITS "${CMAKE_MATCH_1}")
	list(APPEND _options "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_CURRENT_LIST_DIR}/mingw.cmake"
		"-DCI_MINGW_BITS=${CI_MINGW_BITS}" -DCRYPTO_BACKEND=none)
elseif(NOT CI_PROFILE STREQUAL native)
	message(FATAL_ERROR "Unknown CI_PROFILE: ${CI_PROFILE}")
endif()
list(APPEND _options "-DCMAKE_BUILD_TYPE=${CI_BUILD_TYPE}")
# Absolute compiler filenames may contain spaces on Windows. Pass those as
# individual CMake arguments; leave command-with-flags CC/CXX values alone.
foreach(_language IN ITEMS C CXX)
	if(_language STREQUAL C)
		set(_environment_compiler "$ENV{CC}")
	else()
		set(_environment_compiler "$ENV{CXX}")
	endif()
	if(IS_ABSOLUTE "${_environment_compiler}" AND EXISTS "${_environment_compiler}")
		file(TO_CMAKE_PATH "${_environment_compiler}" _compiler)
		list(APPEND _options "-DCMAKE_${_language}_COMPILER=${_compiler}")
	endif()
endforeach()
# A CMake list, not shell text: paths containing spaces remain one argument.
list(APPEND _options $ENV{CI_EXTRA_CMAKE_ARGS})

ci_command(configure "${CMAKE_COMMAND}" -S "${CI_SOURCE_DIR}"
	-B "${CI_BUILD_DIR}" ${_options})
file(STRINGS "${CI_BUILD_DIR}/CMakeCache.txt" _testing REGEX "^BUILD_TESTING:[^=]+=")
string(REGEX REPLACE "^[^=]*=" "" _testing "${_testing}")
if(NOT _testing)
	message(FATAL_ERROR "CI requires BUILD_TESTING=ON")
endif()
file(STRINGS "${CI_BUILD_DIR}/CMakeCache.txt" _capabilities REGEX
	"^(ST_|XPTLS_|SYNCTERM_|CRYPTO_BACKEND:|CMAKE_(C|CXX|OBJC)_COMPILER[:_]|CMAKE_SYSTEM_)")
string(REPLACE ";" "\n" _capabilities "${_capabilities}")
file(WRITE "${_reports}/capabilities.txt" "${_capabilities}\n")
file(STRINGS "${_reports}/configure.log" _provider REGEX "xptls crypto provider:")
file(APPEND "${_reports}/capabilities.txt" "${_provider}\n")
file(GLOB _configs "${CI_BUILD_DIR}/*/*config.h")
foreach(_header IN LISTS _configs)
	file(STRINGS "${_header}" _defines REGEX "^#define (ST_|XPTLS_|SYNCTERM_)")
	string(REPLACE ";" "\n" _defines "${_defines}")
	get_filename_component(_name "${_header}" NAME)
	file(APPEND "${_reports}/capabilities.txt" "\n${_name}:\n${_defines}\n")
endforeach()
if("$ENV{CMAKE_BUILD_PARALLEL_LEVEL}" STREQUAL "")
	set(ENV{CMAKE_BUILD_PARALLEL_LEVEL} 4)
endif()
ci_command(build "${CMAKE_COMMAND}" --build "${CI_BUILD_DIR}"
	--config "${CI_BUILD_TYPE}" --parallel "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}")

set(_app "${CI_BUILD_DIR}/app/syncterm")
if(_cross OR CMAKE_HOST_WIN32)
	string(APPEND _app .exe)
	if(EXISTS "${CI_BUILD_DIR}/app/${CI_BUILD_TYPE}/syncterm.exe")
		set(_app "${CI_BUILD_DIR}/app/${CI_BUILD_TYPE}/syncterm.exe")
	endif()
endif()
if(NOT EXISTS "${_app}")
	message(FATAL_ERROR "Application was not built: ${_app}")
endif()
file(MAKE_DIRECTORY "${CI_BUILD_DIR}/artifacts")
file(COPY "${_app}" DESTINATION "${CI_BUILD_DIR}/artifacts")

if(_cross)
	file(WRITE "${_reports}/execution.txt"
		"Cross-compilation only: application and test targets built; no target tests executed.\n")
	message(STATUS "Cross-build complete; target tests were NOT executed")
else()
	file(WRITE "${_reports}/execution.txt" "Native test execution\n")
	get_filename_component(_cmake_bin "${CMAKE_COMMAND}" DIRECTORY)
	find_program(_ctest NAMES ctest HINTS "${_cmake_bin}" REQUIRED)
	if("$ENV{CTEST_PARALLEL_LEVEL}" STREQUAL "")
		set(ENV{CTEST_PARALLEL_LEVEL} 1)
	endif()
	if(CI_PROFILE STREQUAL sanitizers AND CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
		# Record module bases for allocation frames in runtime-loaded libraries,
		# including tentative loads rejected after symbol resolution fails.
		# This optional diagnostic does not alter library lifetime.
		set(ENV{LD_DEBUG} files)
		set(ENV{LD_DEBUG_OUTPUT} "${_reports}/loader")
	endif()
	ci_command(test "${_ctest}" --test-dir "${CI_BUILD_DIR}"
		-C "${CI_BUILD_TYPE}" --parallel "$ENV{CTEST_PARALLEL_LEVEL}"
		--output-on-failure --no-tests=error --output-junit "${_reports}/junit.xml")
endif()

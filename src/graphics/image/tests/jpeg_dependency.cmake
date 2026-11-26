# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)
include("${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/dependencies/Recipes.cmake")
syncterm_dependency_recipe(JPEG)
set(source "${TEST_ROOT}/source")
file(MAKE_DIRECTORY "${source}")
file(WRITE "${source}/README.ijg" "IJG fixture notice\n")
file(WRITE "${source}/LICENSE.md" "JPEG fixture license\n")
# libjpeg-turbo defaults both ENABLE_STATIC and ENABLE_SHARED to ON and does
# not select them from BUILD_SHARED_LIBS. Exercise our source-build adapter.
file(WRITE "${source}/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.22)
project(JpegBuildPolicy NONE)
option(ENABLE_SHARED "Shared" ON)
option(ENABLE_STATIC "Static" ON)
if(BUILD_SHARED_LIBS)
  if(NOT ENABLE_SHARED OR ENABLE_STATIC)
    message(FATAL_ERROR "Shared JPEG policy also builds static libraries")
  endif()
else()
  if(ENABLE_SHARED OR NOT ENABLE_STATIC)
    message(FATAL_ERROR "Static JPEG policy also builds shared libraries")
  endif()
endif()
if(WITH_TURBOJPEG OR WITH_TOOLS OR WITH_TESTS OR WITH_JAVA)
  message(FATAL_ERROR "JPEG recipe enables unrelated consumers")
endif()
]=])
foreach(mode STATIC SHARED)
	set(settings "${TEST_ROOT}/${mode}.cmake")
	file(WRITE "${settings}"
		"set(name JPEG)\nset(kind cmake)\nset(mode ${mode})\nset(config Release)\nset(CMAKE_GENERATOR [==[${CMAKE_GENERATOR}]==])\nset(prefix [==[${TEST_ROOT}/prefix]==])\nset(options [==[${recipe_options}]==])\n")
	execute_process(COMMAND "${CMAKE_COMMAND}" "-DSETTINGS=${settings}"
		"-DSOURCE=${source}" "-DWORK=${TEST_ROOT}/${mode}" -DSTEP=configure
		-P "${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/dependencies/Build.cmake"
		RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
	if(NOT result STREQUAL "0")
		message(FATAL_ERROR "JPEG ${mode} recipe failed: ${output}${errors}")
	endif()
	execute_process(COMMAND "${CMAKE_COMMAND}" "-DSETTINGS=${settings}"
		"-DSOURCE=${source}" "-DWORK=${TEST_ROOT}/${mode}" -DSTEP=install
		-P "${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/dependencies/Build.cmake"
		RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
	if(NOT result STREQUAL "0")
		message(FATAL_ERROR "JPEG ${mode} install failed: ${output}${errors}")
	endif()
	file(READ "${TEST_ROOT}/prefix/share/licenses/JPEG/README.ijg" notice)
	file(READ "${TEST_ROOT}/prefix/share/licenses/JPEG/LICENSE.md" license)
	if(NOT notice STREQUAL "IJG fixture notice\n" OR NOT license STREQUAL "JPEG fixture license\n")
		message(FATAL_ERROR "JPEG installation must retain both upstream notices")
	endif()
endforeach()

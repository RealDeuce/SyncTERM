# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermAtomics.cmake")

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(STAudioMixer LANGUAGES C)
	include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCStandard.cmake")
include(CTest)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCheckC17Source.cmake")

if(NOT TARGET syncterm_threads)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../base/threads"
		"${CMAKE_CURRENT_BINARY_DIR}/threads")
endif()
if(NOT TARGET syncterm_io_error)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../base/io_error"
		"${CMAKE_CURRENT_BINARY_DIR}/io_error")
endif()

add_library(st_audio_mixer STATIC audio_mixer.c)
target_include_directories(st_audio_mixer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_audio_mixer PUBLIC syncterm_threads syncterm_io_error)

syncterm_check_c17_source([=[
#include <math.h>
int main(void)
{
	volatile float f = 0.5f;
	volatile double d = 0.5;
	return (int)(isfinite(f) + sin(d) + tanh(d) + powf(f, f)
	    + log10f(f));
}
]=] ST_AUDIO_MIXER_MATH_IN_C_LIBRARY)
if(NOT ST_AUDIO_MIXER_MATH_IN_C_LIBRARY)
	set(_st_audio_mixer_saved_required_libraries ${CMAKE_REQUIRED_LIBRARIES})
	set(CMAKE_REQUIRED_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES} m)
	syncterm_check_c17_source([=[
#include <math.h>
int main(void)
{
	volatile float f = 0.5f;
	volatile double d = 0.5;
	return (int)(isfinite(f) + sin(d) + tanh(d) + powf(f, f)
	    + log10f(f));
}
]=] ST_AUDIO_MIXER_MATH_IN_LIBM)
	set(CMAKE_REQUIRED_LIBRARIES ${_st_audio_mixer_saved_required_libraries})
	if(NOT ST_AUDIO_MIXER_MATH_IN_LIBM)
		message(FATAL_ERROR "The target does not provide the required math functions")
	endif()
	target_link_libraries(st_audio_mixer PRIVATE m)
endif()
set_target_properties(st_audio_mixer PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)

function(st_audio_mixer_warnings target)
	if(MSVC)
		target_compile_options(${target} PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(${target} PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
endfunction()
st_audio_mixer_warnings(st_audio_mixer)

if(BUILD_TESTING)
	add_executable(st_audio_mixer_test tests/audio_mixer_test.c)
	target_link_libraries(st_audio_mixer_test PRIVATE st_audio_mixer)
	syncterm_target_atomics(st_audio_mixer_test)
	set_target_properties(st_audio_mixer_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	st_audio_mixer_warnings(st_audio_mixer_test)
	add_test(NAME st_audio_mixer COMMAND st_audio_mixer_test)
	set_tests_properties(st_audio_mixer PROPERTIES TIMEOUT 20)
endif()

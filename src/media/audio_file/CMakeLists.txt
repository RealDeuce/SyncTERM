# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(STAudioFile LANGUAGES C)
	include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCStandard.cmake")
	include(CTest)
endif()
foreach(_st_component_path IN ITEMS io/filesystem base/dynamic_library base/threads)
	get_filename_component(component "${_st_component_path}" NAME)
	if(NOT TARGET syncterm_${component})
		add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../${_st_component_path}"
			"${CMAKE_CURRENT_BINARY_DIR}/${component}")
	endif()
endforeach()
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCheckC17Source.cmake")
set(ST_AUDIO_FILE_SNDFILE AUTO CACHE STRING "Runtime libsndfile decoder: AUTO, ON, or OFF")
set_property(CACHE ST_AUDIO_FILE_SNDFILE PROPERTY STRINGS AUTO ON OFF)
string(TOUPPER "${ST_AUDIO_FILE_SNDFILE}" _sndfile_policy)
if(NOT _sndfile_policy MATCHES "^(AUTO|ON|OFF)$")
	message(FATAL_ERROR "ST_AUDIO_FILE_SNDFILE must be AUTO, ON, or OFF")
endif()
set(ST_AUDIO_FILE_HAVE_SNDFILE 0)
if(NOT _sndfile_policy STREQUAL "OFF")
	find_path(ST_AUDIO_FILE_SNDFILE_INCLUDE_DIR sndfile.h)
	if(ST_AUDIO_FILE_SNDFILE_INCLUDE_DIR)
		set(_saved_includes "${CMAKE_REQUIRED_INCLUDES}")
		set(_saved_type "${CMAKE_TRY_COMPILE_TARGET_TYPE}")
		set(CMAKE_REQUIRED_INCLUDES "${ST_AUDIO_FILE_SNDFILE_INCLUDE_DIR}")
		set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
		syncterm_check_c17_source([=[
#include <stdint.h>
#include <limits.h>
#include <sndfile.h>
_Static_assert(sizeof(sf_count_t) == sizeof(int64_t), "64-bit audio offsets required");
_Static_assert((sf_count_t)-1 < 0, "signed audio offsets required");
_Static_assert(sizeof(short) == sizeof(int16_t) && SHRT_MIN == INT16_MIN
    && SHRT_MAX == INT16_MAX, "16-bit signed audio samples required");
_Static_assert(SF_FORMAT_TYPEMASK == 0x0fff0000 && SF_FORMAT_SUBMASK == 0x0000ffff,
    "audio protocol format mask layout required");
int main(void)
{
	SNDFILE *(*open_fn)(SF_VIRTUAL_IO *, int, SF_INFO *, void *) = sf_open_virtual;
	sf_count_t (*read_fn)(SNDFILE *, short *, sf_count_t) = sf_readf_short;
	int (*close_fn)(SNDFILE *) = sf_close;
	int (*check_fn)(const SF_INFO *) = sf_format_check;
	return !open_fn || !read_fn || !close_fn || !check_fn;
}
]=] ST_AUDIO_FILE_SNDFILE_HEADERS_VALID)
		set(CMAKE_REQUIRED_INCLUDES "${_saved_includes}")
		set(CMAKE_TRY_COMPILE_TARGET_TYPE "${_saved_type}")
		if(ST_AUDIO_FILE_SNDFILE_HEADERS_VALID)
			set(ST_AUDIO_FILE_HAVE_SNDFILE 1)
		endif()
	endif()
	if(NOT ST_AUDIO_FILE_HAVE_SNDFILE AND _sndfile_policy STREQUAL "ON")
		message(FATAL_ERROR "ST_AUDIO_FILE_SNDFILE=ON requires usable libsndfile headers")
	endif()
endif()
configure_file(st_audio_file_config.h.in st_audio_file_config.h)
add_library(st_audio_file STATIC st_audio_file.c)
target_include_directories(st_audio_file PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
	PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
if(ST_AUDIO_FILE_HAVE_SNDFILE)
	target_include_directories(st_audio_file SYSTEM PRIVATE ${ST_AUDIO_FILE_SNDFILE_INCLUDE_DIR})
endif()
target_link_libraries(st_audio_file PUBLIC syncterm_io_error
	PRIVATE syncterm_filesystem syncterm_dynamic_library syncterm_threads)
set_target_properties(st_audio_file PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(MSVC)
	target_compile_options(st_audio_file PRIVATE /W4 /WX)
else()
	target_compile_options(st_audio_file PRIVATE -Wall -Wextra -Wpedantic -Werror)
	# The runtime loader's platform contract permits the symbol-to-function
	# pointer casts, as in the other dynamically loaded media backends.
	set_source_files_properties(st_audio_file.c PROPERTIES COMPILE_OPTIONS -Wno-pedantic)
endif()
if(BUILD_TESTING)
	add_executable(st_audio_file_test tests/st_audio_file_test.c)
	target_link_libraries(st_audio_file_test PRIVATE st_audio_file)
	set_target_properties(st_audio_file_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_audio_file_test PRIVATE /W4 /WX)
	else()
		target_compile_options(st_audio_file_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME audio_file COMMAND st_audio_file_test)
endif()

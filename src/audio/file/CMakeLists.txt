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
set(ST_AUDIO_FILE_SNDFILE AUTO CACHE STRING "libsndfile decoder support: AUTO, ON, or OFF")
set_property(CACHE ST_AUDIO_FILE_SNDFILE PROPERTY STRINGS AUTO ON OFF)
string(TOUPPER "${ST_AUDIO_FILE_SNDFILE}" _sndfile_policy)
if(NOT _sndfile_policy MATCHES "^(AUTO|ON|OFF)$")
	message(FATAL_ERROR "ST_AUDIO_FILE_SNDFILE must be AUTO, ON, or OFF")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermDependencies.cmake")
syncterm_dependency_options(SNDFILE RUNTIME)
set(ST_AUDIO_FILE_HAVE_SNDFILE 0)
set(ST_AUDIO_FILE_SNDFILE_LINKED 0)
if(NOT _sndfile_policy STREQUAL "OFF")
	set(required)
	if(WIN32 OR APPLE OR _sndfile_policy STREQUAL "ON" OR NOT ST_DEP_SNDFILE_LINKAGE STREQUAL "AUTO"
	    OR NOT ST_DEP_SNDFILE_SOURCE STREQUAL "AUTO")
		set(required REQUIRED)
	endif()
	syncterm_dependency(NAME SNDFILE PACKAGE sndfile HEADER sndfile.h
		LIBRARIES sndfile DEFAULT RUNTIME ${required} PROBE [=[
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
]=])
	if(ST_DEP_SNDFILE_TARGET)
		set(ST_AUDIO_FILE_HAVE_SNDFILE 1)
		if(NOT ST_DEP_SNDFILE_MODE STREQUAL "RUNTIME")
			set(ST_AUDIO_FILE_SNDFILE_LINKED 1)
		endif()
	endif()
endif()
configure_file(st_audio_file_config.h.in st_audio_file_config.h)
add_library(st_audio_file STATIC st_audio_file.c)
target_include_directories(st_audio_file PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
	PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
if(ST_AUDIO_FILE_HAVE_SNDFILE)
	target_link_libraries(st_audio_file PRIVATE ${ST_DEP_SNDFILE_TARGET})
endif()
target_link_libraries(st_audio_file PUBLIC syncterm_io_error
	PRIVATE syncterm_filesystem syncterm_dynamic_library syncterm_threads)
set_target_properties(st_audio_file PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(MSVC)
	target_compile_options(st_audio_file PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
else()
	target_compile_options(st_audio_file PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	# The runtime loader's platform contract permits the symbol-to-function
	# pointer casts, as in the other dynamically loaded media backends.
	set_source_files_properties(st_audio_file.c PROPERTIES COMPILE_OPTIONS -Wno-pedantic)
endif()
if(BUILD_TESTING)
	add_executable(st_audio_file_test tests/st_audio_file_test.c)
	target_compile_definitions(st_audio_file_test PRIVATE
		ST_AUDIO_FILE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
	target_link_libraries(st_audio_file_test PRIVATE st_audio_file)
	set_target_properties(st_audio_file_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_audio_file_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_audio_file_test PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME audio_file COMMAND st_audio_file_test)
	if(TARGET st_dep_sndfile)
		get_property(_sndfile_source TARGET st_dep_sndfile PROPERTY ST_DEP_SOURCE)
		if(_sndfile_source STREQUAL "BUNDLED")
			add_test(NAME audio_file_codecs COMMAND st_audio_file_test --require-codecs)
		endif()
	endif()
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermBuildFeatures.cmake")
syncterm_export_configuration(st_audio_file
	ST_AUDIO_FILE_HAVE_SNDFILE)

# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(STPixelImage LANGUAGES C)
	include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCStandard.cmake")
	include(CTest)
endif()

set(ST_PIXEL_IMAGE_JXL AUTO CACHE STRING
	"JPEG XL decoder support: AUTO, ON, or OFF")
set_property(CACHE ST_PIXEL_IMAGE_JXL PROPERTY STRINGS AUTO ON OFF)
string(TOUPPER "${ST_PIXEL_IMAGE_JXL}" _st_pixel_image_jxl)
if(NOT _st_pixel_image_jxl MATCHES "^(AUTO|ON|OFF)$")
	message(FATAL_ERROR "ST_PIXEL_IMAGE_JXL must be AUTO, ON, or OFF")
endif()

set(ST_PIXEL_IMAGE_JPEG AUTO CACHE STRING
	"8-bit JPEG decoder support: AUTO, ON, or OFF")
set_property(CACHE ST_PIXEL_IMAGE_JPEG PROPERTY STRINGS AUTO ON OFF)
string(TOUPPER "${ST_PIXEL_IMAGE_JPEG}" _st_pixel_image_jpeg)
if(NOT _st_pixel_image_jpeg MATCHES "^(AUTO|ON|OFF)$")
	message(FATAL_ERROR "ST_PIXEL_IMAGE_JPEG must be AUTO, ON, or OFF")
endif()

if(NOT TARGET st_pixels)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../pixels"
		"${CMAKE_CURRENT_BINARY_DIR}/pixels")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermDependencies.cmake")
syncterm_dependency_options(JXL RUNTIME)
set(_st_pixel_image_have_jxl FALSE)
set(_st_pixel_image_have_jxl_threads FALSE)
set(ST_DEP_JXL_LINKED 0)
if(NOT _st_pixel_image_jxl STREQUAL "OFF")
	set(required)
	if(_st_pixel_image_jxl STREQUAL "ON" OR NOT ST_DEP_JXL_LINKAGE STREQUAL "AUTO" OR NOT ST_DEP_JXL_SOURCE STREQUAL "AUTO")
		set(required REQUIRED)
	endif()
	set(cxx)
	if(NOT ST_DEP_JXL_MODE STREQUAL "RUNTIME")
		set(cxx CXX)
	endif()
	syncterm_dependency(NAME JXL PACKAGE libjxl HEADER jxl/decode.h
		LIBRARIES jxl jxl_cms jxl_threads DEFAULT RUNTIME ${required} ${cxx} PROBE [=[
#include <jxl/cms.h>
#include <jxl/decode.h>
#include <jxl/encode.h>
int main(void) {
    JxlDecoder *d = JxlDecoderCreate(0);
    JxlColorEncoding c;
    JxlColorEncodingSetToSRGB(&c, JXL_FALSE);
    (void)JxlDecoderSetCms(d, *JxlGetDefaultCms());
    JxlDecoderDestroy(d);
    return 0;
}
]=])
	if(ST_DEP_JXL_TARGET)
		set(_st_pixel_image_have_jxl TRUE)
		if(NOT ST_DEP_JXL_MODE STREQUAL "RUNTIME")
			set(ST_DEP_JXL_LINKED 1)
		endif()
		get_property(pending TARGET ${ST_DEP_JXL_TARGET} PROPERTY ST_DEP_PENDING)
		if(pending)
			set(_st_pixel_image_have_jxl_threads TRUE)
		else()
			get_property(CMAKE_REQUIRED_INCLUDES TARGET ${ST_DEP_JXL_TARGET} PROPERTY INTERFACE_INCLUDE_DIRECTORIES)
			set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
			syncterm_check_c17_source([=[
#include <jxl/resizable_parallel_runner.h>
int main(void) { JxlParallelRunner r = JxlResizableParallelRunner; return r == 0; }
]=] ST_PIXEL_IMAGE_JXL_THREADS_HEADERS_VALID)
			set(_st_pixel_image_have_jxl_threads "${ST_PIXEL_IMAGE_JXL_THREADS_HEADERS_VALID}")
			unset(CMAKE_TRY_COMPILE_TARGET_TYPE)
			unset(CMAKE_REQUIRED_INCLUDES)
		endif()
	endif()
endif()

syncterm_dependency_options(JPEG RUNTIME)
set(_st_pixel_image_have_jpeg FALSE)
set(ST_DEP_JPEG_LINKED 0)
if(NOT _st_pixel_image_jpeg STREQUAL "OFF")
	set(required)
	if(_st_pixel_image_jpeg STREQUAL "ON" OR NOT ST_DEP_JPEG_LINKAGE STREQUAL "AUTO" OR NOT ST_DEP_JPEG_SOURCE STREQUAL "AUTO")
		set(required REQUIRED)
	endif()
	syncterm_dependency(NAME JPEG PACKAGE libjpeg HEADER jpeglib.h
		LIBRARIES jpeg DEFAULT RUNTIME ${required} PROBE [=[
#include <stddef.h>
#include <stdio.h>
#include <jpeglib.h>
#include <jerror.h>
_Static_assert(sizeof(JSAMPLE) == 1, "JPEG decoding requires 8-bit samples");
int main(void) {
    struct jpeg_decompress_struct d = {0};
    struct jpeg_error_mgr e;
    unsigned char input[] = {0xff, 0xd8, 0xff, 0xd9};
    JSAMPARRAY rows = NULL;
    d.err = jpeg_std_error(&e);
    jpeg_CreateDecompress(&d, JPEG_LIB_VERSION, sizeof(d));
    jpeg_mem_src(&d, input, sizeof(input));
    (void)jpeg_read_header(&d, TRUE);
    d.out_color_space = JCS_RGB;
    d.dct_method = JDCT_ISLOW;
    d.quantize_colors = FALSE;
    if (d.data_precision == 8 && !d.arith_code
        && d.comp_info[0].h_samp_factor == 1
        && d.comp_info[0].v_samp_factor == 1)
        (void)jpeg_start_decompress(&d);
    (void)jpeg_read_scanlines(&d, rows, 1);
    (void)jpeg_finish_decompress(&d);
    jpeg_destroy_decompress(&d);
    return JERR_OUT_OF_MEMORY == 0;
}
]=])
	if(ST_DEP_JPEG_TARGET)
		set(_st_pixel_image_have_jpeg TRUE)
		if(NOT ST_DEP_JPEG_MODE STREQUAL "RUNTIME")
			set(ST_DEP_JPEG_LINKED 1)
		endif()
	endif()
endif()

add_library(st_pixel_image STATIC st_pixel_image.c st_pixel_image_jxl.c
	st_pixel_image_jpeg.c)
if(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND _st_pixel_image_have_jpeg)
	set_source_files_properties(st_pixel_image_jpeg.c PROPERTIES
		COMPILE_OPTIONS -Wno-pedantic)
endif()
if(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND _st_pixel_image_have_jxl)
	set_source_files_properties(st_pixel_image_jxl.c PROPERTIES
		COMPILE_OPTIONS -Wno-pedantic)
endif()
target_include_directories(st_pixel_image PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_pixel_image PUBLIC st_pixels syncterm_io_error)
if(NOT MSVC)
	target_link_libraries(st_pixel_image PRIVATE m)
endif()
if(_st_pixel_image_have_jxl OR _st_pixel_image_have_jpeg)
	if(NOT TARGET syncterm_dynamic_library)
		add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../base/dynamic_library"
			"${CMAKE_CURRENT_BINARY_DIR}/dynamic_library")
	endif()
	if(NOT TARGET syncterm_threads)
		add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../base/threads"
			"${CMAKE_CURRENT_BINARY_DIR}/threads")
	endif()
	target_link_libraries(st_pixel_image PRIVATE syncterm_dynamic_library syncterm_threads)
endif()
if(_st_pixel_image_have_jpeg)
	target_link_libraries(st_pixel_image PRIVATE ${ST_DEP_JPEG_TARGET})
	target_compile_definitions(st_pixel_image PRIVATE ST_PIXEL_IMAGE_HAVE_JPEG=1 ST_DEP_JPEG_LINKED=${ST_DEP_JPEG_LINKED})
endif()
if(_st_pixel_image_have_jxl)
	target_link_libraries(st_pixel_image PRIVATE ${ST_DEP_JXL_TARGET})
	target_compile_definitions(st_pixel_image PRIVATE ST_PIXEL_IMAGE_HAVE_JXL=1 ST_DEP_JXL_LINKED=${ST_DEP_JXL_LINKED})
	if(_st_pixel_image_have_jxl_threads)
		target_compile_definitions(st_pixel_image PRIVATE
			ST_PIXEL_IMAGE_HAVE_JXL_THREADS=1)
	endif()
endif()
set_target_properties(st_pixel_image PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)

if(MSVC)
	target_compile_options(st_pixel_image PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
else()
	target_compile_options(st_pixel_image PRIVATE
		-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
endif()

if(BUILD_TESTING)
	add_executable(st_pixel_image_test tests/st_pixel_image_test.c)
	target_link_libraries(st_pixel_image_test PRIVATE st_pixel_image)
	target_compile_definitions(st_pixel_image_test PRIVATE
		ST_PIXEL_IMAGE_TEST_HAVE_JXL=$<BOOL:${_st_pixel_image_have_jxl}>)
	set_target_properties(st_pixel_image_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_pixel_image_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_pixel_image_test PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME pixel_image COMMAND st_pixel_image_test)
	add_test(NAME pixel_jpeg_dependency COMMAND ${CMAKE_COMMAND}
		"-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/jpeg-dependency-test"
		"-DCMAKE_GENERATOR=${CMAKE_GENERATOR}"
		-P "${CMAKE_CURRENT_SOURCE_DIR}/tests/jpeg_dependency.cmake")
	foreach(variant native disabled)
		add_executable(st_pixel_jpeg_${variant}_test tests/st_pixel_jpeg_test.c)
		if(variant STREQUAL "disabled")
			target_sources(st_pixel_jpeg_${variant}_test PRIVATE st_pixel_image_jpeg.c)
			target_include_directories(st_pixel_jpeg_${variant}_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
			target_link_libraries(st_pixel_jpeg_${variant}_test PRIVATE st_pixels syncterm_io_error)
			set(have_jpeg FALSE)
		else()
			target_link_libraries(st_pixel_jpeg_${variant}_test PRIVATE st_pixel_image)
			set(have_jpeg "${_st_pixel_image_have_jpeg}")
		endif()
		target_compile_definitions(st_pixel_jpeg_${variant}_test PRIVATE
			ST_TEST_JPEG=$<BOOL:${have_jpeg}>)
		set_target_properties(st_pixel_jpeg_${variant}_test PROPERTIES
			C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
		if(MSVC)
			target_compile_options(st_pixel_jpeg_${variant}_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
		else()
			target_compile_options(st_pixel_jpeg_${variant}_test PRIVATE
				-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
		endif()
		add_test(NAME pixel_jpeg_${variant} COMMAND st_pixel_jpeg_${variant}_test
			"${CMAKE_CURRENT_SOURCE_DIR}/tests/jpeg")
	endforeach()
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermBuildFeatures.cmake")
syncterm_export_configuration(st_pixel_image
	_st_pixel_image_have_jxl _st_pixel_image_have_jpeg)

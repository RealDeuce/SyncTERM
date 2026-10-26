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

if(NOT TARGET st_pixels)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../pixels"
		"${CMAKE_CURRENT_BINARY_DIR}/pixels")
endif()

set(_st_pixel_image_have_jxl FALSE)
set(_st_pixel_image_have_jxl_threads FALSE)
if(NOT _st_pixel_image_jxl STREQUAL "OFF")
	include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCheckC17Source.cmake")
	find_path(ST_PIXEL_IMAGE_JXL_INCLUDE_DIR jxl/decode.h)
	if(ST_PIXEL_IMAGE_JXL_INCLUDE_DIR)
		set(_st_saved_required_includes "${CMAKE_REQUIRED_INCLUDES}")
		set(_st_saved_try_compile_target_type "${CMAKE_TRY_COMPILE_TARGET_TYPE}")
		set(CMAKE_REQUIRED_INCLUDES "${ST_PIXEL_IMAGE_JXL_INCLUDE_DIR}")
		set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
		syncterm_check_c17_source([=[
#include <jxl/cms.h>
#include <jxl/decode.h>
#include <jxl/encode.h>
int main(void)
{
	JxlDecoder *decoder = 0;
	JxlColorEncoding colour;
	const JxlCmsInterface *cms = JxlGetDefaultCms();
	JxlColorEncodingSetToSRGB(&colour, JXL_FALSE);
	(void)JxlDecoderSetCms(decoder, *cms);
	(void)JxlDecoderSetOutputColorProfile(decoder, &colour, 0, 0);
	return JPEGXL_MAJOR_VERSION < 0;
}
]=] ST_PIXEL_IMAGE_JXL_HEADERS_VALID)
		syncterm_check_c17_source([=[
#include <jxl/decode.h>
#include <jxl/resizable_parallel_runner.h>
int main(void)
{
	JxlParallelRunner runner = JxlResizableParallelRunner;
	return runner == 0;
}
]=] ST_PIXEL_IMAGE_JXL_THREADS_HEADERS_VALID)
		set(CMAKE_REQUIRED_INCLUDES "${_st_saved_required_includes}")
		set(CMAKE_TRY_COMPILE_TARGET_TYPE "${_st_saved_try_compile_target_type}")
	endif()
	if(ST_PIXEL_IMAGE_JXL_HEADERS_VALID)
		set(_st_pixel_image_have_jxl TRUE)
		if(ST_PIXEL_IMAGE_JXL_THREADS_HEADERS_VALID)
			set(_st_pixel_image_have_jxl_threads TRUE)
		endif()
	elseif(_st_pixel_image_jxl STREQUAL "ON")
		message(FATAL_ERROR
			"ST_PIXEL_IMAGE_JXL=ON requires usable libjxl decode and CMS headers")
	endif()
endif()

add_library(st_pixel_image STATIC st_pixel_image.c st_pixel_image_jxl.c)
if(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND _st_pixel_image_have_jxl)
	set_source_files_properties(st_pixel_image_jxl.c PROPERTIES
		COMPILE_OPTIONS -Wno-pedantic)
endif()
target_include_directories(st_pixel_image PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_pixel_image PUBLIC st_pixels syncterm_io_error)
if(NOT MSVC)
	target_link_libraries(st_pixel_image PRIVATE m)
endif()
if(_st_pixel_image_have_jxl)
	if(NOT TARGET syncterm_dynamic_library)
		add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../base/dynamic_library"
			"${CMAKE_CURRENT_BINARY_DIR}/dynamic_library")
	endif()
	if(NOT TARGET syncterm_threads)
		add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../base/threads"
			"${CMAKE_CURRENT_BINARY_DIR}/threads")
	endif()
	target_include_directories(st_pixel_image SYSTEM PRIVATE
		${ST_PIXEL_IMAGE_JXL_INCLUDE_DIR})
	target_compile_definitions(st_pixel_image PRIVATE ST_PIXEL_IMAGE_HAVE_JXL=1)
	target_link_libraries(st_pixel_image PRIVATE syncterm_dynamic_library
		syncterm_threads)
	if(_st_pixel_image_have_jxl_threads)
		target_compile_definitions(st_pixel_image PRIVATE
			ST_PIXEL_IMAGE_HAVE_JXL_THREADS=1)
	endif()
endif()
set_target_properties(st_pixel_image PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)

if(MSVC)
	target_compile_options(st_pixel_image PRIVATE /W4 /WX)
else()
	target_compile_options(st_pixel_image PRIVATE
		-Wall -Wextra -Wpedantic -Werror)
endif()

if(BUILD_TESTING)
	add_executable(st_pixel_image_test tests/st_pixel_image_test.c)
	target_link_libraries(st_pixel_image_test PRIVATE st_pixel_image)
	target_compile_definitions(st_pixel_image_test PRIVATE
		ST_PIXEL_IMAGE_TEST_HAVE_JXL=$<BOOL:${_st_pixel_image_have_jxl}>)
	set_target_properties(st_pixel_image_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_pixel_image_test PRIVATE /W4 /WX)
	else()
		target_compile_options(st_pixel_image_test PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME pixel_image COMMAND st_pixel_image_test)
endif()

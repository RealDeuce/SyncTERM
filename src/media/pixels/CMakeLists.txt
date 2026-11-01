# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(STPixels LANGUAGES C)
	include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCStandard.cmake")
	include(CTest)
endif()

if(NOT TARGET syncterm_io_error)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../base/io_error"
		"${CMAKE_CURRENT_BINARY_DIR}/io_error")
endif()

add_library(st_pixels STATIC st_pixels.c st_scaler.c st_pointy.c st_xbr.c)
target_include_directories(st_pixels PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_pixels PUBLIC syncterm_io_error)
set_target_properties(st_pixels PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)

if(MSVC)
	target_compile_options(st_pixels PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
else()
	target_compile_options(st_pixels PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
endif()

if(BUILD_TESTING)
	if(NOT TARGET st_time)
		add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../base/time"
			"${CMAKE_CURRENT_BINARY_DIR}/time")
	endif()
	add_executable(st_pixels_test tests/st_pixels_test.c)
	target_link_libraries(st_pixels_test PRIVATE st_pixels)
	set_target_properties(st_pixels_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_pixels_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_pixels_test PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME st_pixels COMMAND st_pixels_test)

	add_executable(st_scaler_test tests/st_scaler_test.c)
	target_link_libraries(st_scaler_test PRIVATE st_pixels)
	set_target_properties(st_scaler_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_scaler_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_scaler_test PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME st_scaler COMMAND st_scaler_test)

	add_executable(st_scaler_benchmark tests/st_scaler_benchmark.c)
	target_link_libraries(st_scaler_benchmark PRIVATE st_pixels st_time)
	set_target_properties(st_scaler_benchmark PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_scaler_benchmark PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_scaler_benchmark PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
endif()

# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(STTime LANGUAGES C)
	include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCStandard.cmake")
	include(CTest)
endif()

if(NOT TARGET syncterm_io_error)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../io_error"
		"${CMAKE_CURRENT_BINARY_DIR}/io_error")
endif()

if(WIN32)
	set(ST_TIME_PLATFORM_SOURCE st_time_windows.c)
	set(ST_TIME_PLATFORM_LIBRARIES)
else()
	include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCheckC17Source.cmake")
	set(_st_time_saved_required_libraries ${CMAKE_REQUIRED_LIBRARIES})
	set(_st_time_saved_required_definitions ${CMAKE_REQUIRED_DEFINITIONS})
	if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
		set(ST_TIME_DEFINITIONS _GNU_SOURCE)
		list(APPEND CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
	endif()
	syncterm_check_c17_source([=[
#include <time.h>
int main(void) { struct timespec value; return clock_gettime(CLOCK_MONOTONIC, &value); }
]=] ST_TIME_HAVE_MONOTONIC)
	if(NOT ST_TIME_HAVE_MONOTONIC)
		set(CMAKE_REQUIRED_LIBRARIES ${_st_time_saved_required_libraries} rt)
		syncterm_check_c17_source([=[
#include <time.h>
int main(void) { struct timespec value; return clock_gettime(CLOCK_MONOTONIC, &value); }
]=] ST_TIME_HAVE_MONOTONIC_WITH_RT)
		if(ST_TIME_HAVE_MONOTONIC_WITH_RT)
			set(ST_TIME_PLATFORM_LIBRARIES rt)
		endif()
	endif()
	set(CMAKE_REQUIRED_LIBRARIES ${_st_time_saved_required_libraries}
		${ST_TIME_PLATFORM_LIBRARIES})
	syncterm_check_c17_source([=[
#include <time.h>
int main(void) { struct timespec value; return clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &value); }
]=] ST_TIME_HAVE_PROCESS_CPU_CLOCK)
	set(CMAKE_REQUIRED_LIBRARIES ${_st_time_saved_required_libraries})
	set(CMAKE_REQUIRED_DEFINITIONS ${_st_time_saved_required_definitions})
	if(NOT ST_TIME_HAVE_MONOTONIC AND NOT ST_TIME_HAVE_MONOTONIC_WITH_RT)
		message(FATAL_ERROR
			"The selected POSIX programming environment does not provide a monotonic clock")
	endif()
	set(ST_TIME_PLATFORM_SOURCE st_time_posix.c)
endif()

configure_file(st_time_config.h.in st_time_config.h)
add_library(st_time STATIC st_time.c ${ST_TIME_PLATFORM_SOURCE})
target_include_directories(st_time PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(st_time PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(st_time PUBLIC syncterm_io_error
	PRIVATE ${ST_TIME_PLATFORM_LIBRARIES})
target_compile_definitions(st_time PRIVATE ${ST_TIME_DEFINITIONS})
set_target_properties(st_time PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(MSVC)
	target_compile_options(st_time PRIVATE /W4 /WX)
else()
	target_compile_options(st_time PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(BUILD_TESTING)
	add_executable(st_time_test tests/st_time_test.c)
	target_link_libraries(st_time_test PRIVATE st_time)
	add_executable(st_time_fault_test tests/st_time_fault_test.c)
	target_include_directories(st_time_fault_test PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
		${CMAKE_CURRENT_SOURCE_DIR}/../io_error)
	set_target_properties(st_time_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	set_target_properties(st_time_fault_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_time_test PRIVATE /W4 /WX)
		target_compile_options(st_time_fault_test PRIVATE /W4 /WX)
	else()
		target_compile_options(st_time_test PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
		target_compile_options(st_time_fault_test PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME time COMMAND st_time_test)
	add_test(NAME time_fault COMMAND st_time_fault_test)
	if(NOT WIN32)
		add_executable(st_time_posix_test tests/st_time_posix_test.c)
		target_include_directories(st_time_posix_test PRIVATE
			${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
		target_link_libraries(st_time_posix_test PRIVATE syncterm_io_error)
		target_compile_definitions(st_time_posix_test PRIVATE ${ST_TIME_DEFINITIONS})
		set_target_properties(st_time_posix_test PROPERTIES
			C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
		target_compile_options(st_time_posix_test PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
		add_test(NAME time_posix_failure COMMAND st_time_posix_test)
	endif()
endif()

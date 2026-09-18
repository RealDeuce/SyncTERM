# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(STWebGet LANGUAGES C)
	include(CTest)
endif()

if(NOT TARGET st_file_lock)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../file_lock"
		"${CMAKE_CURRENT_BINARY_DIR}/file_lock")
endif()
if(NOT TARGET st_tls)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../xptls"
		"${CMAKE_CURRENT_BINARY_DIR}/xptls")
endif()
foreach(_dependency IN ITEMS filesystem ini io_error path socket string_compat
		string_util tcp_client)
	if(NOT TARGET syncterm_${_dependency})
		add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../${_dependency}"
			"${CMAKE_CURRENT_BINARY_DIR}/${_dependency}")
	endif()
endforeach()

add_library(st_webget STATIC st_webget.c)
target_include_directories(st_webget PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_webget PUBLIC syncterm_io_error st_tls PRIVATE
	st_file_lock syncterm_filesystem syncterm_ini syncterm_socket
	syncterm_string_compat syncterm_string_util syncterm_path
	syncterm_tcp_client)
set_target_properties(st_webget PROPERTIES C_STANDARD 17
	C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(MSVC)
	target_compile_options(st_webget PRIVATE /W4 /WX)
else()
	target_compile_options(st_webget PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(BUILD_TESTING)
	if(NOT TARGET syncterm_threads)
		add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../threads"
			"${CMAKE_CURRENT_BINARY_DIR}/threads")
	endif()
	add_executable(st_webget_test tests/st_webget_test.c)
	target_link_libraries(st_webget_test PRIVATE st_webget
		syncterm_filesystem syncterm_socket syncterm_threads)
	target_compile_definitions(st_webget_test PRIVATE
		ST_WEBGET_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/webget-test")
	set_target_properties(st_webget_test PROPERTIES C_STANDARD 17
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_webget_test PRIVATE /W4 /WX)
	else()
		target_compile_options(st_webget_test PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME webget COMMAND st_webget_test)
	set_tests_properties(webget PROPERTIES TIMEOUT 10)
endif()

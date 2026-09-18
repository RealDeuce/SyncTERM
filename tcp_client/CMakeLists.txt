# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(SyncTERMTCPClient LANGUAGES C)
	include(CTest)
	include(CheckCSourceCompiles)
	set(CMAKE_C_STANDARD 17)
	set(CMAKE_C_STANDARD_REQUIRED ON)
	set(CMAKE_C_EXTENSIONS OFF)
	if(WIN32)
		set(SYNCTERM_PLATFORM_WINDOWS TRUE)
		set(SYNCTERM_PLATFORM_POSIX FALSE)
	else()
		set(SYNCTERM_PLATFORM_WINDOWS FALSE)
		set(SYNCTERM_PLATFORM_POSIX TRUE)
	endif()
	check_c_source_compiles([=[
#include <sys/types.h>
int main(void) { ssize_t value = 0; return value != 0; }
]=] SYNCTERM_HAVE_SSIZE_T)
	configure_file("${CMAKE_CURRENT_SOURCE_DIR}/../syncterm_config.h.in"
		"${CMAKE_CURRENT_BINARY_DIR}/syncterm_config.h" @ONLY)
endif()

foreach(_dependency IN ITEMS io_error socket)
	if(NOT TARGET syncterm_${_dependency})
		add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../${_dependency}"
			"${CMAKE_CURRENT_BINARY_DIR}/${_dependency}")
	endif()
endforeach()

set(SYNCTERM_TCP_CLIENT_DEFINITIONS)
if(WIN32)
	set(SYNCTERM_TCP_CLIENT_RESOLVER tcp_client_resolve_windows.c)
else()
	set(SYNCTERM_TCP_CLIENT_RESOLVER tcp_client_resolve_posix.c)
	if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
		list(APPEND SYNCTERM_TCP_CLIENT_DEFINITIONS _GNU_SOURCE)
	endif()
endif()

include(CheckCSourceCompiles)
set(_tcp_client_saved_required_definitions ${CMAKE_REQUIRED_DEFINITIONS})
set(_tcp_client_saved_required_libraries ${CMAKE_REQUIRED_LIBRARIES})
if(WIN32)
	set(CMAKE_REQUIRED_LIBRARIES
		${_tcp_client_saved_required_libraries} ws2_32)
	check_c_source_compiles([=[
#include <winsock2.h>
#include <ws2tcpip.h>
int main(void)
{
	struct addrinfo hints = {0};
	struct addrinfo *result = 0;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_NUMERICSERV;
	if (getaddrinfo("127.0.0.1", "23", &hints, &result) == 0)
		freeaddrinfo(result);
	return 0;
}
]=] SYNCTERM_TCP_CLIENT_RESOLVER_BASELINE)
else()
	if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
		set(CMAKE_REQUIRED_DEFINITIONS
			${_tcp_client_saved_required_definitions} -D_GNU_SOURCE)
	endif()
	check_c_source_compiles([=[
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
int main(void)
{
	struct addrinfo hints = {0};
	struct addrinfo *result = 0;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_NUMERICSERV;
	if (getaddrinfo("127.0.0.1", "23", &hints, &result) == 0)
		freeaddrinfo(result);
	return 0;
}
]=] SYNCTERM_TCP_CLIENT_RESOLVER_BASELINE)
endif()
set(CMAKE_REQUIRED_DEFINITIONS ${_tcp_client_saved_required_definitions})
set(CMAKE_REQUIRED_LIBRARIES ${_tcp_client_saved_required_libraries})
if(NOT SYNCTERM_TCP_CLIENT_RESOLVER_BASELINE)
	message(FATAL_ERROR
		"The selected programming environment does not provide the required getaddrinfo resolver API")
endif()

add_library(syncterm_tcp_client STATIC
	tcp_client.c ${SYNCTERM_TCP_CLIENT_RESOLVER})
target_include_directories(syncterm_tcp_client PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(syncterm_tcp_client
	PUBLIC syncterm_socket syncterm_io_error)
if(SYNCTERM_TCP_CLIENT_DEFINITIONS)
	target_compile_definitions(syncterm_tcp_client PRIVATE
		${SYNCTERM_TCP_CLIENT_DEFINITIONS})
endif()
set_target_properties(syncterm_tcp_client PROPERTIES
	C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)

function(syncterm_tcp_client_warnings target)
	if(MSVC)
		target_compile_options(${target} PRIVATE /W4 /WX)
	else()
		target_compile_options(${target} PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
endfunction()
syncterm_tcp_client_warnings(syncterm_tcp_client)

if(BUILD_TESTING)
	add_executable(tcp_client_test tests/tcp_client_test.c)
	target_link_libraries(tcp_client_test PRIVATE syncterm_tcp_client)
	if(SYNCTERM_TCP_CLIENT_DEFINITIONS)
		target_compile_definitions(tcp_client_test PRIVATE
			${SYNCTERM_TCP_CLIENT_DEFINITIONS})
	endif()
	set_target_properties(tcp_client_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	syncterm_tcp_client_warnings(tcp_client_test)
	add_test(NAME tcp_client COMMAND tcp_client_test)
	set_tests_properties(tcp_client PROPERTIES TIMEOUT 10)

	add_executable(tcp_client_algorithm_test
		tcp_client.c tests/tcp_client_algorithm_test.c
		tests/tcp_client_fake.c)
	target_include_directories(tcp_client_algorithm_test PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../socket
		${CMAKE_CURRENT_SOURCE_DIR}/.. ${PROJECT_BINARY_DIR})
	target_link_libraries(tcp_client_algorithm_test PRIVATE syncterm_io_error)
	set_target_properties(tcp_client_algorithm_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	syncterm_tcp_client_warnings(tcp_client_algorithm_test)
	add_test(NAME tcp_client_algorithm COMMAND tcp_client_algorithm_test)
	set_tests_properties(tcp_client_algorithm PROPERTIES TIMEOUT 10)
endif()

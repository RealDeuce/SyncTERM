# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(SyncTERMSocket LANGUAGES C)
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

# Aggregating component builds also need the common generated type header.
if(NOT EXISTS "${PROJECT_BINARY_DIR}/syncterm_config.h")
	if(WIN32)
		set(SYNCTERM_PLATFORM_WINDOWS TRUE)
		set(SYNCTERM_PLATFORM_POSIX FALSE)
	else()
		set(SYNCTERM_PLATFORM_WINDOWS FALSE)
		set(SYNCTERM_PLATFORM_POSIX TRUE)
	endif()
	include(CheckCSourceCompiles)
	check_c_source_compiles([=[
#include <sys/types.h>
int main(void) { ssize_t value = 0; return value != 0; }
]=] SYNCTERM_HAVE_SSIZE_T)
	configure_file("${CMAKE_CURRENT_SOURCE_DIR}/../syncterm_config.h.in"
		"${PROJECT_BINARY_DIR}/syncterm_config.h" @ONLY)
endif()

if(NOT TARGET syncterm_io_error)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../io_error"
		"${CMAKE_CURRENT_BINARY_DIR}/io_error")
endif()
if(NOT TARGET st_time)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../time"
		"${CMAKE_CURRENT_BINARY_DIR}/time")
endif()

include(CheckCSourceCompiles)

set(SYNCTERM_SOCKET_DEFINITIONS)
set(_socket_probe_definitions)
if(NOT WIN32 AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
	list(APPEND SYNCTERM_SOCKET_DEFINITIONS _GNU_SOURCE)
	list(APPEND _socket_probe_definitions -D_GNU_SOURCE)
endif()

if(WIN32)
	set(SYNCTERM_SOCKET_PLATFORM_SOURCE socket_windows.c)
	set(SYNCTERM_SOCKET_PLATFORM_LIBRARIES ws2_32)
else()
	set(SYNCTERM_SOCKET_PLATFORM_SOURCE socket_posix.c)
	set(_socket_saved_required_definitions ${CMAKE_REQUIRED_DEFINITIONS})
	set(_socket_saved_required_libraries ${CMAKE_REQUIRED_LIBRARIES})
	set(CMAKE_REQUIRED_DEFINITIONS ${_socket_saved_required_definitions}
		${_socket_probe_definitions})
	check_c_source_compiles([=[
#include <fcntl.h>
#include <poll.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
int main(void)
{
	struct pollfd item = {0, POLLIN | POLLOUT | POLLPRI, 0};
	struct timespec now;
	fd_set descriptors;
	int flags = fcntl(0, F_GETFL);
	FD_ZERO(&descriptors);
	(void)select(0, &descriptors, &descriptors, &descriptors, 0);
	(void)poll(&item, 1, 0);
	(void)clock_gettime(CLOCK_MONOTONIC, &now);
	(void)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	(void)sockatmark(0);
	(void)send(0, "", 0, MSG_DONTWAIT);
	(void)recv(0, 0, 0, MSG_PEEK | MSG_DONTWAIT);
	return flags == -2;
}
]=] SYNCTERM_SOCKET_POSIX_BASELINE)
	if(NOT SYNCTERM_SOCKET_POSIX_BASELINE)
		set(CMAKE_REQUIRED_LIBRARIES
			${_socket_saved_required_libraries} rt)
		check_c_source_compiles([=[
#include <fcntl.h>
#include <poll.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
int main(void)
{
	struct pollfd item = {0, POLLIN | POLLOUT | POLLPRI, 0};
	struct timespec now;
	fd_set descriptors;
	FD_ZERO(&descriptors);
	(void)select(0, &descriptors, &descriptors, &descriptors, 0);
	(void)fcntl(0, F_GETFL);
	(void)poll(&item, 1, 0);
	(void)clock_gettime(CLOCK_MONOTONIC, &now);
	(void)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	(void)sockatmark(0);
	(void)send(0, "", 0, MSG_DONTWAIT);
	(void)recv(0, 0, 0, MSG_PEEK | MSG_DONTWAIT);
	return 0;
}
]=] SYNCTERM_SOCKET_POSIX_BASELINE_WITH_RT)
		if(SYNCTERM_SOCKET_POSIX_BASELINE_WITH_RT)
			set(SYNCTERM_SOCKET_PLATFORM_LIBRARIES rt)
		endif()
	endif()
	set(CMAKE_REQUIRED_LIBRARIES ${_socket_saved_required_libraries})
	if(NOT SYNCTERM_SOCKET_POSIX_BASELINE AND
	    NOT SYNCTERM_SOCKET_POSIX_BASELINE_WITH_RT)
		message(FATAL_ERROR
			"The selected POSIX programming environment does not provide the required socket, poll, fcntl, and monotonic-clock APIs")
	endif()
	check_c_source_compiles([=[
#include <sys/socket.h>
int main(void) { return MSG_NOSIGNAL == 0; }
]=] SYNCTERM_SOCKET_HAVE_MSG_NOSIGNAL)
	check_c_source_compiles([=[
#include <sys/socket.h>
int main(void) { return SO_NOSIGPIPE == 0; }
]=] SYNCTERM_SOCKET_HAVE_SO_NOSIGPIPE)
	if(NOT SYNCTERM_SOCKET_HAVE_MSG_NOSIGNAL AND
	    NOT SYNCTERM_SOCKET_HAVE_SO_NOSIGPIPE)
		message(FATAL_ERROR
			"The target does not provide a per-operation or per-socket way to suppress SIGPIPE")
	endif()
	set(CMAKE_REQUIRED_DEFINITIONS ${_socket_saved_required_definitions})
endif()

configure_file(socket_config.h.in socket_config.h @ONLY)

add_library(syncterm_socket STATIC ${SYNCTERM_SOCKET_PLATFORM_SOURCE})
target_include_directories(syncterm_socket
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} "${CMAKE_CURRENT_SOURCE_DIR}/.."
		"${PROJECT_BINARY_DIR}"
	PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(syncterm_socket PUBLIC syncterm_io_error
	PRIVATE st_time ${SYNCTERM_SOCKET_PLATFORM_LIBRARIES})
if(SYNCTERM_SOCKET_DEFINITIONS)
	target_compile_definitions(syncterm_socket PRIVATE
		${SYNCTERM_SOCKET_DEFINITIONS})
endif()
set_target_properties(syncterm_socket PROPERTIES
	C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)

function(syncterm_socket_warnings target)
	if(MSVC)
		target_compile_options(${target} PRIVATE /W4 /WX)
	else()
		target_compile_options(${target} PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
endfunction()
syncterm_socket_warnings(syncterm_socket)

if(BUILD_TESTING)
	add_executable(socket_test tests/socket_test.c)
	target_link_libraries(socket_test PRIVATE syncterm_socket)
	if(SYNCTERM_SOCKET_DEFINITIONS)
		target_compile_definitions(socket_test PRIVATE
			${SYNCTERM_SOCKET_DEFINITIONS})
	endif()
	set_target_properties(socket_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	syncterm_socket_warnings(socket_test)
	add_test(NAME socket COMMAND socket_test)
	set_tests_properties(socket PROPERTIES TIMEOUT 10)
endif()

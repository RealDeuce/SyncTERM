# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(SyncTERMSocket LANGUAGES C)
	include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCStandard.cmake")
	include(CTest)
	include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCheckC17Source.cmake")
	set(CMAKE_C_STANDARD ${SYNCTERM_C_STANDARD})
	set(CMAKE_C_STANDARD_REQUIRED ON)
	set(CMAKE_C_EXTENSIONS OFF)
	if(WIN32)
		set(SYNCTERM_PLATFORM_WINDOWS TRUE)
		set(SYNCTERM_PLATFORM_POSIX FALSE)
	else()
		set(SYNCTERM_PLATFORM_WINDOWS FALSE)
		set(SYNCTERM_PLATFORM_POSIX TRUE)
	endif()
	syncterm_check_c17_source([=[
#include <sys/types.h>
int main(void) { ssize_t value = 0; return value != 0; }
]=] SYNCTERM_HAVE_SSIZE_T)
	configure_file("${CMAKE_CURRENT_SOURCE_DIR}/../../syncterm_config.h.in"
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
	include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCheckC17Source.cmake")
	syncterm_check_c17_source([=[
#include <sys/types.h>
int main(void) { ssize_t value = 0; return value != 0; }
]=] SYNCTERM_HAVE_SSIZE_T)
	configure_file("${CMAKE_CURRENT_SOURCE_DIR}/../../syncterm_config.h.in"
		"${PROJECT_BINARY_DIR}/syncterm_config.h" @ONLY)
endif()

if(NOT TARGET syncterm_io_error)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../base/io_error"
		"${CMAKE_CURRENT_BINARY_DIR}/io_error")
endif()
if(NOT TARGET st_time)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../base/time"
		"${CMAKE_CURRENT_BINARY_DIR}/time")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCheckC17Source.cmake")

set(SYNCTERM_SOCKET_DEFINITIONS)
set(_socket_probe_definitions)
if(NOT WIN32 AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
	list(APPEND SYNCTERM_SOCKET_DEFINITIONS _GNU_SOURCE)
	list(APPEND _socket_probe_definitions -D_GNU_SOURCE)
endif()

if(WIN32)
	set(SYNCTERM_SOCKET_PLATFORM_SOURCE socket_windows.c)
	set(SYNCTERM_SOCKET_PLATFORM_LIBRARIES ws2_32)
	set(_socket_saved_required_libraries ${CMAKE_REQUIRED_LIBRARIES})
	set(CMAKE_REQUIRED_LIBRARIES ${_socket_saved_required_libraries} ws2_32)
	syncterm_check_c17_source([=[
#include <winsock2.h>
int main(void) {
	WSADATA data;
	WSAPOLLFD item = {INVALID_SOCKET, POLLRDNORM, 0};
	(void)WSAStartup(MAKEWORD(2, 2), &data);
	int result = WSAPoll(&item, 1, 0);
	(void)WSACleanup();
	return result;
}
]=] SYNCTERM_SOCKET_HAVE_WSAPOLL)
	set(CMAKE_REQUIRED_LIBRARIES ${_socket_saved_required_libraries})
	if(NOT SYNCTERM_SOCKET_HAVE_WSAPOLL)
		message(FATAL_ERROR "The Windows socket environment must provide WSAPoll")
	endif()
else()
	set(SYNCTERM_SOCKET_PLATFORM_SOURCE socket_posix.c)
	set(_socket_saved_required_definitions ${CMAKE_REQUIRED_DEFINITIONS})
	set(_socket_saved_required_libraries ${CMAKE_REQUIRED_LIBRARIES})
	set(CMAKE_REQUIRED_DEFINITIONS ${_socket_saved_required_definitions}
		${_socket_probe_definitions})
	syncterm_check_c17_source([=[
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
		syncterm_check_c17_source([=[
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
	syncterm_check_c17_source([=[
#include <poll.h>
int main(void) { struct pollfd p = {0, POLLRDHUP, 0}; return poll(&p, 1, 0); }
]=] SYNCTERM_SOCKET_HAVE_POLLRDHUP)
	syncterm_check_c17_source([=[
#include <sys/types.h>
#include <sys/event.h>
#include <time.h>
#include <unistd.h>
int main(void) {
	struct kevent change, event;
	struct timespec timeout = {0};
	int descriptor = kqueue();
	EV_SET(&change, 0, EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0, 0);
	int result = kevent(descriptor, &change, 1, &event, 1, &timeout);
	(void)close(descriptor);
	return result > 0 && (event.flags & EV_EOF) != 0;
}
]=] SYNCTERM_SOCKET_HAVE_KQUEUE)
	if(NOT SYNCTERM_SOCKET_POSIX_BASELINE AND
	    NOT SYNCTERM_SOCKET_POSIX_BASELINE_WITH_RT)
		message(FATAL_ERROR
			"The selected POSIX programming environment does not provide the required socket, poll, fcntl, and monotonic-clock APIs")
	endif()
	syncterm_check_c17_source([=[
#include <sys/socket.h>
int main(void) { return MSG_NOSIGNAL == 0; }
]=] SYNCTERM_SOCKET_HAVE_MSG_NOSIGNAL)
	syncterm_check_c17_source([=[
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
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} "${CMAKE_CURRENT_SOURCE_DIR}/../.."
		"${PROJECT_BINARY_DIR}"
	PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(syncterm_socket PUBLIC syncterm_io_error
	PRIVATE st_time ${SYNCTERM_SOCKET_PLATFORM_LIBRARIES})
if(SYNCTERM_SOCKET_DEFINITIONS)
	target_compile_definitions(syncterm_socket PRIVATE
		${SYNCTERM_SOCKET_DEFINITIONS})
endif()
set_target_properties(syncterm_socket PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)

function(syncterm_socket_warnings target)
	if(MSVC)
		target_compile_options(${target} PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(${target} PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
endfunction()
syncterm_socket_warnings(syncterm_socket)

if(BUILD_TESTING)
	if(NOT TARGET syncterm_threads)
		add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../base/threads"
			"${CMAKE_CURRENT_BINARY_DIR}/threads")
	endif()
	add_executable(socket_test tests/socket_test.c)
	target_link_libraries(socket_test PRIVATE syncterm_socket syncterm_threads)
	if(SYNCTERM_SOCKET_DEFINITIONS)
		target_compile_definitions(socket_test PRIVATE
			${SYNCTERM_SOCKET_DEFINITIONS})
	endif()
	set_target_properties(socket_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	syncterm_socket_warnings(socket_test)
	add_test(NAME socket COMMAND socket_test)
	add_test(NAME socket_half_close COMMAND socket_test --half-close)
	set_tests_properties(socket_half_close PROPERTIES TIMEOUT 10)
	set_tests_properties(socket PROPERTIES TIMEOUT 10)
	if(WIN32)
		add_executable(socket_windows_boundary_test
			tests/socket_windows_boundary_test.c)
		target_link_libraries(socket_windows_boundary_test PRIVATE
			syncterm_socket syncterm_threads)
		set_target_properties(socket_windows_boundary_test PROPERTIES
			C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
		syncterm_socket_warnings(socket_windows_boundary_test)
		foreach(_mode pending peek taken overlapped)
			add_test(NAME socket_windows_boundary_${_mode}
				COMMAND socket_windows_boundary_test ${_mode})
			set_tests_properties(socket_windows_boundary_${_mode}
				PROPERTIES TIMEOUT 15)
		endforeach()
	endif()
	if(NOT WIN32)
		add_executable(socket_poll_error_test tests/socket_poll_error_test.c)
		target_include_directories(socket_poll_error_test PRIVATE
			${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}
			${CMAKE_CURRENT_SOURCE_DIR}/../.. ${PROJECT_BINARY_DIR})
		target_link_libraries(socket_poll_error_test PRIVATE
			syncterm_io_error st_time)
		target_compile_definitions(socket_poll_error_test PRIVATE
			${SYNCTERM_SOCKET_DEFINITIONS})
		set_target_properties(socket_poll_error_test PROPERTIES
			C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
		syncterm_socket_warnings(socket_poll_error_test)
		add_test(NAME socket_poll_errors COMMAND socket_poll_error_test)
	endif()
endif()

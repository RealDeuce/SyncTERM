# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(STSerial LANGUAGES C)
	include("${CMAKE_CURRENT_LIST_DIR}/../cmake/SynctermCStandard.cmake")
	include(CTest)
	include("${CMAKE_CURRENT_LIST_DIR}/../cmake/SynctermCheckC17Source.cmake")
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
	configure_file("${CMAKE_CURRENT_SOURCE_DIR}/../syncterm_config.h.in"
		"${CMAKE_CURRENT_BINARY_DIR}/syncterm_config.h" @ONLY)
endif()

if(NOT TARGET syncterm_io_error)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../io_error"
		"${CMAKE_CURRENT_BINARY_DIR}/io_error")
endif()
if(WIN32 AND NOT TARGET st_windows_utf)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../windows_utf"
		"${CMAKE_CURRENT_BINARY_DIR}/windows_utf")
endif()
if(WIN32 AND NOT TARGET st_time)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../time"
		"${CMAKE_CURRENT_BINARY_DIR}/time")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/SynctermCheckC17Source.cmake")

set(ST_SERIAL_DEFINITIONS)
set(_st_serial_saved_required_definitions ${CMAKE_REQUIRED_DEFINITIONS})
if(NOT WIN32 AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
	list(APPEND ST_SERIAL_DEFINITIONS _GNU_SOURCE)
	set(CMAKE_REQUIRED_DEFINITIONS
		${_st_serial_saved_required_definitions} -D_GNU_SOURCE)
endif()

if(WIN32)
	set(ST_SERIAL_PLATFORM_SOURCE serial_windows.c)
	syncterm_check_c17_source([=[
#include <windows.h>
int main(void)
{
	OVERLAPPED operation = {0};
	COMMTIMEOUTS timeouts = {0};
	COMMPROP properties = {0};
	DCB state = {0};
	DWORD value = 0;
	HANDLE port = CreateFileW(L"COM1", GENERIC_READ | GENERIC_WRITE, 0,
	    0, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, 0);
	operation.hEvent = CreateEventW(0, TRUE, FALSE, 0);
	(void)GetCommState(port, &state);
	(void)SetCommState(port, &state);
	(void)SetCommTimeouts(port, &timeouts);
	(void)SetCommMask(port, EV_RXCHAR);
	(void)WaitCommEvent(port, &value, &operation);
	(void)ReadFile(port, &value, sizeof(value), &value, &operation);
	(void)WriteFile(port, &value, sizeof(value), &value, &operation);
	(void)GetOverlappedResult(port, &operation, &value, FALSE);
	(void)CancelIo(port);
	(void)PurgeComm(port, PURGE_RXCLEAR | PURGE_TXCLEAR);
	(void)GetCommModemStatus(port, &value);
	(void)EscapeCommFunction(port, SETDTR);
	(void)GetCommProperties(port, &properties);
	(void)CloseHandle(operation.hEvent);
	(void)CloseHandle(port);
	return 0;
}
]=] ST_SERIAL_PLATFORM_BASELINE)
else()
	set(ST_SERIAL_PLATFORM_SOURCE serial_posix.c)
	syncterm_check_c17_source([=[
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
int main(void)
{
	struct termios attributes;
	struct pollfd item = {0, POLLIN | POLLOUT, 0};
	int fd = open("/dev/null", O_RDWR | O_NONBLOCK | O_NOCTTY);
	int flags = fcntl(fd, F_GETFD);
	(void)fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
	(void)tcgetattr(fd, &attributes);
	(void)cfgetispeed(&attributes);
	(void)cfgetospeed(&attributes);
	(void)cfsetispeed(&attributes, B9600);
	(void)cfsetospeed(&attributes, B9600);
	(void)tcsetattr(fd, TCSANOW, &attributes);
	(void)tcflush(fd, TCOFLUSH);
	(void)poll(&item, 1, 0);
	(void)read(fd, &flags, sizeof(flags));
	(void)write(fd, &flags, sizeof(flags));
	return close(fd);
}
]=] ST_SERIAL_PLATFORM_BASELINE)
	syncterm_check_c17_source([=[
#include <termios.h>
int main(void) { tcflag_t flags = CRTSCTS; return flags == 0; }
]=] ST_SERIAL_HAVE_CRTSCTS)
	syncterm_check_c17_source([=[
#include <termios.h>
int main(void)
{
	tcflag_t flags = CCTS_OFLOW | CRTS_IFLOW;
	return flags == 0;
}
]=] ST_SERIAL_HAVE_CCTS_OFLOW_CRTS_IFLOW)
	syncterm_check_c17_source([=[
#include <sys/ioctl.h>
#include <termios.h>
int main(void)
{
	int lines = TIOCM_DTR | TIOCM_RTS;
	(void)ioctl(0, TIOCMBIS, &lines);
	(void)ioctl(0, TIOCMBIC, &lines);
	return 0;
}
]=] ST_SERIAL_HAVE_MODEM_CONTROL)
	foreach(_line IN ITEMS TIOCM_CAR TIOCM_CD TIOCM_RNG TIOCM_RI)
		syncterm_check_c17_source("#include <sys/ioctl.h>\n#include <termios.h>\nint main(void) { return ${_line} == 0; }"
			ST_SERIAL_HAVE_${_line})
	endforeach()
	syncterm_check_c17_source([=[
#include <sys/ioctl.h>
#include <termios.h>
int main(void)
{
	int lines = TIOCM_CTS | TIOCM_DSR;
#if defined(TIOCM_CAR)
	lines |= TIOCM_CAR;
#elif defined(TIOCM_CD)
	lines |= TIOCM_CD;
#else
#error "No carrier-detect line"
#endif
#if defined(TIOCM_RNG)
	lines |= TIOCM_RNG;
#elif defined(TIOCM_RI)
	lines |= TIOCM_RI;
#else
#error "No ring-indicator line"
#endif
	return ioctl(0, TIOCMGET, &lines);
}
]=] ST_SERIAL_HAVE_MODEM_STATUS)
	foreach(_rate IN ITEMS 57600 115200 230400 460800 500000 576000
	    921600 1000000 1152000 1500000 2000000 2500000 3000000
	    3500000 4000000)
		syncterm_check_c17_source("#include <termios.h>\nint main(void) { speed_t speed = B${_rate}; return speed == 0; }"
			ST_SERIAL_HAVE_B${_rate})
	endforeach()
endif()

set(CMAKE_REQUIRED_DEFINITIONS ${_st_serial_saved_required_definitions})

if(NOT ST_SERIAL_PLATFORM_BASELINE)
	message(FATAL_ERROR
		"The selected platform does not provide the required serial-port APIs")
endif()

configure_file(serial_config.h.in serial_config.h @ONLY)

add_library(st_serial STATIC ${ST_SERIAL_PLATFORM_SOURCE})
target_include_directories(st_serial
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} "${CMAKE_CURRENT_SOURCE_DIR}/.."
		"${PROJECT_BINARY_DIR}"
	PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(st_serial PUBLIC syncterm_io_error)
if(WIN32)
	target_link_libraries(st_serial PRIVATE st_time st_windows_utf)
endif()
if(ST_SERIAL_DEFINITIONS)
	target_compile_definitions(st_serial PRIVATE ${ST_SERIAL_DEFINITIONS})
endif()
set_target_properties(st_serial PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)

function(st_serial_warnings target)
	if(MSVC)
		target_compile_options(${target} PRIVATE /W4 /WX)
	else()
		target_compile_options(${target} PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
endfunction()
st_serial_warnings(st_serial)

if(BUILD_TESTING)
	if(NOT WIN32)
		set(CMAKE_REQUIRED_DEFINITIONS
			${_st_serial_saved_required_definitions})
		if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
			list(APPEND CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
		endif()
		syncterm_check_c17_source([=[
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
int main(void)
{
	int master = posix_openpt(O_RDWR | O_NOCTTY);
	char *name;
	if (master < 0 || grantpt(master) != 0 || unlockpt(master) != 0)
		return 1;
	name = ptsname(master);
	return name == 0 || close(master) != 0;
}
]=] ST_SERIAL_TEST_HAVE_PTY)
		set(CMAKE_REQUIRED_DEFINITIONS
			${_st_serial_saved_required_definitions})
	else()
		set(ST_SERIAL_TEST_HAVE_PTY 0)
	endif()
	add_executable(serial_test tests/serial_test.c)
	target_link_libraries(serial_test PRIVATE st_serial)
	target_compile_definitions(serial_test PRIVATE
		ST_SERIAL_TEST_HAVE_PTY=$<BOOL:${ST_SERIAL_TEST_HAVE_PTY}>)
	if(ST_SERIAL_DEFINITIONS)
		target_compile_definitions(serial_test PRIVATE
			${ST_SERIAL_DEFINITIONS})
	endif()
	set_target_properties(serial_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	st_serial_warnings(serial_test)
	add_test(NAME serial COMMAND serial_test arguments)
	set_tests_properties(serial PROPERTIES TIMEOUT 10)
	add_test(NAME serial_pty COMMAND serial_test pty)
	set_tests_properties(serial_pty PROPERTIES
		SKIP_RETURN_CODE 77
		TIMEOUT 10)
endif()

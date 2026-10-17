# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/SynctermAtomics.cmake")

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(STConnection LANGUAGES C)
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

foreach(_dependency IN ITEMS io_error socket tcp_client serial threads profile settings)
	if(_dependency STREQUAL "serial")
		set(_target st_serial)
	elseif(_dependency STREQUAL "threads")
		set(_target syncterm_threads)
	elseif(_dependency STREQUAL "profile")
		set(_target st_profile)
	elseif(_dependency STREQUAL "settings")
		set(_target st_settings)
	else()
		set(_target syncterm_${_dependency})
	endif()
	if(NOT TARGET ${_target})
		add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../${_dependency}"
			"${CMAKE_CURRENT_BINARY_DIR}/${_dependency}")
	endif()
endforeach()
if(NOT TARGET st_time)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../time"
		"${CMAKE_CURRENT_BINARY_DIR}/time")
endif()
if(NOT TARGET st_sftp_client)
	add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../sftp_client"
		"${CMAKE_CURRENT_BINARY_DIR}/sftp_client")
endif()
if(NOT TARGET st_tls)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../xptls"
		"${CMAKE_CURRENT_BINARY_DIR}/xptls")
endif()
get_property(ST_TLS_PROVIDER TARGET xptls_crypto PROPERTY XPTLS_CRYPTO_PROVIDER)
if(NOT ST_TLS_PROVIDER STREQUAL "none")
	set(ST_CONNECTION_HAVE_TLS TRUE)
	include("${CMAKE_CURRENT_LIST_DIR}/../cmake/DeuceSSH.cmake")
	set(ST_CONNECTION_SSH_SOURCE connection_ssh.c)
else()
	set(ST_CONNECTION_HAVE_TLS FALSE)
	set(ST_CONNECTION_SSH_SOURCE connection_ssh_stub.c)
endif()
if(WIN32 AND NOT TARGET st_windows_utf)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../windows_utf"
		"${CMAKE_CURRENT_BINARY_DIR}/windows_utf")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/SynctermCheckC17Source.cmake")
set(ST_CONNECTION_HAVE_FORKPTY FALSE)
set(ST_CONNECTION_HAVE_CONPTY FALSE)
set(ST_CONNECTION_HAVE_TTYDEFAULTS FALSE)
set(ST_CONNECTION_HAVE_IXANY FALSE)
set(ST_CONNECTION_HAVE_VERASE2 FALSE)
set(ST_CONNECTION_HAVE_B115200 FALSE)
set(ST_CONNECTION_FORKPTY_HEADER_PTY_H FALSE)
set(ST_CONNECTION_FORKPTY_HEADER_UTIL_H FALSE)
set(ST_CONNECTION_FORKPTY_HEADER_LIBUTIL_H FALSE)
set(ST_CONNECTION_PLATFORM_LIBRARIES)
set(ST_CONNECTION_DEFINITIONS)
set(_st_connection_saved_required_libraries ${CMAKE_REQUIRED_LIBRARIES})
set(_st_connection_saved_required_definitions ${CMAKE_REQUIRED_DEFINITIONS})

if(WIN32)
	set(CMAKE_REQUIRED_LIBRARIES
		${_st_connection_saved_required_libraries} bcrypt)
	set(_st_conpty_probe [=[
#include <windows.h>
#include <bcrypt.h>
int main(void)
{
	HPCON console = NULL;
	COORD size = {80, 25};
	STARTUPINFOEXW startup = {0};
	PROCESS_INFORMATION process = {0};
	SIZE_T bytes = 0;
	HANDLE pipe = INVALID_HANDLE_VALUE;
	HANDLE event = NULL;
	OVERLAPPED overlapped = {0};
	unsigned char random[16];
	HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
	(void)console;
	(void)size;
	(void)BCryptGenRandom(NULL, random, sizeof(random),
	    BCRYPT_USE_SYSTEM_PREFERRED_RNG);
	pipe = CreateNamedPipeW(L"probe",
	    PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED
	    | FILE_FLAG_FIRST_PIPE_INSTANCE,
	    PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT
	    | PIPE_REJECT_REMOTE_CLIENTS, 1, 16384, 16384, 0, NULL);
	event = CreateEventW(NULL, TRUE, FALSE, NULL);
	overlapped.hEvent = event;
	(void)ConnectNamedPipe(pipe, &overlapped);
	(void)CancelIoEx(pipe, &overlapped);
	(void)InitializeProcThreadAttributeList(NULL, 1, 0, &bytes);
	(void)UpdateProcThreadAttribute(startup.lpAttributeList, 0,
	    PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, console, sizeof(console),
	    NULL, NULL);
	(void)CreateProcessW(NULL, NULL, NULL, NULL, FALSE,
	    EXTENDED_STARTUPINFO_PRESENT, NULL, NULL, &startup.StartupInfo,
	    &process);
	(void)GetProcAddress(kernel, "CreatePseudoConsole");
	return (int)PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE;
}
]=])
	syncterm_check_c17_source("${_st_conpty_probe}"
		ST_CONNECTION_CONPTY_PROGRAMMING_ENVIRONMENT)
	set(ST_CONNECTION_HAVE_CONPTY
		${ST_CONNECTION_CONPTY_PROGRAMMING_ENVIRONMENT})
	if(NOT ST_CONNECTION_HAVE_CONPTY)
		# Some SDKs hide the attribute constant behind the RS5 namespace.
		# ConPTY functions are still resolved dynamically at runtime.
		set(_st_conpty_definitions _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A000006)
		foreach(_definition IN LISTS _st_conpty_definitions)
			list(APPEND CMAKE_REQUIRED_DEFINITIONS "-D${_definition}")
		endforeach()
		syncterm_check_c17_source("${_st_conpty_probe}"
			ST_CONNECTION_CONPTY_EXTENDED_ENVIRONMENT)
		set(ST_CONNECTION_HAVE_CONPTY ${ST_CONNECTION_CONPTY_EXTENDED_ENVIRONMENT})
		if(ST_CONNECTION_HAVE_CONPTY)
			set_source_files_properties(connection_shell_windows.c PROPERTIES
				COMPILE_DEFINITIONS "${_st_conpty_definitions}")
		endif()
	endif()
	if(ST_CONNECTION_HAVE_CONPTY)
		set(ST_CONNECTION_SHELL_SOURCE connection_shell_windows.c)
		list(APPEND ST_CONNECTION_PLATFORM_LIBRARIES bcrypt)
	else()
		set(ST_CONNECTION_SHELL_SOURCE connection_shell_stub.c)
	endif()
else()
	if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
		list(APPEND ST_CONNECTION_DEFINITIONS _GNU_SOURCE)
		list(APPEND CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
	endif()
	syncterm_check_c17_source([=[
#include <termios.h>
#define TTYDEFCHARS
#include <sys/ttydefaults.h>
_Static_assert(sizeof(ttydefchars) ==
    sizeof(((struct termios *)0)->c_cc), "control character count");
int main(void)
{
	struct termios attributes = {0};
	attributes.c_iflag = TTYDEF_IFLAG;
	attributes.c_oflag = TTYDEF_OFLAG;
	attributes.c_lflag = TTYDEF_LFLAG;
	attributes.c_cflag = TTYDEF_CFLAG;
	attributes.c_cc[0] = ttydefchars[0];
	return TTYDEF_SPEED == 0;
}
]=] ST_CONNECTION_TTYDEFAULTS_PROGRAMMING_ENVIRONMENT)
	set(ST_CONNECTION_HAVE_TTYDEFAULTS
		${ST_CONNECTION_TTYDEFAULTS_PROGRAMMING_ENVIRONMENT})
	syncterm_check_c17_source([=[
#include <termios.h>
int main(void) { tcflag_t flag = IXANY; return flag == 0; }
]=] ST_CONNECTION_IXANY_PROGRAMMING_ENVIRONMENT)
	set(ST_CONNECTION_HAVE_IXANY
		${ST_CONNECTION_IXANY_PROGRAMMING_ENVIRONMENT})
	syncterm_check_c17_source([=[
#include <termios.h>
int main(void) { return VERASE2; }
]=] ST_CONNECTION_VERASE2_PROGRAMMING_ENVIRONMENT)
	set(ST_CONNECTION_HAVE_VERASE2
		${ST_CONNECTION_VERASE2_PROGRAMMING_ENVIRONMENT})
	syncterm_check_c17_source([=[
#include <termios.h>
int main(void)
{
	struct termios attributes = {0};
	return cfsetispeed(&attributes, B115200)
	    || cfsetospeed(&attributes, B115200);
}
]=] ST_CONNECTION_B115200_PROGRAMMING_ENVIRONMENT)
	set(ST_CONNECTION_HAVE_B115200
		${ST_CONNECTION_B115200_PROGRAMMING_ENVIRONMENT})
	foreach(_library IN ITEMS "" util)
		set(CMAKE_REQUIRED_LIBRARIES ${_st_connection_saved_required_libraries}
			${_library})
		foreach(_header IN ITEMS pty.h util.h libutil.h)
			string(REPLACE "." "_" _header_key ${_header})
			string(TOUPPER ${_header_key} _header_key)
			syncterm_check_c17_source("#include <${_header}>\n#include <fcntl.h>\n#include <poll.h>\n#include <signal.h>\n#include <sys/ioctl.h>\n#include <sys/wait.h>\n#include <termios.h>\n#include <unistd.h>\nextern char **environ;\nint main(void) { int master; int status; struct winsize size = {0}; struct termios attributes; struct pollfd item = {0}; pid_t child = forkpty(&master, 0, 0, &size); (void)poll(&item, 1, 0); (void)tcgetattr(master, &attributes); (void)tcsetattr(master, TCSANOW, &attributes); (void)fcntl(master, F_GETFD); (void)ioctl(master, TIOCSWINSZ, &size); (void)kill(child, SIGHUP); (void)waitpid(child, &status, WNOHANG); (void)execle(\"/bin/sh\", \"sh\", (char *)0, environ); (void)read(master, &status, sizeof(status)); (void)write(master, &status, sizeof(status)); return close(master); }"
				ST_CONNECTION_FORKPTY_COMPLETE_${_header_key}_${_library})
			if(ST_CONNECTION_FORKPTY_COMPLETE_${_header_key}_${_library})
				set(ST_CONNECTION_HAVE_FORKPTY TRUE)
				set(ST_CONNECTION_FORKPTY_HEADER_${_header_key} TRUE)
				if(_library)
					list(APPEND ST_CONNECTION_PLATFORM_LIBRARIES ${_library})
				endif()
				break()
			endif()
		endforeach()
		if(ST_CONNECTION_HAVE_FORKPTY)
			break()
		endif()
	endforeach()
	if(ST_CONNECTION_HAVE_FORKPTY)
		set(ST_CONNECTION_SHELL_SOURCE connection_shell_posix.c)
	else()
		set(ST_CONNECTION_SHELL_SOURCE connection_shell_stub.c)
	endif()
endif()

set(CMAKE_REQUIRED_LIBRARIES ${_st_connection_saved_required_libraries})
set(CMAKE_REQUIRED_DEFINITIONS ${_st_connection_saved_required_definitions})
configure_file(st_connection_config.h.in st_connection_config.h @ONLY)

add_library(st_connection STATIC
	connection.c connection_network.c connection_serial.c
	connection_stream.c connection_tls.c connection_telnet.c connection_ghost.c connection_mqtt.c
	${ST_CONNECTION_SSH_SOURCE}
	${ST_CONNECTION_SHELL_SOURCE})
if(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND ST_CONNECTION_HAVE_CONPTY)
	set_source_files_properties(connection_shell_windows.c PROPERTIES
		COMPILE_OPTIONS "-Wno-pedantic;-Wno-cast-function-type")
endif()
target_include_directories(st_connection
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
		${CMAKE_CURRENT_SOURCE_DIR}/../io_error
		${CMAKE_CURRENT_SOURCE_DIR}/../profile
		${CMAKE_CURRENT_SOURCE_DIR}/..
		${PROJECT_BINARY_DIR}
	PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(st_connection
	PUBLIC syncterm_io_error st_profile st_sftp_client
	PRIVATE syncterm_socket syncterm_tcp_client st_serial
		syncterm_threads st_settings st_time
		st_tls xptls_crypto syncterm_string_compat
	PRIVATE ${ST_CONNECTION_PLATFORM_LIBRARIES})
if(ST_DEUCESSH_TARGET)
	target_link_libraries(st_connection PRIVATE ${ST_DEUCESSH_TARGET})
endif()
if(WIN32)
	target_link_libraries(st_connection PRIVATE st_windows_utf)
endif()
if(ST_CONNECTION_DEFINITIONS)
	target_compile_definitions(st_connection PRIVATE ${ST_CONNECTION_DEFINITIONS})
endif()
syncterm_target_atomics(st_connection)
set_target_properties(st_connection PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)

function(st_connection_warnings target)
	if(MSVC)
		# Use the portable C runtime APIs, not Microsoft-only _s replacements.
		target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(${target} PRIVATE /W4 /WX)
	else()
		target_compile_options(${target} PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
endfunction()
st_connection_warnings(st_connection)
if(MSVC)
	# SSH's queued log records use a standard C flexible array member.
	target_compile_options(st_connection PRIVATE /wd4200)
endif()

if(BUILD_TESTING)
	if(NOT WIN32)
		if(ST_DEUCESSH_TARGET)
			if(NOT TARGET st_base64)
				add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../base64" "${CMAKE_CURRENT_BINARY_DIR}/base64")
			endif()
			add_executable(st_connection_ssh_fixture tests/st_connection_ssh_fixture.c
				tests/st_connection_sftp_fixture.c)
			target_link_libraries(st_connection_ssh_fixture PRIVATE
				${ST_DEUCESSH_TARGET} syncterm_threads st_base64)
			set_target_properties(st_connection_ssh_fixture PROPERTIES
				C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
			st_connection_warnings(st_connection_ssh_fixture)
			add_executable(st_connection_ssh_test tests/st_connection_ssh_test.c)
			target_link_libraries(st_connection_ssh_test PRIVATE st_connection syncterm_string_compat syncterm_threads)
			syncterm_target_atomics(st_connection_ssh_test)
			set_target_properties(st_connection_ssh_test PROPERTIES
				C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
			st_connection_warnings(st_connection_ssh_test)
			if(ST_CONNECTION_DEFINITIONS)
				target_compile_definitions(st_connection_ssh_fixture PRIVATE ${ST_CONNECTION_DEFINITIONS})
				target_compile_definitions(st_connection_ssh_test PRIVATE ${ST_CONNECTION_DEFINITIONS})
			endif()
			add_dependencies(st_connection_ssh_test st_connection_ssh_fixture)
			add_test(NAME connection_ssh COMMAND st_connection_ssh_test
				$<TARGET_FILE:st_connection_ssh_fixture>)
			set_tests_properties(connection_ssh PROPERTIES TIMEOUT 180)
		endif()
		find_package(OpenSSL 3.0 QUIET)
		if(OpenSSL_FOUND)
			add_executable(st_connection_protocol_test tests/st_connection_protocol_test.c)
			target_link_libraries(st_connection_protocol_test PRIVATE st_connection
				syncterm_socket syncterm_threads OpenSSL::SSL)
			target_compile_definitions(st_connection_protocol_test PRIVATE
				ST_CONNECTION_TEST_HAVE_TLS=$<BOOL:${ST_CONNECTION_HAVE_TLS}>)
			syncterm_target_atomics(st_connection_protocol_test)
			set_target_properties(st_connection_protocol_test PROPERTIES
				C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
			st_connection_warnings(st_connection_protocol_test)
			add_test(NAME connection_protocols COMMAND st_connection_protocol_test)
			set_tests_properties(connection_protocols PROPERTIES TIMEOUT 120)
			if(ST_CONNECTION_HAVE_TLS)
				add_executable(st_connection_tls_prompt_test tests/st_connection_tls_prompt_test.c)
				target_link_libraries(st_connection_tls_prompt_test PRIVATE st_connection
					st_settings syncterm_string_compat syncterm_threads OpenSSL::SSL)
				set_target_properties(st_connection_tls_prompt_test PROPERTIES
					C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
				st_connection_warnings(st_connection_tls_prompt_test)
				add_test(NAME connection_tls_prompts COMMAND st_connection_tls_prompt_test)
				set_tests_properties(connection_tls_prompts PROPERTIES TIMEOUT 120)
			endif()
		endif()
	endif()
	if(ST_CONNECTION_HAVE_FORKPTY OR ST_CONNECTION_HAVE_CONPTY)
		set(ST_CONNECTION_TEST_HAVE_SHELL TRUE)
	else()
		set(ST_CONNECTION_TEST_HAVE_SHELL FALSE)
	endif()
	add_executable(st_connection_test tests/st_connection_test.c)
	target_link_libraries(st_connection_test PRIVATE
		st_connection syncterm_socket syncterm_threads)
	syncterm_target_atomics(st_connection_test)
	set_target_properties(st_connection_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	st_connection_warnings(st_connection_test)
	target_compile_definitions(st_connection_test PRIVATE
		ST_CONNECTION_TEST_HAVE_SHELL=$<BOOL:${ST_CONNECTION_TEST_HAVE_SHELL}>)
	if(ST_CONNECTION_DEFINITIONS)
		target_compile_definitions(st_connection_test PRIVATE
			${ST_CONNECTION_DEFINITIONS})
	endif()
	if(ST_CONNECTION_HAVE_FORKPTY)
		add_executable(st_connection_shell_fixture
			tests/st_connection_shell_fixture.c)
		target_include_directories(st_connection_shell_fixture PRIVATE
			${CMAKE_CURRENT_BINARY_DIR})
		set_target_properties(st_connection_shell_fixture PROPERTIES
			C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
		st_connection_warnings(st_connection_shell_fixture)
		if(ST_CONNECTION_DEFINITIONS)
			target_compile_definitions(st_connection_shell_fixture PRIVATE
				${ST_CONNECTION_DEFINITIONS})
		endif()
		add_dependencies(st_connection_test st_connection_shell_fixture)
		add_test(NAME connection COMMAND st_connection_test
			./$<TARGET_FILE_NAME:st_connection_shell_fixture>)
		set_tests_properties(connection PROPERTIES WORKING_DIRECTORY
			$<TARGET_FILE_DIR:st_connection_shell_fixture>)
	else()
		add_test(NAME connection COMMAND st_connection_test)
	endif()
	set_tests_properties(connection PROPERTIES TIMEOUT 15)
	add_test(NAME connection_partial_input
		COMMAND st_connection_test --partial-input)
	add_test(NAME connection_auxiliary_close
		COMMAND st_connection_test --auxiliary-close)
	set_tests_properties(connection_partial_input PROPERTIES TIMEOUT 15)
	if(WIN32 AND ST_CONNECTION_HAVE_CONPTY)
		add_test(NAME connection_shell_shutdown
			COMMAND st_connection_test --shell-shutdown)
		set_tests_properties(connection_shell_shutdown PROPERTIES TIMEOUT 30)
	endif()

	add_executable(st_connection_serial_test
		connection.c connection_serial.c
		tests/st_connection_serial_fake.c
		tests/st_connection_serial_test.c)
	target_include_directories(st_connection_serial_test PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../serial
		${CMAKE_CURRENT_SOURCE_DIR}/../io_error
		${CMAKE_CURRENT_SOURCE_DIR}/../threads
		${CMAKE_CURRENT_SOURCE_DIR}/../profile
		${CMAKE_CURRENT_SOURCE_DIR}/../settings
		${CMAKE_CURRENT_SOURCE_DIR}/../name_value_list
		${CMAKE_CURRENT_SOURCE_DIR}/../array_list
		${CMAKE_CURRENT_SOURCE_DIR}/..
		${PROJECT_BINARY_DIR} ${CMAKE_CURRENT_BINARY_DIR})
	target_link_libraries(st_connection_serial_test PRIVATE
		syncterm_io_error syncterm_string_compat syncterm_threads
		st_profile st_settings st_time)
	syncterm_target_atomics(st_connection_serial_test)
	set_target_properties(st_connection_serial_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	st_connection_warnings(st_connection_serial_test)
	add_test(NAME connection_serial COMMAND st_connection_serial_test)
	set_tests_properties(connection_serial PROPERTIES TIMEOUT 15)
endif()

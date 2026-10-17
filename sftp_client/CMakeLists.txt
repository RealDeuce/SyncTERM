# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/SynctermAtomics.cmake")

if(NOT TARGET syncterm_threads)
	add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../threads" "${CMAKE_CURRENT_BINARY_DIR}/threads")
endif()
add_library(st_sftp_client STATIC protocol.c client.c)
target_include_directories(st_sftp_client PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
	"${CMAKE_CURRENT_SOURCE_DIR}/.." "${CMAKE_BINARY_DIR}")
target_link_libraries(st_sftp_client PUBLIC syncterm_io_error
	PRIVATE syncterm_threads st_time syncterm_string_compat)
syncterm_target_atomics(st_sftp_client)
set_target_properties(st_sftp_client PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(MSVC)
	# The packet buffer uses a standard C flexible array member.
	target_compile_options(st_sftp_client PRIVATE /W4 /WX /wd4200)
else()
	target_compile_options(st_sftp_client PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(BUILD_TESTING)
	add_executable(st_sftp_protocol_test tests/protocol_test.c)
	target_link_libraries(st_sftp_protocol_test PRIVATE st_sftp_client)
	set_target_properties(st_sftp_protocol_test PROPERTIES C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_sftp_protocol_test PRIVATE /W4 /WX)
	else()
		target_compile_options(st_sftp_protocol_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME sftp_protocol COMMAND st_sftp_protocol_test)
endif()

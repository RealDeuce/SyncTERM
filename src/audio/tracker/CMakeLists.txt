# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermDependencies.cmake")
syncterm_dependency(NAME OPENMPT PACKAGE libopenmpt HEADER libopenmpt/libopenmpt.h
	LIBRARIES openmpt DEFAULT SHARED REQUIRED LINK_REQUIRED CXX PROBE [=[
#include <libopenmpt/libopenmpt.h>
#include <libopenmpt/libopenmpt_stream_callbacks_file.h>
int main(void) {
	openmpt_module *mod = openmpt_module_create2(openmpt_stream_get_file_callbacks2(), 0, openmpt_log_func_silent, 0, 0, 0, 0, 0, 0);
	if (mod) {
		openmpt_module_set_position_order_row(mod, 0, 0);
		openmpt_module_read_interleaved_stereo(mod, 44100, 0, 0);
		openmpt_module_destroy(mod);
	}
	return 0;
}
]=])
add_library(st_tracker STATIC st_tracker.c)
syncterm_target_atomics(st_tracker)
target_include_directories(st_tracker PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_tracker PUBLIC st_audio PRIVATE ${ST_DEP_OPENMPT_TARGET})
set_target_properties(st_tracker PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)

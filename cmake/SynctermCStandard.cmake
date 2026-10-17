# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

# CMake populates this capability list when it identifies the C compiler.
# Older macOS x86 toolchains implement C11 but lack a C17 language mode.
set(SYNCTERM_C_STANDARD 17)
if(NOT "c_std_17" IN_LIST CMAKE_C_COMPILE_FEATURES)
	if(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|i[3-6]86)$"
	    AND "c_std_11" IN_LIST CMAKE_C_COMPILE_FEATURES)
		set(SYNCTERM_C_STANDARD 11)
		get_property(_st_c11_notice GLOBAL PROPERTY SYNCTERM_C11_NOTICE)
		if(NOT _st_c11_notice)
			message(STATUS "C17 mode unavailable on macOS x86; using C11")
			set_property(GLOBAL PROPERTY SYNCTERM_C11_NOTICE TRUE)
		endif()
	else()
		message(FATAL_ERROR "The selected C compiler does not support C17")
	endif()
endif()
set(SYNCTERM_C_STANDARD "${SYNCTERM_C_STANDARD}" CACHE INTERNAL
	"C dialect selected from compiler capabilities" FORCE)

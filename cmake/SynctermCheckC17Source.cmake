# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

include_guard(GLOBAL)

include(CheckCSourceCompiles)

function(syncterm_check_c17_source source result)
	set(CMAKE_C_STANDARD 17)
	set(CMAKE_C_STANDARD_REQUIRED ON)
	set(CMAKE_C_EXTENSIONS OFF)
	check_c_source_compiles("${source}" ${result} ${ARGN})
	set(${result} "${${result}}" PARENT_SCOPE)
endfunction()

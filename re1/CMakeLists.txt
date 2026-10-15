# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

add_library(st_re1 STATIC compile.c pike.c sub.c y.tab.c)
set_target_properties(st_re1 PROPERTIES
	C_STANDARD 17
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS NO)
target_include_directories(st_re1 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
add_library(st_ddofx STATIC st_ddofx.c)
target_include_directories(st_ddofx PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(st_ddofx PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(MSVC)
	target_compile_options(st_ddofx PRIVATE /W4)
else()
	target_compile_options(st_ddofx PRIVATE -Wall -Wextra -Wpedantic)
endif()

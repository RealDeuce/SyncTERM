# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
add_library(st_bgi STATIC st_bgi_data.c)
target_include_directories(st_bgi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(st_bgi PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(MSVC)
	target_compile_options(st_bgi PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
else()
	target_compile_options(st_bgi PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
endif()

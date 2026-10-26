# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(STOOII LANGUAGES C)
	include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCStandard.cmake")
	include(CTest)
endif()
foreach(component io_error string_compat value_convert)
	if(NOT TARGET syncterm_${component})
		add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../base/${component}"
			"${CMAKE_CURRENT_BINARY_DIR}/${component}")
	endif()
endforeach()
add_library(st_ooii STATIC st_ooii.c ooii_bmenus.c ooii_cmenus.c
	ooii_logons.c ooii_sounds.c)
target_include_directories(st_ooii PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_ooii PUBLIC syncterm_io_error
	PRIVATE syncterm_string_compat syncterm_value_convert)
set_target_properties(st_ooii PROPERTIES C_STANDARD ${SYNCTERM_C_STANDARD}
	C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(MSVC)
	target_compile_options(st_ooii PRIVATE /W4 /WX)
else()
	target_compile_options(st_ooii PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
if(BUILD_TESTING)
	add_executable(st_ooii_test tests/st_ooii_test.c)
	target_link_libraries(st_ooii_test PRIVATE st_ooii)
	set_target_properties(st_ooii_test PROPERTIES C_STANDARD ${SYNCTERM_C_STANDARD}
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_ooii_test PRIVATE /W4 /WX)
	else()
		target_compile_options(st_ooii_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME ooii COMMAND st_ooii_test)
endif()

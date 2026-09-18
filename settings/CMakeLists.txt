# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(STSettings LANGUAGES C)
	include(CTest)
endif()

foreach(_dependency IN ITEMS string_compat string_util name_value_list)
	if(NOT TARGET syncterm_${_dependency})
		add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../${_dependency}"
			"${CMAKE_CURRENT_BINARY_DIR}/${_dependency}")
	endif()
endforeach()

foreach(_dependency IN ITEMS app_paths file_lock)
	if(NOT TARGET st_${_dependency})
		add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../${_dependency}"
			"${CMAKE_CURRENT_BINARY_DIR}/${_dependency}")
	endif()
endforeach()
foreach(_dependency IN ITEMS filesystem ini io_error)
	if(NOT TARGET syncterm_${_dependency})
		add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../${_dependency}"
			"${CMAKE_CURRENT_BINARY_DIR}/${_dependency}")
	endif()
endforeach()

add_library(st_settings STATIC st_settings.c st_settings_store.c)
target_include_directories(st_settings PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_settings PUBLIC
	syncterm_name_value_list st_app_paths syncterm_io_error PRIVATE
	st_file_lock syncterm_filesystem syncterm_ini syncterm_string_compat
	syncterm_string_util)
set_target_properties(st_settings PROPERTIES
	C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)

function(st_settings_warnings target)
	if(MSVC)
		target_compile_options(${target} PRIVATE /W4 /WX)
	else()
		target_compile_options(${target} PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
endfunction()
st_settings_warnings(st_settings)

if(BUILD_TESTING)
	add_executable(st_settings_test tests/st_settings_test.c)
	target_link_libraries(st_settings_test PRIVATE
		st_settings syncterm_string_compat)
	set_target_properties(st_settings_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	st_settings_warnings(st_settings_test)
	add_test(NAME settings COMMAND st_settings_test)

	add_executable(st_settings_store_test tests/st_settings_store_test.c)
	target_link_libraries(st_settings_store_test PRIVATE st_settings
		syncterm_filesystem syncterm_ini)
	target_compile_definitions(st_settings_store_test PRIVATE
		ST_SETTINGS_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/store-test")
	set_target_properties(st_settings_store_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	st_settings_warnings(st_settings_store_test)
	add_test(NAME settings_store COMMAND st_settings_store_test)
endif()

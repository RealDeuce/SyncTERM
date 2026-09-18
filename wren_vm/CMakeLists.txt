# Wren virtual machine.

# Wren is distributed under the MIT license in LICENSE.

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(STWrenVM LANGUAGES C)
endif()

set(_st_wren_vm_sources
	vm/wren_compiler.c
	vm/wren_core.c
	vm/wren_debug.c
	vm/wren_primitive.c
	vm/wren_utils.c
	vm/wren_value.c
	vm/wren_vm.c
	optional/wren_opt_meta.c
	optional/wren_opt_random.c)

add_library(st_wren_vm STATIC ${_st_wren_vm_sources})
target_compile_definitions(st_wren_vm PRIVATE WREN_COMPUTED_GOTO=0)
target_include_directories(st_wren_vm
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vm
		${CMAKE_CURRENT_SOURCE_DIR}/optional)
set_target_properties(st_wren_vm PROPERTIES
	C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)

if(MSVC)
	target_compile_options(st_wren_vm PRIVATE /W4 /WX /wd4100)
else()
	target_compile_options(st_wren_vm PRIVATE
		-Wall -Wextra -Wpedantic -Werror -Wno-unused-parameter
		-Wno-overlength-strings)
endif()

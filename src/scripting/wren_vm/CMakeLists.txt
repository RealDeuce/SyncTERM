# Wren virtual machine.

# Wren is distributed under the MIT license in LICENSE.

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(STWrenVM LANGUAGES C)
	include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCStandard.cmake")
	include(CTest)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCheckC17Source.cmake")

set(_st_wren_vm_math_source [=[
#include <math.h>
int main(int argc, char **argv)
{
	volatile double input = (double)argc;
	volatile double output = 0.0;
	output += acos(input);
	output += asin(input);
	output += atan(input);
	output += atan2(input, input);
	output += ceil(input);
	output += cos(input);
	output += floor(input);
	output += fmod(input, 2.0);
	output += log(input);
	output += log2(input);
	output += pow(input, 2.0);
	output += sin(input);
	output += sqrt(input);
	output += tan(input);
	output += trunc(input);
	return isnan(output) && argv == 0;
}
]=])
set(_st_wren_vm_saved_required_libraries ${CMAKE_REQUIRED_LIBRARIES})
set(CMAKE_REQUIRED_LIBRARIES)
syncterm_check_c17_source("${_st_wren_vm_math_source}"
	ST_WREN_VM_HAVE_MATH_WITHOUT_LIBRARY)
set(_st_wren_vm_math_libraries)
if(NOT ST_WREN_VM_HAVE_MATH_WITHOUT_LIBRARY)
	set(CMAKE_REQUIRED_LIBRARIES m)
	syncterm_check_c17_source("${_st_wren_vm_math_source}"
		ST_WREN_VM_HAVE_MATH_WITH_M)
	if(NOT ST_WREN_VM_HAVE_MATH_WITH_M)
		message(FATAL_ERROR
			"The target C environment does not provide the math functions required by Wren")
	endif()
	set(_st_wren_vm_math_libraries m)
endif()
set(CMAKE_REQUIRED_LIBRARIES ${_st_wren_vm_saved_required_libraries})

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
if(_st_wren_vm_math_libraries)
	target_link_libraries(st_wren_vm PRIVATE ${_st_wren_vm_math_libraries})
endif()
set_target_properties(st_wren_vm PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)

if(MSVC)
	# Use the portable C runtime APIs, not Microsoft-only _s replacements.
	target_compile_definitions(st_wren_vm PRIVATE _CRT_SECURE_NO_WARNINGS)
	target_compile_options(st_wren_vm PRIVATE /W4 $<$<CONFIG:Debug>:/WX> /wd4100)
	# Flexible array members and READ_SHORT's sequenced comma expression
	# are intentional C idioms in the upstream VM, not language extensions.
	target_compile_options(st_wren_vm PRIVATE /wd4200 /wd4709)
else()
	target_compile_options(st_wren_vm PRIVATE
		-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror> -Wno-unused-parameter
		-Wno-overlength-strings)
endif()

if(BUILD_TESTING)
	add_executable(st_wren_vm_link_test tests/st_wren_vm_link_test.c)
	target_link_libraries(st_wren_vm_link_test PRIVATE st_wren_vm)
	set_target_properties(st_wren_vm_link_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_wren_vm_link_test PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_wren_vm_link_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_wren_vm_link_test PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME st_wren_vm_link COMMAND st_wren_vm_link_test)
	add_executable(st_wren_vm_foreign_stack_test tests/st_wren_vm_foreign_stack_test.c)
	target_link_libraries(st_wren_vm_foreign_stack_test PRIVATE st_wren_vm)
	set_target_properties(st_wren_vm_foreign_stack_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_wren_vm_foreign_stack_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_wren_vm_foreign_stack_test PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME st_wren_vm_foreign_stack COMMAND st_wren_vm_foreign_stack_test)

	add_executable(st_wren_vm_scoped_fiber_test tests/st_wren_vm_scoped_fiber_test.c)
	target_link_libraries(st_wren_vm_scoped_fiber_test PRIVATE st_wren_vm)
	set_target_properties(st_wren_vm_scoped_fiber_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_wren_vm_scoped_fiber_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_wren_vm_scoped_fiber_test PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME st_wren_vm_scoped_fiber COMMAND st_wren_vm_scoped_fiber_test)

	add_executable(st_wren_vm_unicode_string_test tests/st_wren_vm_unicode_string_test.c)
	target_link_libraries(st_wren_vm_unicode_string_test PRIVATE st_wren_vm)
	set_target_properties(st_wren_vm_unicode_string_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_wren_vm_unicode_string_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_wren_vm_unicode_string_test PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME st_wren_vm_unicode_string COMMAND st_wren_vm_unicode_string_test)

endif()

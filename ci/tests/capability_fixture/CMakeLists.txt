# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)
project(CapabilityFixture LANGUAGES C)
if(CAPABILITY_CASE STREQUAL c11)
	# Simulate the compiler capability result, then compile in the selected mode.
	set(APPLE TRUE)
	set(CMAKE_SYSTEM_PROCESSOR x86_64)
	list(REMOVE_ITEM CMAKE_C_COMPILE_FEATURES c_std_17)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCStandard.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermAtomics.cmake")
if(CAPABILITY_CASE MATCHES "^atomics_" AND NOT CAPABILITY_CASE STREQUAL atomics_vs_sources)
	# Exercise the opt-in and failure paths without requiring an MSVC host.
	set(MSVC TRUE)
	function(syncterm_check_c17_source source result)
		if(NOT CMAKE_REQUIRED_FLAGS MATCHES "/U__STDC_NO_ATOMICS__")
			message(FATAL_ERROR "Missing atomics macro undefinition in probe")
		endif()
		set(_available FALSE)
		if(result STREQUAL SYNCTERM_HAVE_C_ATOMICS
		    AND CAPABILITY_CASE STREQUAL atomics_msvc_native)
			set(_available TRUE)
		elseif(result STREQUAL SYNCTERM_HAVE_ENABLED_C_ATOMICS)
			if(NOT CMAKE_REQUIRED_FLAGS MATCHES "/experimental:c11atomics")
				message(FATAL_ERROR "Missing atomics switch in retry probe")
			endif()
			if(CAPABILITY_CASE STREQUAL atomics_switch)
				set(_available TRUE)
			endif()
		endif()
		set(${result} ${_available} PARENT_SCOPE)
	endfunction()
endif()
add_executable(capability main.c)
set_target_properties(capability PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
target_compile_definitions(capability PRIVATE EXPECTED_STANDARD=${SYNCTERM_C_STANDARD})
if(CAPABILITY_CASE STREQUAL atomics_vs_sources)
	enable_language(CXX)
	file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/probe.c" "int probe(void) { return 0; }\n")
	add_library(atomics_probe STATIC "${CMAKE_CURRENT_BINARY_DIR}/probe.c")
	syncterm_target_atomics(atomics_probe)
	get_property(_native_options TARGET syncterm_c_atomics PROPERTY INTERFACE_COMPILE_OPTIONS)
	# A portable marker proves the source-level path independently of the
	# target-wide interface, which Visual Studio evaluates as CXX here.
	set_property(TARGET syncterm_c_atomics PROPERTY INTERFACE_COMPILE_OPTIONS
		${_native_options} "$<$<COMPILE_LANGUAGE:C>:-DATOMICS_C_OPTION=1>")
	file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/marker.c" [=[
#if EXPECT_ATOMICS
#ifndef ATOMICS_C_OPTION
#error C atomics option was lost
#endif
#else
#ifdef ATOMICS_C_OPTION
#error C atomics option leaked into an unrelated target
#endif
#endif
int marker(void) { return 0; }
]=])
	file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/late.c"
		"#ifndef ATOMICS_C_OPTION\n#error Later C source lost atomics option\n#endif\nint late(void) { return 0; }\n")
	file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/cpp.cpp"
		"#ifdef ATOMICS_C_OPTION\n#error C atomics option leaked into C++\n#endif\nint cpp() { return 0; }\n")
	add_library(cpp_dependency STATIC "${CMAKE_CURRENT_BINARY_DIR}/cpp.cpp")
	add_library(c_linked STATIC "${CMAKE_CURRENT_BINARY_DIR}/marker.c")
	target_link_libraries(c_linked PRIVATE cpp_dependency)
	set_target_properties(c_linked PROPERTIES LINKER_LANGUAGE CXX)
	add_library(mixed STATIC "${CMAKE_CURRENT_BINARY_DIR}/marker.c" "${CMAKE_CURRENT_BINARY_DIR}/cpp.cpp")
	add_library(unrelated STATIC "${CMAKE_CURRENT_BINARY_DIR}/marker.c")
	target_compile_definitions(unrelated PRIVATE EXPECT_ATOMICS=0)
	set(_native_generator "${CMAKE_GENERATOR}")
	set(CMAKE_GENERATOR "Visual Studio atomics fixture")
	foreach(_target capability c_linked mixed)
		syncterm_target_atomics(${_target})
		target_compile_definitions(${_target} PRIVATE EXPECT_ATOMICS=1)
	endforeach()
	target_sources(c_linked PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/late.c")
	set(CMAKE_GENERATOR "${_native_generator}")
	# Force compilation to rely on the source options, modelling VS's loss
	# of C-only target options when the target links through CXX.
	set_property(TARGET syncterm_c_atomics PROPERTY INTERFACE_COMPILE_OPTIONS "")
	foreach(_target atomics_probe cpp_dependency c_linked mixed unrelated)
		set_target_properties(${_target} PROPERTIES
			C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO
			CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
	endforeach()
else()
	syncterm_target_atomics(capability)
endif()
if(CAPABILITY_CASE STREQUAL atomics_switch
    OR CAPABILITY_CASE STREQUAL atomics_msvc_native)
	set(_expected_options "$<$<COMPILE_LANGUAGE:C>:/U__STDC_NO_ATOMICS__>")
	if(CAPABILITY_CASE STREQUAL atomics_switch)
		list(APPEND _expected_options "$<$<COMPILE_LANGUAGE:C>:/experimental:c11atomics>")
	endif()
	get_target_property(_options syncterm_c_atomics INTERFACE_COMPILE_OPTIONS)
	if(NOT _options STREQUAL _expected_options)
		message(FATAL_ERROR "Atomics options not attached to the C-only interface")
	endif()
	if(CMAKE_REQUIRED_FLAGS MATCHES "/experimental:c11atomics|/U__STDC_NO_ATOMICS__")
		message(FATAL_ERROR "Atomics probe leaked its flags")
	endif()
endif()

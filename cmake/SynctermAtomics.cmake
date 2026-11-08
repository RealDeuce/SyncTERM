# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/SynctermCheckC17Source.cmake")

function(syncterm_source_atomics target)
	get_property(_options TARGET ${target} PROPERTY SYNCTERM_C_ATOMIC_OPTIONS)
	get_property(_sources TARGET ${target} PROPERTY SOURCES)
	get_property(_source_dir TARGET ${target} PROPERTY SOURCE_DIR)
	foreach(_source IN LISTS _sources)
		if(_source MATCHES "^\\$<TARGET_OBJECTS:")
			# Object-library sources receive options on their owning target.
			continue()
		endif()
		if(NOT IS_ABSOLUTE "${_source}")
			set(_source "${_source_dir}/${_source}")
		endif()
		foreach(_option IN LISTS _options)
			# Source properties are shared by targets in this directory.
			set_property(SOURCE "${_source}" TARGET_DIRECTORY ${target}
				APPEND PROPERTY COMPILE_OPTIONS
				"$<$<STREQUAL:$<TARGET_PROPERTY:NAME>,${target}>:${_option}>")
		endforeach()
	endforeach()
endfunction()

function(syncterm_target_atomics target)
	if(NOT TARGET syncterm_c_atomics)
		set(_source [=[
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
int main(void)
{
	atomic_bool flag = false;
	atomic_uint count = 0;
	atomic_uint_fast64_t clock = 0;
	_Atomic(void *) pointer = 0;
	void *expected = 0;
	atomic_init(&clock, 1);
	atomic_store_explicit(&flag, true, memory_order_release);
	(void)atomic_fetch_add_explicit(&count, 1, memory_order_acq_rel);
	(void)atomic_fetch_or(&clock, UINT64_C(2));
	(void)atomic_compare_exchange_strong(&pointer, &expected, &count);
	(void)atomic_exchange(&pointer, 0);
	return atomic_load_explicit(&flag, memory_order_acquire) ? 0 : 1;
}
]=])
		set(_options)
		if(MSVC)
			# MSVC still defines this macro for its experimental built-in atomics.
			set(_options /U__STDC_NO_ATOMICS__)
			string(APPEND CMAKE_REQUIRED_FLAGS " /U__STDC_NO_ATOMICS__")
		endif()
		syncterm_check_c17_source("${_source}" SYNCTERM_HAVE_C_ATOMICS)
		set(_available ${SYNCTERM_HAVE_C_ATOMICS})
		if(NOT SYNCTERM_HAVE_C_ATOMICS AND MSVC)
			# MSVC currently requires an explicit switch for its C atomics.
			set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} /experimental:c11atomics")
			syncterm_check_c17_source("${_source}" SYNCTERM_HAVE_ENABLED_C_ATOMICS)
			if(SYNCTERM_HAVE_ENABLED_C_ATOMICS)
				list(APPEND _options /experimental:c11atomics)
				set(_available TRUE)
			endif()
		endif()
		if(NOT _available)
			message(FATAL_ERROR "The selected C environment does not support the required atomic operations")
		endif()
		add_library(syncterm_c_atomics INTERFACE)
		foreach(_option IN LISTS _options)
			target_compile_options(syncterm_c_atomics INTERFACE
				"$<$<COMPILE_LANGUAGE:C>:${_option}>")
		endforeach()
	endif()
	target_link_libraries(${target} PRIVATE syncterm_c_atomics)
	if(CMAKE_GENERATOR MATCHES "^Visual Studio ")
		# VS evaluates target-wide C-only options using the linker language,
		# which can be CXX even for a C target with C++ dependencies. Source
		# options use the actual compilation language. Defer until all later
		# target_sources() calls in this directory have completed.
		get_property(_options TARGET syncterm_c_atomics PROPERTY INTERFACE_COMPILE_OPTIONS)
		if(_options)
			set_property(TARGET ${target} PROPERTY SYNCTERM_C_ATOMIC_OPTIONS "${_options}")
			cmake_language(EVAL CODE
				"cmake_language(DEFER CALL syncterm_source_atomics [==[${target}]==])")
		endif()
	endif()
endfunction()

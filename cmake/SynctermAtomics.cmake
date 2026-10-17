# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/SynctermCheckC17Source.cmake")

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
		syncterm_check_c17_source("${_source}" SYNCTERM_HAVE_C_ATOMICS)
		set(_options)
		if(NOT SYNCTERM_HAVE_C_ATOMICS AND MSVC)
			# MSVC currently requires an explicit switch for its C atomics.
			set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} /experimental:c11atomics")
			syncterm_check_c17_source("${_source}" SYNCTERM_HAVE_ENABLED_C_ATOMICS)
			if(SYNCTERM_HAVE_ENABLED_C_ATOMICS)
				set(_options /experimental:c11atomics)
			endif()
		endif()
		if(NOT SYNCTERM_HAVE_C_ATOMICS AND NOT _options)
			message(FATAL_ERROR "The selected C environment does not support the required atomic operations")
		endif()
		add_library(syncterm_c_atomics INTERFACE)
		if(_options)
			target_compile_options(syncterm_c_atomics INTERFACE
				"$<$<COMPILE_LANGUAGE:C>:${_options}>")
		endif()
	endif()
	target_link_libraries(${target} PRIVATE syncterm_c_atomics)
endfunction()

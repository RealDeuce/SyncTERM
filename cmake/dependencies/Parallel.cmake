# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause

# A recursive Make build can share the caller's budget. Other tools need an
# explicit limit: their automatic CPU counts would multiply outer parallelism.
function(syncterm_dependency_parallel output backend)
	set(shared FALSE)
	if(dep_job_server AND "$ENV{MAKEFLAGS}" MATCHES "--jobserver-(auth|fds)=")
		if(backend STREQUAL "make" OR (backend STREQUAL "cmake" AND CMAKE_GENERATOR MATCHES "Makefiles"))
			set(shared TRUE)
		elseif(backend STREQUAL "ninja" OR backend STREQUAL "meson")
			# Ninja 1.13 added the FIFO/Windows semaphore jobserver protocol;
			# earlier versions and POSIX pipe jobservers need the fallback.
			if("$ENV{MAKEFLAGS}" MATCHES "--jobserver-auth=([^ ]+)")
				set(auth "${CMAKE_MATCH_1}")
				if(CMAKE_HOST_WIN32 OR auth MATCHES "^fifo:")
					set(parallel_ninja "${ARGV2}")
					# Meson's NINJA override may be a launcher with arguments;
					# use the fallback unless we can identify its actual tool.
					if(backend STREQUAL "meson" AND "$ENV{NINJA}" STREQUAL "")
						find_program(parallel_ninja_path NAMES ninja ninja-build
							PATHS ENV PATH NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
						set(parallel_ninja "${parallel_ninja_path}")
					endif()
					if(parallel_ninja)
						execute_process(COMMAND "${parallel_ninja}" --version
							OUTPUT_VARIABLE version OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE result)
						if(result EQUAL 0 AND version VERSION_GREATER_EQUAL 1.13)
							set(shared TRUE)
						endif()
					endif()
				endif()
			endif()
		endif()
	endif()
	set(args)
	if(shared)
		# cmake --build otherwise turns this environment value into a fresh
		# -j limit, disconnecting the child from the inherited jobserver.
		if(backend STREQUAL "cmake")
			unset(ENV{CMAKE_BUILD_PARALLEL_LEVEL})
		endif()
	elseif(NOT backend STREQUAL "nmake")
		set(jobs "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}")
		if(jobs STREQUAL "")
			set(jobs 1)
		elseif(NOT jobs MATCHES "^[1-9][0-9]*$")
			message(FATAL_ERROR "CMAKE_BUILD_PARALLEL_LEVEL must be a positive integer for dependency builds")
		endif()
		if(backend STREQUAL "cmake")
			set(args --parallel "${jobs}")
		else()
			set(args "-j${jobs}")
		endif()
	endif()
	set(${output} "${args}" PARENT_SCOPE)
endfunction()

# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause
# Initial-cache settings cover C++ if a selected provider enables it, without
# producing an unused command-line-variable warning in C-only configurations.
set(CMAKE_C_FLAGS "-fsanitize=address,undefined -fno-omit-frame-pointer"
	CACHE STRING "CI sanitizer instrumentation" FORCE)
set(CMAKE_CXX_FLAGS "-fsanitize=address,undefined -fno-omit-frame-pointer"
	CACHE STRING "CI sanitizer instrumentation" FORCE)

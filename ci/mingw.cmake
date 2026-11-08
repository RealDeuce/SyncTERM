# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

set(CMAKE_SYSTEM_NAME Windows)
if(NOT DEFINED CI_MINGW_BITS)
	set(CI_MINGW_BITS "$ENV{CI_MINGW_BITS}")
endif()
if(CI_MINGW_BITS STREQUAL "32")
	set(_triple i686-w64-mingw32)
	set(CMAKE_SYSTEM_PROCESSOR i686)
elseif(CI_MINGW_BITS STREQUAL "64")
	set(_triple x86_64-w64-mingw32)
	set(CMAKE_SYSTEM_PROCESSOR x86_64)
else()
	message(FATAL_ERROR "CI_MINGW_BITS must be 32 or 64")
endif()
set(_root "$ENV{CI_MINGW_ROOT}")
if(NOT _root)
	set(_root "/home/admin/mingw-w${CI_MINGW_BITS}")
endif()
# The toolchain is also read in try_compile's generated projects.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES CI_MINGW_BITS)
set(CMAKE_C_COMPILER "${_root}/bin/${_triple}-gcc")
set(CMAKE_CXX_COMPILER "${_root}/bin/${_triple}-g++")
set(CMAKE_RC_COMPILER "${_root}/bin/${_triple}-windres")
list(APPEND CMAKE_FIND_ROOT_PATH "${_root}/${_triple}")
list(REMOVE_DUPLICATES CMAKE_FIND_ROOT_PATH)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
# pkg-config is a host program and does not honor CMake's root-path modes.
# Search target metadata only, including any additional target roots supplied
# by the caller; never let a shell runner's native packages enable backends.
set(PKG_CONFIG_USE_CMAKE_PREFIX_PATH FALSE)
set(_pkg_dirs)
foreach(_target_root IN LISTS CMAKE_FIND_ROOT_PATH)
	list(APPEND _pkg_dirs "${_target_root}/lib/pkgconfig"
		"${_target_root}/share/pkgconfig")
endforeach()
list(JOIN _pkg_dirs ":" _pkg_libdir)
set(ENV{PKG_CONFIG_LIBDIR} "${_pkg_libdir}")
set(ENV{PKG_CONFIG_PATH} "")

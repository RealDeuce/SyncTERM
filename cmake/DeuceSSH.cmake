# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

find_package(deucessh CONFIG QUIET)
if(NOT TARGET deucessh::deucessh_static AND NOT TARGET deucessh::deucessh_shared)
	include(FetchContent)
	set(DEUCESSH_BUILD_STATIC ON)
	set(DEUCESSH_BUILD_SHARED OFF)
	set(DEUCESSH_BUILD_TESTS OFF)
	set(DEUCESSH_CRYPTO_BACKEND "${ST_TLS_PROVIDER}")
	FetchContent_Declare(deucessh
		GIT_REPOSITORY https://github.com/RealDeuce/DeuceSSH.git
		GIT_TAG f85f292a94013396964b80eb6b49a0e96aa440f2)
	FetchContent_MakeAvailable(deucessh)
	set(ST_DEUCESSH_TARGET deucessh_static)
	set_target_properties(deucessh_static PROPERTIES C_EXTENSIONS NO)
elseif(TARGET deucessh::deucessh_static)
	set(ST_DEUCESSH_TARGET deucessh::deucessh_static)
else()
	set(ST_DEUCESSH_TARGET deucessh::deucessh_shared)
endif()

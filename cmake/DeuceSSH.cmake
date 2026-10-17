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
		GIT_TAG f855d75a8462146a607bf26dc5c88ef5023bf0b7)
	FetchContent_MakeAvailable(deucessh)
	set(ST_DEUCESSH_TARGET deucessh_static)
	set_target_properties(deucessh_static PROPERTIES C_EXTENSIONS NO)
elseif(TARGET deucessh::deucessh_static)
	set(ST_DEUCESSH_TARGET deucessh::deucessh_static)
else()
	set(ST_DEUCESSH_TARGET deucessh::deucessh_shared)
endif()

# DeuceSSH exposes a C ABI, but its Botan archive contains C++ objects.
# A C-only consumer (including the standalone SSH fixture) needs the runtime
# of the selected C++ compiler even without a dependency on xptls_crypto.
if(ST_TLS_PROVIDER STREQUAL "Botan")
	enable_language(CXX)
	add_library(st_deucessh INTERFACE)
	target_link_libraries(st_deucessh INTERFACE ${ST_DEUCESSH_TARGET}
		${CMAKE_CXX_IMPLICIT_LINK_LIBRARIES})
	target_link_directories(st_deucessh INTERFACE
		${CMAKE_CXX_IMPLICIT_LINK_DIRECTORIES})
	set(ST_DEUCESSH_TARGET st_deucessh)
endif()

# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

get_property(_st_crypto_dependency TARGET st_crypto PROPERTY ST_CRYPTO_DEPENDENCY)
# Installed DeuceSSH packages do not currently export provider provenance.
# Build our pinned source against the provider already selected by SyncTERM.

if(NOT TARGET deucessh::deucessh_static AND NOT TARGET deucessh::deucessh_shared)
	include(FetchContent)
	set(DEUCESSH_BUILD_STATIC ON)
	set(DEUCESSH_BUILD_SHARED OFF)
	set(DEUCESSH_BUILD_TESTS OFF)
	set(DEUCESSH_CRYPTO_BACKEND "${ST_TLS_PROVIDER}")
	set(DEUCESSH_PARENT_CRYPTO_TARGET "${_st_crypto_dependency}")
	if(ST_TLS_PROVIDER STREQUAL "Botan")
		set(BOTAN3_VENDORED_TARGET "${_st_crypto_dependency}")
		set(BOTAN3_VENDORED_BUILD_TARGET "${_st_crypto_dependency}")
	endif()
	FetchContent_Declare(deucessh
		GIT_REPOSITORY https://github.com/RealDeuce/DeuceSSH.git
		GIT_TAG 66c896ee659628282a68105b44a99b26e5a7cffa
		PATCH_COMMAND "${CMAKE_COMMAND}" "-DSOURCE=<SOURCE_DIR>"
			-P "${CMAKE_CURRENT_LIST_DIR}/PatchDeuceSSH.cmake")
	FetchContent_MakeAvailable(deucessh)
	# Build-interface linkage supplies both pending generated headers and their
	# configuration-specific build dependency, without exporting a parent-owned
	# target in DeuceSSH's installed package.
	target_link_libraries(deucessh_static PRIVATE "$<BUILD_INTERFACE:${_st_crypto_dependency}>")
	set(ST_DEUCESSH_TARGET deucessh_static)
	set_target_properties(deucessh_static PROPERTIES C_EXTENSIONS NO)
elseif(TARGET deucessh::deucessh_static)
	message(FATAL_ERROR "Pre-existing DeuceSSH target cannot establish the selected crypto provider's provenance")
else()
	message(FATAL_ERROR "Pre-existing DeuceSSH target cannot establish the selected crypto provider's provenance")
endif()

# DeuceSSH exposes a C ABI, but its Botan archive contains C++ objects.
# A C-only consumer (including the standalone SSH fixture) needs the runtime
# of the selected C++ compiler even without a dependency on st_crypto.
add_library(st_deucessh INTERFACE)
target_link_libraries(st_deucessh INTERFACE ${ST_DEUCESSH_TARGET} ${_st_crypto_dependency})
if(ST_TLS_PROVIDER STREQUAL "Botan")
	enable_language(CXX)
	target_link_libraries(st_deucessh INTERFACE
		${CMAKE_CXX_IMPLICIT_LINK_LIBRARIES})
	target_link_directories(st_deucessh INTERFACE
		${CMAKE_CXX_IMPLICIT_LINK_DIRECTORIES})
endif()
set(ST_DEUCESSH_TARGET st_deucessh)

# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)
project(DeuceSSHProviderFixture C)

# The selected provider is unavailable until the build runs. Neither system
# OpenSSL nor a configure-time-only include path can satisfy this contract.
set(provider_include "${CMAKE_CURRENT_BINARY_DIR}/provider-include")
file(MAKE_DIRECTORY "${provider_include}")
add_custom_command(OUTPUT "${provider_include}/provider.h"
	COMMAND "${CMAKE_COMMAND}" -E copy
		"${CMAKE_CURRENT_SOURCE_DIR}/provider.h" "${provider_include}/provider.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/provider.h")
add_custom_target(provider_headers DEPENDS "${provider_include}/provider.h")
add_library(selected_provider INTERFACE)
add_dependencies(selected_provider provider_headers)
target_include_directories(selected_provider INTERFACE "${provider_include}")
target_compile_definitions(selected_provider INTERFACE SELECTED_PROVIDER=17)
add_library(st_crypto INTERFACE)
set_property(TARGET st_crypto PROPERTY ST_CRYPTO_DEPENDENCY selected_provider)
set(ST_TLS_PROVIDER OpenSSL)
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_BINARY_DIR}/original-module-path")
include("${MODULES}/DeuceSSH.cmake")
if(NOT CMAKE_MODULE_PATH STREQUAL "${CMAKE_CURRENT_BINARY_DIR}/original-module-path")
	message(FATAL_ERROR "DeuceSSH's package lookup leaked into the parent")
endif()

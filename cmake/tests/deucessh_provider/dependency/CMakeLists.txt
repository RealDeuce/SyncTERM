# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)
project(DeuceSSHDependencyFixture C)
find_package(OpenSSL 3.0 REQUIRED)
get_property(provider TARGET OpenSSL::Crypto PROPERTY INTERFACE_LINK_LIBRARIES)
if(NOT provider STREQUAL selected_provider OR NOT OPENSSL_INCLUDE_DIR STREQUAL provider_include)
	message(FATAL_ERROR "DeuceSSH did not receive the selected provider")
endif()
add_library(deucessh_static STATIC consumer.c)
target_include_directories(deucessh_static PRIVATE "${OPENSSL_INCLUDE_DIR}")
target_link_libraries(deucessh_static INTERFACE OpenSSL::Crypto)
set_target_properties(deucessh_static PROPERTIES
	C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
# A parent-owned provider must not become an export dependency.
install(TARGETS deucessh_static EXPORT fixtureTargets)
install(EXPORT fixtureTargets DESTINATION lib/cmake/fixture)

# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)
project(SyncTERMOpenMPT LANGUAGES CXX)

# Use the release's library source manifest, excluding its tools and tests.
file(STRINGS "${OPENMPT_SOURCE}/Makefile.am" manifest REGEX
	"^(MPT_FILES_(SRC_MPT|SRC_OPENMPT|COMMON|SOUNDBASE|SOUNDLIB|SOUNDDSP)|libopenmpt_la_SOURCES) [+]?= [A-Za-z0-9_/.-]+[.]cpp$")
foreach(entry IN LISTS manifest)
	string(REGEX REPLACE "^.*= " "" path "${entry}")
	list(APPEND sources "${OPENMPT_SOURCE}/${path}")
endforeach()
add_library(openmpt ${sources})
# Preserve the release archive's version information in the decoder.
file(STRINGS "${OPENMPT_SOURCE}/configure.ac" metadata REGEX "^AC_DEFINE.*MPT_SVN")
foreach(line IN LISTS metadata)
	if(line MATCHES [=[^AC_DEFINE\(\[(MPT_SVN[A-Z]+)\], \[("[^"]+")\]]=])
		set_property(SOURCE "${OPENMPT_SOURCE}/common/version.cpp" APPEND PROPERTY
			COMPILE_DEFINITIONS "${CMAKE_MATCH_1}=${CMAKE_MATCH_2}")
	endif()
endforeach()
set_property(SOURCE "${OPENMPT_SOURCE}/common/version.cpp" APPEND PROPERTY COMPILE_DEFINITIONS MPT_PACKAGE=1)
target_compile_features(openmpt PRIVATE cxx_std_20)
target_compile_definitions(openmpt PRIVATE LIBOPENMPT_BUILD)
if(WIN32)
	target_compile_definitions(openmpt PRIVATE UNICODE _UNICODE NOMINMAX)
endif()
if(BUILD_SHARED_LIBS)
	target_compile_definitions(openmpt PRIVATE LIBOPENMPT_BUILD_DLL)
endif()
target_include_directories(openmpt PRIVATE "${OPENMPT_SOURCE}" "${OPENMPT_SOURCE}/common" "${OPENMPT_SOURCE}/src")
install(TARGETS openmpt ARCHIVE DESTINATION lib LIBRARY DESTINATION lib RUNTIME DESTINATION bin)
install(DIRECTORY "${OPENMPT_SOURCE}/libopenmpt/" DESTINATION include/libopenmpt
	FILES_MATCHING PATTERN "*.h")

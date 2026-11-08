# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
get_filename_component(modules "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
include("${modules}/SynctermDependencies.cmake")
foreach(platform posix windows apple)
	set(WIN32 FALSE)
	set(APPLE FALSE)
	if(platform STREQUAL "windows")
		set(WIN32 TRUE)
	elseif(platform STREQUAL "apple")
		set(APPLE TRUE)
	endif()
	set(ST_DEP_POLICY_LINKAGE AUTO CACHE STRING "" FORCE)
	syncterm_dependency_options(POLICY RUNTIME)
	if(platform STREQUAL "posix")
		set(expected RUNTIME)
	else()
		set(expected STATIC)
	endif()
	if(NOT ST_DEP_POLICY_MODE STREQUAL expected)
		message(FATAL_ERROR "Wrong automatic ${platform} policy")
	endif()
	foreach(mode STATIC SHARED RUNTIME)
		set(ST_DEP_POLICY_LINKAGE "${mode}" CACHE STRING "" FORCE)
		syncterm_dependency_options(POLICY RUNTIME)
		if(NOT ST_DEP_POLICY_MODE STREQUAL mode)
			message(FATAL_ERROR "Explicit linkage lost on ${platform}")
		endif()
	endforeach()
endforeach()
function(run label expect)
	execute_process(COMMAND ${ARGN} RESULT_VARIABLE result
		OUTPUT_FILE "${TEST_ROOT}/${label}.log" ERROR_FILE "${TEST_ROOT}/${label}.err")
	if((expect AND NOT result EQUAL 0) OR (NOT expect AND result EQUAL 0))
		message(FATAL_ERROR "Unexpected ${label} result ${result}; see ${TEST_ROOT}/${label}.log/.err")
	endif()
endfunction()
file(MAKE_DIRECTORY "${TEST_ROOT}")
set(source "${CMAKE_CURRENT_LIST_DIR}/dependency_fixture")
foreach(kind STATIC SHARED)
	run("provide-${kind}" TRUE "${CMAKE_COMMAND}" -S "${source}" -B "${TEST_ROOT}/provide-${kind}"
		"-DPROVIDE=${kind}" "-DCMAKE_INSTALL_PREFIX=${TEST_ROOT}/prefix-${kind}")
	run("build-${kind}" TRUE "${CMAKE_COMMAND}" --build "${TEST_ROOT}/provide-${kind}" --target install)
endforeach()
foreach(mode STATIC SHARED RUNTIME)
	foreach(kind STATIC SHARED)
		set(success FALSE)
		if(mode STREQUAL kind OR mode STREQUAL "RUNTIME")
			set(success TRUE)
		endif()
		run("select-${mode}-${kind}" "${success}" "${CMAKE_COMMAND}" -S "${source}"
			-B "${TEST_ROOT}/select-${mode}-${kind}" "-DPROJECT_MODULES=${modules}"
			"-DST_DEP_FIXTURE_LINKAGE=${mode}" "-DST_DEP_FIXTURE_ROOT=${TEST_ROOT}/prefix-${kind}"
			"-DEXPECTED=${mode}")
	endforeach()
endforeach()
# Change linkage in an existing cache: a previous success must not hide failure.
run(reconfigure FALSE "${CMAKE_COMMAND}" -S "${source}"
	-B "${TEST_ROOT}/select-RUNTIME-SHARED" "-DST_DEP_FIXTURE_LINKAGE=STATIC")
run(transitive TRUE "${CMAKE_COMMAND}" -S "${source}"
	-B "${TEST_ROOT}/transitive" "-DPROJECT_MODULES=${modules}"
	-DST_DEP_FIXTURE_LINKAGE=RUNTIME "-DST_DEP_FIXTURE_ROOT=${TEST_ROOT}/prefix-SHARED"
	-DEXPECTED=RUNTIME -DEXERCISE_TRANSITIVE=ON)
run(transitive-build TRUE "${CMAKE_COMMAND}" --build "${TEST_ROOT}/transitive")
if(CMAKE_HOST_SYSTEM_NAME MATCHES "^(Linux|FreeBSD)$")
	set(sdk "${TEST_ROOT}/native-sdk")
	file(MAKE_DIRECTORY "${sdk}/usr")
	file(COPY "${TEST_ROOT}/prefix-SHARED/" DESTINATION "${sdk}/usr")
	run(native-sdk TRUE "${CMAKE_COMMAND}" -S "${source}" -B "${TEST_ROOT}/native-default"
		"-DPROJECT_MODULES=${modules}" -DEXERCISE_NATIVE_SDK=ON "-DNATIVE_SDK=${sdk}"
		-DST_DEP_FIXTURE_LINKAGE=AUTO -DEXPECTED=SHARED)
	run(native-explicit TRUE "${CMAKE_COMMAND}" -S "${source}" -B "${TEST_ROOT}/native-explicit"
		"-DPROJECT_MODULES=${modules}" -DEXERCISE_NATIVE_SDK=ON "-DNATIVE_SDK=${sdk}"
		-DST_DEP_FIXTURE_LINKAGE=STATIC "-DST_DEP_FIXTURE_ROOT=${TEST_ROOT}/prefix-STATIC"
		-DEXPECTED=STATIC)
	file(MAKE_DIRECTORY "${TEST_ROOT}/missing-sdk")
	run(native-absent FALSE "${CMAKE_COMMAND}" -S "${source}" -B "${TEST_ROOT}/native-absent"
		"-DPROJECT_MODULES=${modules}" -DEXERCISE_NATIVE_SDK=ON
		"-DNATIVE_SDK=${TEST_ROOT}/missing-sdk" -DST_DEP_FIXTURE_LINKAGE=AUTO -DEXPECTED=SHARED)
endif()

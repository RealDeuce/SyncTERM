# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
	file(GLOB_RECURSE _modern_ui_scripts CONFIGURE_DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/scripts/*.wren")
	set(_modern_ui_zip "${CMAKE_BINARY_DIR}/addons/syncterm-modern-ui.zip")
	add_custom_command(OUTPUT "${_modern_ui_zip}"
		COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/install.py"
			package --output "${_modern_ui_zip}"
		DEPENDS ${_modern_ui_scripts} install.py README.md LICENSE
		VERBATIM)
	add_custom_target(syncterm_modern_ui_addon DEPENDS "${_modern_ui_zip}")
	if(BUILD_TESTING)
		add_test(NAME modern_ui_installer COMMAND "${Python3_EXECUTABLE}"
			"${CMAKE_CURRENT_SOURCE_DIR}/tests/test_install.py")
		if(ST_CONSOLE_TEST)
			foreach(_case directory settings connection recovery web_list)
				add_test(NAME modern_ui_${_case} COMMAND "${Python3_EXECUTABLE}"
					"${CMAKE_CURRENT_SOURCE_DIR}/tests/test_frontend.py"
					$<TARGET_FILE:syncterm_app> ${_case}
					--reports "${CMAKE_BINARY_DIR}/reports/modern-ui")
				set_tests_properties(modern_ui_${_case} PROPERTIES TIMEOUT 90)
			endforeach()
		endif()
	endif()
endif()

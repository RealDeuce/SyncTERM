# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/SynctermAtomics.cmake")

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(STWrenHost LANGUAGES C)
	include("${CMAKE_CURRENT_LIST_DIR}/../cmake/SynctermCStandard.cmake")
endif()

set(_st_wren_scripts
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/syncterm.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/syncterm_menu.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/syncterm_picker.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/picker_bootstrap.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/wren_console.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/sftp_queue.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/sftp_activity.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/sftp_app.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/pixel_vm_test.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/menu_bbs_editor.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/menu_palette_picker.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/menu_scrollback.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/menu_settings_ui.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/menu_sort_profiles.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/menu_theme_editor.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/menu_ui.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui_app.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui_button.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui_checkbox.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui_color_picker.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui_date_picker.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui_draw.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui_form.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui_help.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui_input.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui_list.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui_logview.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui_markdown.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui_menubar.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui_pane.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui_picker.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui_popup.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui_progress.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui_radio.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui_spinbox.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui_statusbar.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui_style.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ui_widget.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/auto/menu/menu_host_ui.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/auto/menu/main_menu.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/auto/picker/file_picker.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/auto/connected/connected.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/auto/connected/capture_menu.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/auto/connected/console.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/auto/connected/disconnect_flow.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/auto/connected/font_pick.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/auto/connected/keys_default.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/auto/connected/music_menu.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/auto/connected/online_menu.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/auto/connected/scrollback_view.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/auto/connected/session_log.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/auto/connected/sftp_queue_init.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/auto/connected/sftp_pubkey.wren
	${CMAKE_CURRENT_SOURCE_DIR}/../scripts/auto/connected/status_default.wren)

set(_st_wren_embedded ${CMAKE_CURRENT_BINARY_DIR}/embedded_wren.c)
if(WIN32)
	set(_st_wren_directory_fs wren_picker_fs_windows.c)
else()
	set(_st_wren_directory_fs wren_picker_fs_posix.c)
endif()
add_custom_command(OUTPUT ${_st_wren_embedded}
	COMMAND ${CMAKE_COMMAND} -DOUTPUT=${_st_wren_embedded}
		-P ${CMAKE_CURRENT_SOURCE_DIR}/GenerateEmbeddedWren.cmake
		${_st_wren_scripts}
	DEPENDS ${_st_wren_scripts}
		${CMAKE_CURRENT_SOURCE_DIR}/GenerateEmbeddedWren.cmake
	VERBATIM)

include("${CMAKE_CURRENT_SOURCE_DIR}/GenerateLicenses.cmake")

add_library(st_wren_host STATIC
	wren_host.c
	wren_bbs_model.c
	wren_bind_core.c
	wren_bind_won.c
	wren_bind_fs.c
	wren_bind_hyperlinks.c
	wren_sftp.c
	wren_bind_status.c
	wren_bind_session.c
	wren_bind_pixels.c
	wren_repl.c
	wren_bind_menu.c
	${_st_license_output}
	wren_selection.c
	wren_links.c
	wren_directory.c
	wren_scripts.c
	wren_picker.c
	${_st_wren_directory_fs}
	${_st_wren_embedded})
target_include_directories(st_wren_host
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
	PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(st_wren_host SYSTEM PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/../wren_vm/vm)
target_link_libraries(st_wren_host
	PUBLIC st_console st_cterm st_app_paths st_profile st_screen_mode st_session_history
		st_hyperlink st_sftp_client
	PRIVATE st_wren_vm st_re1 st_term st_connection st_codepage st_utf8 st_unicode_text st_unicode_width
		st_unicode_compare
		st_bbs_list st_audio
		st_fonts st_font_catalog st_settings st_theme st_time st_pixel_image st_serial
		syncterm_io_error syncterm_path syncterm_file_map
		syncterm_string_compat syncterm_filesystem
		syncterm_value_convert st_consent_token xptls::crypto)
if(SYNCTERM_ENABLE_UNIFONT)
	target_compile_definitions(st_wren_host PRIVATE ST_HAVE_UNIFONT=1)
endif()
if(WIN32)
	target_link_libraries(st_wren_host PRIVATE st_windows_utf)
endif()
set_target_properties(st_wren_host PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)

if(MSVC)
	# Use the portable C runtime APIs, not Microsoft-only _s replacements.
	target_compile_definitions(st_wren_host PRIVATE _CRT_SECURE_NO_WARNINGS)
	target_compile_options(st_wren_host PRIVATE /W4 /WX)
else()
	target_compile_options(st_wren_host PRIVATE
		-Wall -Wextra -Wpedantic -Werror -Wno-overlength-strings)
endif()

if(BUILD_TESTING)
	add_executable(st_wren_settings_menu_test tests/st_wren_settings_menu_test.c)
	target_include_directories(st_wren_settings_menu_test SYSTEM PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/../wren_vm/vm)
	target_link_libraries(st_wren_settings_menu_test PRIVATE st_wren_host
		st_wren_vm st_bbs_list st_settings st_fonts st_session_history st_audio
		syncterm_threads syncterm_socket syncterm_filesystem)
	target_compile_definitions(st_wren_settings_menu_test PRIVATE
		ST_WREN_SETTINGS_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/settings-menu-test"
		ST_WREN_SETTINGS_SOURCE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/..")
	set_target_properties(st_wren_settings_menu_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_wren_settings_menu_test PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_wren_settings_menu_test PRIVATE /W4 /WX)
	else()
		target_compile_options(st_wren_settings_menu_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME wren_settings_menu COMMAND st_wren_settings_menu_test)
	set_tests_properties(wren_settings_menu PROPERTIES TIMEOUT 120)

	include("${CMAKE_CURRENT_LIST_DIR}/../cmake/SynctermCheckC17Source.cmake")
	set(_st_wren_saved_required_definitions ${CMAKE_REQUIRED_DEFINITIONS})
	if(NOT WIN32)
		if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
			list(APPEND CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
		endif()
		syncterm_check_c17_source([=[
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
int main(void)
{
	int master = posix_openpt(O_RDWR | O_NOCTTY);
	char *name;
	if (master < 0 || grantpt(master) != 0 || unlockpt(master) != 0)
		return 1;
	name = ptsname(master);
	return name == 0 || close(master) != 0;
}
]=] ST_WREN_TEST_HAVE_PTY)
	else()
		set(ST_WREN_TEST_HAVE_PTY 0)
	endif()
	set(CMAKE_REQUIRED_DEFINITIONS ${_st_wren_saved_required_definitions})

	add_executable(st_wren_host_test tests/st_wren_host_test.c)
	target_link_libraries(st_wren_host_test PRIVATE st_wren_host
		st_wren_vm st_settings st_theme st_time st_bbs_list st_audio st_term
		syncterm_filesystem syncterm_string_compat
		syncterm_threads)
	syncterm_target_atomics(st_wren_host_test)
	target_compile_definitions(st_wren_host_test PRIVATE
		ST_WREN_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/host-test"
		ST_WREN_TEST_HAVE_PTY=$<BOOL:${ST_WREN_TEST_HAVE_PTY}>)
	if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
		target_compile_definitions(st_wren_host_test PRIVATE _GNU_SOURCE)
	endif()
	if(SYNCTERM_ENABLE_UNIFONT)
		target_compile_definitions(st_wren_host_test PRIVATE
			ST_HAVE_UNIFONT=1)
	endif()
	set_target_properties(st_wren_host_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_wren_host_test PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_wren_host_test PRIVATE /W4 /WX)
	else()
		target_compile_options(st_wren_host_test PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME wren_host COMMAND st_wren_host_test)
	add_test(NAME wren_menu_contract COMMAND st_wren_host_test --menu-only)
	set_tests_properties(wren_host wren_menu_contract PROPERTIES
		RESOURCE_LOCK wren_host_files TIMEOUT 600)
	if(TARGET st_connection_ssh_fixture)
		target_sources(st_wren_host_test PRIVATE tests/st_wren_sftp_live_test.c)
		target_compile_definitions(st_wren_host_test PRIVATE ST_WREN_HAVE_LIVE_SFTP=1)
		target_link_libraries(st_wren_host_test PRIVATE st_consent_token st_list_crypto)
		add_dependencies(st_wren_host_test st_connection_ssh_fixture)
		add_test(NAME wren_sftp_live COMMAND st_wren_host_test $<TARGET_FILE:st_connection_ssh_fixture>)
		set_tests_properties(wren_sftp_live wren_host PROPERTIES RESOURCE_LOCK wren_host_files TIMEOUT 600)
	endif()

	add_executable(st_wren_loader_audit_test
		tests/st_wren_loader_audit_test.c)
	target_link_libraries(st_wren_loader_audit_test PRIVATE st_wren_host
		st_wren_vm st_bbs_list st_fonts st_settings syncterm_filesystem)
	target_include_directories(st_wren_loader_audit_test PRIVATE
		${CMAKE_CURRENT_BINARY_DIR})
	target_include_directories(st_wren_loader_audit_test SYSTEM PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/../wren_vm/vm)
	set_target_properties(st_wren_loader_audit_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_wren_loader_audit_test PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_wren_loader_audit_test PRIVATE /W4 /WX)
	else()
		target_compile_options(st_wren_loader_audit_test PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
	foreach(_case autoload recovery menu-error script-catalog settings-stale
			settings-save-failure settings-reload screen-mode bbs-setters
			core-helpers)
		set(_directory "${CMAKE_CURRENT_BINARY_DIR}/audit-${_case}")
		file(MAKE_DIRECTORY "${_directory}")
		add_test(NAME "wren_${_case}"
			COMMAND st_wren_loader_audit_test "${_directory}" "${_case}")
	endforeach()
	set(_directory "${CMAKE_CURRENT_BINARY_DIR}/main-menu-address")
	file(MAKE_DIRECTORY "${_directory}")
	add_test(NAME wren_main_menu_address
		COMMAND st_wren_loader_audit_test "${_directory}"
			main-menu-address)
	set_tests_properties(wren_main_menu_address PROPERTIES
		WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/..")
	set(_directory "${CMAKE_CURRENT_BINARY_DIR}/main-menu-position")
	file(MAKE_DIRECTORY "${_directory}")
	add_test(NAME wren_main_menu_position
		COMMAND st_wren_loader_audit_test "${_directory}" main-menu-position)
	set_tests_properties(wren_main_menu_position PROPERTIES TIMEOUT 30)
	set(_directory "${CMAKE_CURRENT_BINARY_DIR}/audit-logview-suite")
	file(MAKE_DIRECTORY "${_directory}")
	add_test(NAME wren_logview
		COMMAND st_wren_loader_audit_test "${_directory}" logview-suite)
	set_tests_properties(wren_logview PROPERTIES
		WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/..")
	set(_directory "${CMAKE_CURRENT_BINARY_DIR}/audit-wren-console-suite")
	file(MAKE_DIRECTORY "${_directory}")
	add_test(NAME wren_console
		COMMAND st_wren_loader_audit_test "${_directory}"
			wren-console-suite)
	set_tests_properties(wren_console PROPERTIES
		WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/..")
	foreach(_widget draw widget radio input form color menubar spinbox popup
			help progress menu)
		set(_directory
			"${CMAKE_CURRENT_BINARY_DIR}/audit-${_widget}-suite")
		file(MAKE_DIRECTORY "${_directory}")
		add_test(NAME "wren_${_widget}"
			COMMAND st_wren_loader_audit_test "${_directory}"
				"${_widget}-suite")
		set_tests_properties("wren_${_widget}" PROPERTIES
			WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/..")
	endforeach()

	if(NOT WIN32)
		foreach(_test readdir_error directory_metadata)
			if(_test STREQUAL "readdir_error")
				set(_source tests/st_wren_picker_readdir_error_test.c)
			else()
				set(_source "tests/st_wren_${_test}_test.c")
			endif()
			add_executable("st_wren_${_test}_test"
				${_source} wren_directory.c)
			target_include_directories("st_wren_${_test}_test" PRIVATE
				${CMAKE_CURRENT_SOURCE_DIR})
			target_link_libraries("st_wren_${_test}_test" PRIVATE
				st_utf8 syncterm_filesystem syncterm_path
				syncterm_string_compat syncterm_io_error)
			set_target_properties("st_wren_${_test}_test" PROPERTIES
				C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
			if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
				target_compile_options("st_wren_${_test}_test" PRIVATE
					-Wall -Wextra -Wpedantic -Werror)
			endif()
		endforeach()
		set(_directory "${CMAKE_CURRENT_BINARY_DIR}/directory-error-test")
		file(MAKE_DIRECTORY "${_directory}")
		add_test(NAME wren_directory_read_error
			COMMAND st_wren_readdir_error_test "${_directory}")
		set(_directory "${CMAKE_CURRENT_BINARY_DIR}/directory-metadata-test")
		file(MAKE_DIRECTORY "${_directory}")
		add_test(NAME wren_directory_metadata
			COMMAND st_wren_directory_metadata_test "${_directory}")
	endif()

	add_executable(st_wren_picker_test tests/st_wren_picker_test.c)
	target_link_libraries(st_wren_picker_test PRIVATE st_wren_host
		st_wren_vm st_bbs_list st_settings syncterm_filesystem syncterm_path
		syncterm_array_list)
	target_compile_definitions(st_wren_picker_test PRIVATE
		ST_WREN_PICKER_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/picker-test")
	syncterm_target_atomics(st_wren_picker_test)
	set_target_properties(st_wren_picker_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_wren_picker_test PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_wren_picker_test PRIVATE /W4 /WX)
	else()
		target_compile_options(st_wren_picker_test PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
	add_test(NAME wren_picker COMMAND st_wren_picker_test)
endif()

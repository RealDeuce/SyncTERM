# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(STConsole LANGUAGES C)
	include(CTest)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/SynctermCheckC17Source.cmake")
find_package(PkgConfig QUIET)

set(ST_CONSOLE_HAVE_QUARTZ 0)
if(APPLE)
	include(CheckLanguage)
	check_language(OBJC)
	if(CMAKE_OBJC_COMPILER)
		enable_language(OBJC)
		find_library(ST_CONSOLE_APPKIT_FRAMEWORK AppKit REQUIRED)
		find_library(ST_CONSOLE_CARBON_FRAMEWORK Carbon REQUIRED)
		set(ST_CONSOLE_HAVE_QUARTZ 1)
	else()
		message(FATAL_ERROR
			"The macOS target requires an Objective-C compiler for the Quartz backend")
	endif()
endif()

set(ST_CONSOLE_HAVE_GDI 0)
set(ST_CONSOLE_HAVE_WIN32 0)
if(WIN32)
	set(_st_console_saved_required_libraries ${CMAKE_REQUIRED_LIBRARIES})
	set(CMAKE_REQUIRED_LIBRARIES user32 gdi32 shell32)
	syncterm_check_c17_source([=[
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
int main(void)
{
	WNDCLASSEXW window_class = {sizeof(window_class)};
	BITMAPINFO bitmap = {0};
	(void)RegisterClassExW(&window_class);
	(void)StretchDIBits(NULL, 0, 0, 1, 1, 0, 0, 1, 1, NULL, &bitmap,
	    DIB_RGB_COLORS, SRCCOPY);
	(void)ShellExecuteW(NULL, L"open", L"https://example.invalid", NULL,
	    NULL, SW_SHOWNORMAL);
	return 0;
}
]=] ST_CONSOLE_GDI_API_WORKS)
	syncterm_check_c17_source([=[
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
int main(void)
{
	CONSOLE_SCREEN_BUFFER_INFOEX information = {sizeof(information)};
	INPUT_RECORD record;
	DWORD count = 0;
	DWORD display_mode = 0;
	COORD dimensions;
	(void)GetConsoleScreenBufferInfoEx(GetStdHandle(STD_OUTPUT_HANDLE),
	    &information);
	(void)ReadConsoleInputW(GetStdHandle(STD_INPUT_HANDLE), &record, 1,
	    &count);
	(void)GetConsoleDisplayMode(&display_mode);
	(void)SetConsoleDisplayMode(GetStdHandle(STD_OUTPUT_HANDLE),
	    CONSOLE_WINDOWED_MODE, &dimensions);
	return 0;
}
]=] ST_CONSOLE_WIN32_API_WORKS)
	set(CMAKE_REQUIRED_LIBRARIES ${_st_console_saved_required_libraries})
	if(ST_CONSOLE_GDI_API_WORKS)
		set(ST_CONSOLE_HAVE_GDI 1)
	else()
		message(FATAL_ERROR "The Windows target does not provide the required GDI window APIs")
	endif()
	if(ST_CONSOLE_WIN32_API_WORKS)
		set(ST_CONSOLE_HAVE_WIN32 1)
	else()
		message(FATAL_ERROR
			"The Windows target does not provide the required native console APIs")
	endif()
endif()

set(ST_CONSOLE_X11 "AUTO" CACHE STRING
	"Build the runtime-loaded X11 bitmap backend (AUTO, ON, or OFF)")
set_property(CACHE ST_CONSOLE_X11 PROPERTY STRINGS AUTO ON OFF)
if(NOT ST_CONSOLE_X11 STREQUAL "AUTO" AND NOT ST_CONSOLE_X11 STREQUAL "ON"
    AND NOT ST_CONSOLE_X11 STREQUAL "OFF")
	message(FATAL_ERROR "ST_CONSOLE_X11 must be AUTO, ON, or OFF")
endif()

set(ST_CONSOLE_WAYLAND "AUTO" CACHE STRING
	"Build the runtime-loaded Wayland backend (AUTO, ON, or OFF)")
set_property(CACHE ST_CONSOLE_WAYLAND PROPERTY STRINGS AUTO ON OFF)
if(NOT ST_CONSOLE_WAYLAND STREQUAL "AUTO"
    AND NOT ST_CONSOLE_WAYLAND STREQUAL "ON"
    AND NOT ST_CONSOLE_WAYLAND STREQUAL "OFF")
	message(FATAL_ERROR "ST_CONSOLE_WAYLAND must be AUTO, ON, or OFF")
endif()

set(ST_CONSOLE_SDL "AUTO" CACHE STRING
	"Build the runtime-loaded SDL bitmap backend (AUTO, ON, or OFF)")
set_property(CACHE ST_CONSOLE_SDL PROPERTY STRINGS AUTO ON OFF)
if(NOT ST_CONSOLE_SDL STREQUAL "AUTO" AND NOT ST_CONSOLE_SDL STREQUAL "ON"
    AND NOT ST_CONSOLE_SDL STREQUAL "OFF")
	message(FATAL_ERROR "ST_CONSOLE_SDL must be AUTO, ON, or OFF")
endif()

set(ST_CONSOLE_HAVE_XKBCOMMON 0)
if(PkgConfig_FOUND)
	pkg_check_modules(ST_CONSOLE_XKBCOMMON_PC QUIET xkbcommon)
endif()
find_path(ST_CONSOLE_XKBCOMMON_INCLUDE_DIR xkbcommon/xkbcommon.h
	HINTS ${ST_CONSOLE_XKBCOMMON_PC_INCLUDE_DIRS})
if(ST_CONSOLE_XKBCOMMON_INCLUDE_DIR)
	set(ST_CONSOLE_HAVE_XKBCOMMON 1)
endif()

set(ST_CONSOLE_HAVE_WAYLAND 0)
set(ST_CONSOLE_HAVE_WAYLAND_ICON 0)
set(ST_CONSOLE_WAYLAND_SOURCES)
set(ST_CONSOLE_WAYLAND_INCLUDE_DIRS)
if(NOT WIN32 AND NOT ST_CONSOLE_WAYLAND STREQUAL "OFF" AND PkgConfig_FOUND)
	pkg_check_modules(ST_CONSOLE_WAYLAND_PC QUIET wayland-client)
	pkg_get_variable(ST_CONSOLE_WAYLAND_DATA wayland-client pkgdatadir)
	pkg_get_variable(ST_CONSOLE_WAYLAND_PROTOCOL_DATA wayland-protocols
		pkgdatadir)
	find_program(ST_CONSOLE_WAYLAND_SCANNER wayland-scanner)
	set(_st_wl_xml ${ST_CONSOLE_WAYLAND_DATA}/wayland.xml)
	set(_st_xdg_xml
		${ST_CONSOLE_WAYLAND_PROTOCOL_DATA}/stable/xdg-shell/xdg-shell.xml)
	set(_st_viewport_xml
		${ST_CONSOLE_WAYLAND_PROTOCOL_DATA}/stable/viewporter/viewporter.xml)
	set(_st_icon_xml
		${ST_CONSOLE_WAYLAND_PROTOCOL_DATA}/staging/xdg-toplevel-icon/xdg-toplevel-icon-v1.xml)
	if(ST_CONSOLE_WAYLAND_PC_FOUND AND ST_CONSOLE_XKBCOMMON_INCLUDE_DIR
	    AND ST_CONSOLE_WAYLAND_SCANNER AND EXISTS "${_st_wl_xml}"
	    AND EXISTS "${_st_xdg_xml}" AND EXISTS "${_st_viewport_xml}")
		set(ST_CONSOLE_HAVE_WAYLAND 1)
		set(ST_CONSOLE_WAYLAND_INCLUDE_DIRS
			${ST_CONSOLE_WAYLAND_PC_INCLUDE_DIRS}
			${ST_CONSOLE_XKBCOMMON_INCLUDE_DIR})
		set(_st_wl_dir ${CMAKE_CURRENT_BINARY_DIR}/wayland-protocols)
		set(_st_wl_protocols wayland xdg-shell viewporter)
		if(EXISTS "${_st_icon_xml}")
			set(ST_CONSOLE_HAVE_WAYLAND_ICON 1)
			list(APPEND _st_wl_protocols xdg-toplevel-icon-v1)
		endif()
		foreach(_name IN LISTS _st_wl_protocols)
			if(_name STREQUAL "wayland")
				set(_xml ${_st_wl_xml})
			elseif(_name STREQUAL "xdg-shell")
				set(_xml ${_st_xdg_xml})
			elseif(_name STREQUAL "viewporter")
				set(_xml ${_st_viewport_xml})
			else()
				set(_xml ${_st_icon_xml})
			endif()
			set(_header ${_st_wl_dir}/${_name}-client.h)
			set(_source ${_st_wl_dir}/${_name}-protocol.c)
			add_custom_command(OUTPUT ${_header} ${_source}
				COMMAND ${CMAKE_COMMAND} -E make_directory ${_st_wl_dir}
				COMMAND ${ST_CONSOLE_WAYLAND_SCANNER} client-header
					${_xml} ${_header}
				COMMAND ${ST_CONSOLE_WAYLAND_SCANNER} private-code
					${_xml} ${_source}
				DEPENDS ${_xml} VERBATIM)
			list(APPEND ST_CONSOLE_WAYLAND_SOURCES ${_header} ${_source})
		endforeach()
	elseif(ST_CONSOLE_WAYLAND STREQUAL "ON")
		message(FATAL_ERROR
			"ST_CONSOLE_WAYLAND=ON requires Wayland and xkbcommon headers plus wayland-scanner protocol data")
	endif()
elseif(ST_CONSOLE_WAYLAND STREQUAL "ON")
	message(FATAL_ERROR "ST_CONSOLE_WAYLAND=ON is unavailable on this target")
endif()

set(_st_console_saved_try_compile_target_type
	${CMAKE_TRY_COMPILE_TARGET_TYPE})
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
syncterm_check_c17_source([=[
#include <curses.h>
int main(void)
{
	WINDOW *window = 0;
	SCREEN *screen = 0;
	(void)window;
	(void)screen;
	return 0;
}
]=] ST_CONSOLE_HAVE_CURSES_H)
if(ST_CONSOLE_HAVE_CURSES_H)
	syncterm_check_c17_source([=[
#include <curses.h>
#include <wchar.h>
int main(void)
{
	WINDOW *window = 0;
	wint_t value = 0;
	int (*get_wide)(WINDOW *, wint_t *) = wget_wch;
	int (*add_wide)(WINDOW *, const wchar_t *, int) = waddnwstr;
	return get_wide(window, &value) + add_wide(window, L"", 0);
}
]=] ST_CONSOLE_HAVE_CURSES_WIDE)
	syncterm_check_c17_source([=[
#include <curses.h>
int main(void)
{
	int (*content)(int, int *, int *, int *) = extended_color_content;
	int (*initialize)(int, int, int, int) = init_extended_color;
	return content(0, 0, 0, 0) + initialize(0, 0, 0, 0);
}
]=] ST_CONSOLE_HAVE_CURSES_EXTENDED_COLOR)
else()
	set(ST_CONSOLE_HAVE_CURSES_WIDE 0)
	set(ST_CONSOLE_HAVE_CURSES_EXTENDED_COLOR 0)
endif()
set(CMAKE_TRY_COMPILE_TARGET_TYPE
	${_st_console_saved_try_compile_target_type})

set(ST_CONSOLE_HAVE_SDL 0)
set(ST_CONSOLE_SDL_INCLUDE_DIRS)
if(NOT ST_CONSOLE_SDL STREQUAL "OFF")
	if(PkgConfig_FOUND)
		pkg_check_modules(ST_CONSOLE_SDL_PC QUIET sdl2)
	endif()
	find_path(ST_CONSOLE_SDL_INCLUDE_DIR SDL.h
		HINTS ${ST_CONSOLE_SDL_PC_INCLUDE_DIRS}
		PATH_SUFFIXES SDL2)
	if(ST_CONSOLE_SDL_INCLUDE_DIR)
		set(ST_CONSOLE_SDL_INCLUDE_DIRS
			${ST_CONSOLE_SDL_PC_INCLUDE_DIRS}
			${ST_CONSOLE_SDL_INCLUDE_DIR})
		list(REMOVE_DUPLICATES ST_CONSOLE_SDL_INCLUDE_DIRS)
		set(_st_console_saved_required_includes ${CMAKE_REQUIRED_INCLUDES})
		set(CMAKE_REQUIRED_INCLUDES ${ST_CONSOLE_SDL_INCLUDE_DIRS})
		syncterm_check_c17_source("#include <SDL.h>\nint main(void) { SDL_Event event; (void)event; return 0; }"
			ST_CONSOLE_SDL_HEADERS_USABLE)
		set(CMAKE_REQUIRED_INCLUDES ${_st_console_saved_required_includes})
	endif()
	if(ST_CONSOLE_SDL_INCLUDE_DIR AND ST_CONSOLE_SDL_HEADERS_USABLE)
		set(ST_CONSOLE_HAVE_SDL 1)
	elseif(ST_CONSOLE_SDL STREQUAL "ON")
		message(FATAL_ERROR
			"ST_CONSOLE_SDL=ON, but usable SDL2 development headers were not found")
	endif()
endif()

set(ST_CONSOLE_HAVE_X11 0)
set(ST_CONSOLE_HAVE_XRENDER 0)
set(ST_CONSOLE_HAVE_XRANDR 0)
set(ST_CONSOLE_HAVE_XINERAMA 0)
set(ST_CONSOLE_X11_INCLUDE_DIRS)
if(NOT WIN32 AND NOT ST_CONSOLE_X11 STREQUAL "OFF")
	find_package(X11 QUIET)
	if(X11_FOUND AND X11_X11_INCLUDE_PATH AND X11_Xutil_FOUND
	    AND X11_Xkb_FOUND)
		set(ST_CONSOLE_HAVE_X11 1)
		set(ST_CONSOLE_X11_INCLUDE_DIRS ${X11_INCLUDE_DIR})
		if(X11_Xrender_FOUND)
			set(ST_CONSOLE_HAVE_XRENDER 1)
		endif()
		if(X11_Xrandr_FOUND)
			set(ST_CONSOLE_HAVE_XRANDR 1)
			list(APPEND ST_CONSOLE_X11_INCLUDE_DIRS
				${X11_Xrandr_INCLUDE_PATH})
		endif()
		if(X11_Xinerama_FOUND)
			set(ST_CONSOLE_HAVE_XINERAMA 1)
			list(APPEND ST_CONSOLE_X11_INCLUDE_DIRS
				${X11_Xinerama_INCLUDE_PATH})
		endif()
	elseif(ST_CONSOLE_X11 STREQUAL "ON")
		message(FATAL_ERROR
			"ST_CONSOLE_X11=ON, but usable Xlib, Xutil, and XKB headers were not found")
	endif()
endif()

configure_file(console_config.h.in console_config.h @ONLY)

if(NOT TARGET syncterm_io_error)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../io_error"
		"${CMAKE_CURRENT_BINARY_DIR}/io_error")
endif()
if(NOT TARGET syncterm_threads)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../threads"
		"${CMAKE_CURRENT_BINARY_DIR}/threads")
endif()
if(NOT TARGET syncterm_array_list)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../array_list"
		"${CMAKE_CURRENT_BINARY_DIR}/array_list")
endif()
if(NOT TARGET syncterm_dynamic_library)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../dynamic_library"
		"${CMAKE_CURRENT_BINARY_DIR}/dynamic_library")
endif()
if(NOT TARGET syncterm_string_compat)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../string_compat"
		"${CMAKE_CURRENT_BINARY_DIR}/string_compat")
endif()
if(NOT TARGET st_pixels)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../pixels"
		"${CMAKE_CURRENT_BINARY_DIR}/pixels")
endif()
if(NOT TARGET st_utf8)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../utf8"
		"${CMAKE_CURRENT_BINARY_DIR}/utf8")
endif()
if(NOT TARGET st_unicode_width)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../unicode_width"
		"${CMAKE_CURRENT_BINARY_DIR}/unicode_width")
endif()
if(NOT TARGET st_unicode_normalization)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../unicode_normalization"
		"${CMAKE_CURRENT_BINARY_DIR}/unicode_normalization")
endif()
if(NOT TARGET st_unicode_grapheme)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../unicode_grapheme"
		"${CMAKE_CURRENT_BINARY_DIR}/unicode_grapheme")
endif()
if(NOT TARGET st_unicode_text)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../unicode_text"
		"${CMAKE_CURRENT_BINARY_DIR}/unicode_text")
endif()
if(NOT TARGET st_codepage)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../codepage"
		"${CMAKE_CURRENT_BINARY_DIR}/codepage")
endif()
if(NOT TARGET st_time)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../time"
		"${CMAKE_CURRENT_BINARY_DIR}/time")
endif()
if(WIN32 AND NOT TARGET st_windows_utf)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../windows_utf"
		"${CMAKE_CURRENT_BINARY_DIR}/windows_utf")
endif()

set(_st_console_sources
	console.c
	console_text.c
	console_text_terminal.c
	console_pixels.c
	console_bitmap.c
	console_libretro.c
	console_ansi.c
	console_ncurses.c)
if(WIN32)
	list(APPEND _st_console_sources console_ansi_windows.c)
else()
	list(APPEND _st_console_sources console_ansi_posix.c)
endif()
if(ST_CONSOLE_HAVE_SDL)
	list(APPEND _st_console_sources console_sdl.c)
endif()
if(ST_CONSOLE_HAVE_X11)
	list(APPEND _st_console_sources console_x11.c console_x11_keys.c
		console_x11_pixels.c)
endif()
if(ST_CONSOLE_HAVE_WAYLAND)
	list(APPEND _st_console_sources console_wayland.c
		console_wayland_loader.c ${ST_CONSOLE_WAYLAND_SOURCES})
endif()
if(ST_CONSOLE_HAVE_WIN32)
	list(APPEND _st_console_sources console_win32.c)
endif()
if(ST_CONSOLE_HAVE_GDI)
	list(APPEND _st_console_sources console_gdi.c)
endif()
if(ST_CONSOLE_HAVE_QUARTZ)
	list(APPEND _st_console_sources console_quartz.m)
endif()
add_library(st_console STATIC ${_st_console_sources})
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
	set_source_files_properties(console_wayland_loader.c
		console_wayland.c console_sdl.c console_ansi.c console_ncurses.c
		console_x11.c
		PROPERTIES COMPILE_OPTIONS -Wno-pedantic)
endif()
target_include_directories(st_console PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
	PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${ST_CONSOLE_SDL_INCLUDE_DIRS}
		${ST_CONSOLE_X11_INCLUDE_DIRS} ${ST_CONSOLE_WAYLAND_INCLUDE_DIRS}
		${_st_wl_dir})
target_link_libraries(st_console PUBLIC syncterm_io_error syncterm_threads
	st_pixels PRIVATE syncterm_array_list syncterm_dynamic_library st_utf8
	st_time st_unicode_width st_unicode_text st_codepage syncterm_string_compat)
if(WIN32)
	target_link_libraries(st_console PRIVATE st_windows_utf)
endif()
if(ST_CONSOLE_HAVE_GDI OR ST_CONSOLE_HAVE_WIN32)
	target_link_libraries(st_console PRIVATE user32 shell32)
endif()
if(ST_CONSOLE_HAVE_GDI)
	target_link_libraries(st_console PRIVATE gdi32)
endif()
if(ST_CONSOLE_HAVE_QUARTZ)
	target_link_libraries(st_console PRIVATE ${ST_CONSOLE_APPKIT_FRAMEWORK}
		${ST_CONSOLE_CARBON_FRAMEWORK})
	set_target_properties(st_console PROPERTIES
		OBJC_STANDARD 17 OBJC_STANDARD_REQUIRED YES OBJC_EXTENSIONS NO)
endif()
set_target_properties(st_console PROPERTIES
	C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)

function(st_console_warnings target)
	if(MSVC)
		target_compile_options(${target} PRIVATE /W4 /WX)
	else()
		target_compile_options(${target} PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
endfunction()
st_console_warnings(st_console)

if(ST_CONSOLE_HAVE_SDL OR ST_CONSOLE_HAVE_QUARTZ)
	add_library(st_console_main STATIC console_main_entry.c)
	target_include_directories(st_console_main PRIVATE
		${CMAKE_CURRENT_BINARY_DIR})
	target_link_libraries(st_console_main PUBLIC st_console)
	target_compile_definitions(st_console_main INTERFACE
		main=st_console_application_main)
	set_target_properties(st_console_main PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	st_console_warnings(st_console_main)
endif()

if(BUILD_TESTING)
	add_executable(st_console_test tests/st_console_test.c)
	target_link_libraries(st_console_test PRIVATE st_console
		syncterm_array_list)
	set_target_properties(st_console_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	st_console_warnings(st_console_test)
	add_test(NAME st_console COMMAND st_console_test)
	add_executable(st_console_regrid_test
		tests/st_console_regrid_test.c)
	target_link_libraries(st_console_regrid_test PRIVATE st_console)
	set_target_properties(st_console_regrid_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	st_console_warnings(st_console_regrid_test)
	add_test(NAME st_console_regrid COMMAND st_console_regrid_test)
	set_tests_properties(st_console PROPERTIES
		ENVIRONMENT "TERM=xterm"
		TIMEOUT 20)
	if(NOT WIN32)
		include(CheckIncludeFile)
		check_include_file(pty.h ST_CONSOLE_TEST_HAVE_PTY_H)
		check_include_file(util.h ST_CONSOLE_TEST_HAVE_UTIL_H)
		check_include_file(libutil.h ST_CONSOLE_TEST_HAVE_LIBUTIL_H)
		set(_st_console_pty_header)
		if(ST_CONSOLE_TEST_HAVE_PTY_H)
			set(_st_console_pty_header pty.h)
		elseif(ST_CONSOLE_TEST_HAVE_UTIL_H)
			set(_st_console_pty_header util.h)
		elseif(ST_CONSOLE_TEST_HAVE_LIBUTIL_H)
			set(_st_console_pty_header libutil.h)
		endif()
		find_library(ST_CONSOLE_TEST_UTIL_LIBRARY NAMES util)
		set(_st_console_saved_required_libraries ${CMAKE_REQUIRED_LIBRARIES})
		if(ST_CONSOLE_TEST_UTIL_LIBRARY)
			set(CMAKE_REQUIRED_LIBRARIES ${ST_CONSOLE_TEST_UTIL_LIBRARY})
		endif()
		if(_st_console_pty_header)
			syncterm_check_c17_source("#include <${_st_console_pty_header}>\n#include <sys/ioctl.h>\nint main(void) { int fd; struct winsize size = {0}; return forkpty(&fd, 0, 0, &size) < 0; }"
				ST_CONSOLE_TEST_HAVE_FORKPTY)
		endif()
		set(CMAKE_REQUIRED_LIBRARIES ${_st_console_saved_required_libraries})
		if(ST_CONSOLE_TEST_HAVE_FORKPTY)
			add_executable(st_console_ansi_test
				tests/st_console_ansi_test.c)
			target_link_libraries(st_console_ansi_test PRIVATE st_console)
			if(ST_CONSOLE_TEST_UTIL_LIBRARY)
				target_link_libraries(st_console_ansi_test PRIVATE
					${ST_CONSOLE_TEST_UTIL_LIBRARY})
			endif()
			target_compile_definitions(st_console_ansi_test PRIVATE
				ST_CONSOLE_TEST_HAVE_PTY_H=$<BOOL:${ST_CONSOLE_TEST_HAVE_PTY_H}>
				ST_CONSOLE_TEST_HAVE_UTIL_H=$<BOOL:${ST_CONSOLE_TEST_HAVE_UTIL_H}>
				ST_CONSOLE_TEST_HAVE_LIBUTIL_H=$<BOOL:${ST_CONSOLE_TEST_HAVE_LIBUTIL_H}>)
			set_target_properties(st_console_ansi_test PROPERTIES
				C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
			st_console_warnings(st_console_ansi_test)
			add_test(NAME st_console_ansi COMMAND st_console_ansi_test)
			set_tests_properties(st_console_ansi PROPERTIES
				ENVIRONMENT "TERM=xterm-256color;COLORTERM=" TIMEOUT 20)
			add_executable(st_console_ncurses_test
				tests/st_console_ncurses_test.c)
			target_link_libraries(st_console_ncurses_test PRIVATE st_console)
			if(ST_CONSOLE_TEST_UTIL_LIBRARY)
				target_link_libraries(st_console_ncurses_test PRIVATE
					${ST_CONSOLE_TEST_UTIL_LIBRARY})
			endif()
			target_compile_definitions(st_console_ncurses_test PRIVATE
				ST_CONSOLE_TEST_HAVE_PTY_H=$<BOOL:${ST_CONSOLE_TEST_HAVE_PTY_H}>
				ST_CONSOLE_TEST_HAVE_UTIL_H=$<BOOL:${ST_CONSOLE_TEST_HAVE_UTIL_H}>
				ST_CONSOLE_TEST_HAVE_LIBUTIL_H=$<BOOL:${ST_CONSOLE_TEST_HAVE_LIBUTIL_H}>)
			set_target_properties(st_console_ncurses_test PROPERTIES
				C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
			st_console_warnings(st_console_ncurses_test)
			add_test(NAME st_console_ncurses COMMAND st_console_ncurses_test)
			set_tests_properties(st_console_ncurses PROPERTIES
				ENVIRONMENT "TERM=xterm-256color;COLORTERM="
				SKIP_RETURN_CODE 77 TIMEOUT 20)
			add_test(NAME st_console_ncurses_direct
				COMMAND st_console_ncurses_test)
			set_tests_properties(st_console_ncurses_direct PROPERTIES
				ENVIRONMENT "TERM=xterm-256color;COLORTERM=truecolor"
				SKIP_RETURN_CODE 77 TIMEOUT 20)
			add_test(NAME st_console_ncurses_cp437
				COMMAND st_console_ncurses_test --cp437)
			set_tests_properties(st_console_ncurses_cp437 PROPERTIES
				ENVIRONMENT "TERM=xterm-256color;COLORTERM="
				SKIP_RETURN_CODE 77 TIMEOUT 20)
			add_test(NAME st_console_ncurses_direct_cp437
				COMMAND st_console_ncurses_test --cp437)
			set_tests_properties(st_console_ncurses_direct_cp437 PROPERTIES
				ENVIRONMENT "TERM=xterm-256color;COLORTERM=truecolor"
				SKIP_RETURN_CODE 77 TIMEOUT 20)
		endif()
	endif()
	add_executable(st_console_bitmap_test tests/console_bitmap_test.c)
	target_include_directories(st_console_bitmap_test PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR})
	target_link_libraries(st_console_bitmap_test PRIVATE st_console
		syncterm_array_list)
	set_target_properties(st_console_bitmap_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	st_console_warnings(st_console_bitmap_test)
	add_test(NAME st_console_bitmap COMMAND st_console_bitmap_test)
	foreach(test IN ITEMS animation scheduler saa_damage saa_modes saa_blink
		switch_geometry)
		add_executable(st_console_${test}_test
			tests/st_console_${test}_test.c)
		target_include_directories(st_console_${test}_test PRIVATE
			${CMAKE_CURRENT_SOURCE_DIR})
		target_link_libraries(st_console_${test}_test PRIVATE st_console
			syncterm_array_list)
		if(test STREQUAL "scheduler")
			target_include_directories(st_console_${test}_test PRIVATE
				${CMAKE_CURRENT_SOURCE_DIR}/../time)
		endif()
		set_target_properties(st_console_${test}_test PROPERTIES
			C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
		st_console_warnings(st_console_${test}_test)
		add_test(NAME st_console_${test} COMMAND st_console_${test}_test)
	endforeach()
	add_executable(st_console_libretro_test tests/st_console_libretro_test.c)
	target_link_libraries(st_console_libretro_test PRIVATE st_console)
	set_target_properties(st_console_libretro_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	st_console_warnings(st_console_libretro_test)
	add_test(NAME st_console_libretro COMMAND st_console_libretro_test)
	add_executable(st_console_benchmark tests/st_console_benchmark.c)
	target_link_libraries(st_console_benchmark PRIVATE st_console st_time)
	set_target_properties(st_console_benchmark PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	st_console_warnings(st_console_benchmark)
	if(ST_CONSOLE_HAVE_SDL)
		add_executable(st_console_sdl_test tests/st_console_sdl_test.c)
		if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
			set_source_files_properties(tests/st_console_sdl_test.c
				PROPERTIES COMPILE_OPTIONS -Wno-pedantic)
		endif()
		target_include_directories(st_console_sdl_test PRIVATE
			${ST_CONSOLE_SDL_INCLUDE_DIRS})
		target_link_libraries(st_console_sdl_test PRIVATE st_console_main
			syncterm_dynamic_library)
		set_target_properties(st_console_sdl_test PROPERTIES
			C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
		st_console_warnings(st_console_sdl_test)
		add_test(NAME st_console_sdl COMMAND st_console_sdl_test)
		set_tests_properties(st_console_sdl PROPERTIES
			ENVIRONMENT
				"SDL_VIDEODRIVER=dummy;DISPLAY=;WAYLAND_DISPLAY="
			SKIP_RETURN_CODE 77
			TIMEOUT 20)
	endif()
	if(ST_CONSOLE_HAVE_X11)
		add_executable(st_console_x11_pixels_test
			tests/st_console_x11_pixels_test.c)
		target_include_directories(st_console_x11_pixels_test PRIVATE
			${CMAKE_CURRENT_SOURCE_DIR})
		target_link_libraries(st_console_x11_pixels_test PRIVATE st_console
			X11::X11 st_time)
		set_target_properties(st_console_x11_pixels_test PROPERTIES
			C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
		st_console_warnings(st_console_x11_pixels_test)
		add_test(NAME st_console_x11_pixels COMMAND st_console_x11_pixels_test)
		add_executable(st_console_x11_test tests/st_console_x11_test.c)
		target_include_directories(st_console_x11_test PRIVATE
			${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}
			${ST_CONSOLE_X11_INCLUDE_DIRS})
		target_link_libraries(st_console_x11_test PRIVATE st_console
			X11::X11 syncterm_array_list)
		if(ST_CONSOLE_HAVE_XRANDR)
			target_link_libraries(st_console_x11_test PRIVATE X11::Xrandr)
		endif()
		set_target_properties(st_console_x11_test PROPERTIES
			C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
		st_console_warnings(st_console_x11_test)
		add_test(NAME st_console_x11 COMMAND st_console_x11_test)
		set_tests_properties(st_console_x11 PROPERTIES
			SKIP_RETURN_CODE 77
			TIMEOUT 20)
		if(ST_CONSOLE_HAVE_XRENDER)
			add_test(NAME st_console_x11_xrender
				COMMAND st_console_x11_test --xrender-pixels)
			set_tests_properties(st_console_x11_xrender PROPERTIES
				SKIP_RETURN_CODE 77
				TIMEOUT 20)
		endif()
	endif()
	if(ST_CONSOLE_HAVE_WAYLAND)
		add_executable(st_console_wayland_internal_test
			tests/st_console_wayland_internal_test.c)
		target_include_directories(st_console_wayland_internal_test PRIVATE
			${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}
			${ST_CONSOLE_WAYLAND_INCLUDE_DIRS} ${_st_wl_dir})
		target_link_libraries(st_console_wayland_internal_test PRIVATE
			st_console syncterm_array_list syncterm_dynamic_library
			st_time)
		set_target_properties(st_console_wayland_internal_test PROPERTIES
			C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
		st_console_warnings(st_console_wayland_internal_test)
		if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
			target_compile_options(st_console_wayland_internal_test PRIVATE
				-Wno-pedantic)
		endif()
		add_test(NAME st_console_wayland_internal
			COMMAND st_console_wayland_internal_test)
		set_tests_properties(st_console_wayland_internal PROPERTIES
			TIMEOUT 20)
		add_executable(st_console_wayland_test
			tests/st_console_wayland_test.c)
		target_link_libraries(st_console_wayland_test PRIVATE st_console)
		set_target_properties(st_console_wayland_test PROPERTIES
			C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
		st_console_warnings(st_console_wayland_test)
		add_test(NAME st_console_wayland COMMAND st_console_wayland_test)
		set_tests_properties(st_console_wayland PROPERTIES
			SKIP_RETURN_CODE 77 TIMEOUT 20)
	endif()
	if(ST_CONSOLE_HAVE_QUARTZ)
		add_executable(st_console_quartz_test
			tests/st_console_quartz_test.c)
		target_link_libraries(st_console_quartz_test PRIVATE
			st_console_main)
		set_target_properties(st_console_quartz_test PROPERTIES
			C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
		st_console_warnings(st_console_quartz_test)
		add_test(NAME st_console_quartz COMMAND st_console_quartz_test)
		set_tests_properties(st_console_quartz PROPERTIES
			SKIP_RETURN_CODE 77 TIMEOUT 20)
	endif()
	if(ST_CONSOLE_HAVE_GDI)
		add_executable(st_console_gdi_test tests/st_console_gdi_test.c)
		target_link_libraries(st_console_gdi_test PRIVATE st_console)
		set_target_properties(st_console_gdi_test PROPERTIES
			C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
		st_console_warnings(st_console_gdi_test)
		add_test(NAME st_console_gdi COMMAND st_console_gdi_test)
		set_tests_properties(st_console_gdi PROPERTIES
			SKIP_RETURN_CODE 77 TIMEOUT 20)
	endif()
	if(ST_CONSOLE_HAVE_WIN32)
		add_executable(st_console_win32_test
			tests/st_console_win32_test.c)
		target_link_libraries(st_console_win32_test PRIVATE st_console)
		set_target_properties(st_console_win32_test PROPERTIES
			C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
		st_console_warnings(st_console_win32_test)
		add_test(NAME st_console_win32 COMMAND st_console_win32_test)
		set_tests_properties(st_console_win32 PROPERTIES
			SKIP_RETURN_CODE 77 TIMEOUT 20)
		add_executable(st_console_ansi_windows_test
			tests/st_console_ansi_windows_test.c)
		target_link_libraries(st_console_ansi_windows_test PRIVATE st_console)
		set_target_properties(st_console_ansi_windows_test PROPERTIES
			C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
		st_console_warnings(st_console_ansi_windows_test)
		add_test(NAME st_console_ansi_windows
			COMMAND st_console_ansi_windows_test)
		set_tests_properties(st_console_ansi_windows PROPERTIES TIMEOUT 20)
	endif()
endif()

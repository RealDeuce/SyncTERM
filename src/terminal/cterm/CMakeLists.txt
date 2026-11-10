# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermTestRunner.cmake")

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCheckC17Source.cmake")

if(WIN32)
	set(ST_CTERM_APC_CACHE_SOURCE cterm_apc_cache_windows.c)
	set(ST_CTERM_RIP_LOOKUP_SOURCE cterm_rip_asset_lookup_windows.c)
	set(ST_CTERM_RIP_TIME_SOURCE cterm_rip_time_windows.c)
	syncterm_check_c17_source([=[
#include <time.h>
int main(void)
{
	time_t value = 0;
	struct tm result;
	return localtime_s(&result, &value) != 0;
}
]=] ST_CTERM_HAVE_LOCALTIME_S)
	if(NOT ST_CTERM_HAVE_LOCALTIME_S)
		message(FATAL_ERROR "The Windows C library must provide localtime_s for RIP file queries")
	endif()
else()
	set(ST_CTERM_APC_CACHE_SOURCE cterm_apc_cache_posix.c)
	set(ST_CTERM_RIP_LOOKUP_SOURCE cterm_rip_asset_lookup_posix.c)
	set(ST_CTERM_RIP_TIME_SOURCE cterm_rip_time_posix.c)
	set(_st_cterm_saved_required_definitions ${CMAKE_REQUIRED_DEFINITIONS})
	# Darwin hides O_NOFOLLOW and older NetBSD headers expose dirfd only
	# in their extended namespaces. Probe exactly the environment we compile.
	set(_st_cterm_apc_definitions _POSIX_C_SOURCE=200809L
		_DARWIN_C_SOURCE _NETBSD_SOURCE)
	foreach(_definition IN LISTS _st_cterm_apc_definitions)
		list(APPEND CMAKE_REQUIRED_DEFINITIONS "-D${_definition}")
	endforeach()
	syncterm_check_c17_source([=[
#include <dirent.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
int main(void)
{
	struct stat info;
	int fd = open(".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	int child = openat(fd, "a", O_RDONLY | O_NOFOLLOW | O_NONBLOCK, 0600);
	DIR *directory = fdopendir(fd);
	(void)fstatat(dirfd(directory), "a", &info, AT_SYMLINK_NOFOLLOW);
	(void)mkdirat(fd, "b", 0700);
	(void)ftruncate(child, 0);
	(void)fdopen(child, "rb");
	(void)close(child);
	return fnmatch("*", "a", FNM_PERIOD) + closedir(directory);
}
]=] ST_CTERM_HAVE_CONFINED_APC_CACHE)
	if(NOT ST_CTERM_HAVE_CONFINED_APC_CACHE)
		message(FATAL_ERROR "The POSIX APC cache requires confined directory-relative file access and fnmatch")
	endif()
	syncterm_check_c17_source([=[
#include <time.h>
int main(void)
{
	time_t value = 0;
	struct tm result;
	return localtime_r(&value, &result) == 0;
}
]=] ST_CTERM_HAVE_LOCALTIME_R)
	set(CMAKE_REQUIRED_DEFINITIONS ${_st_cterm_saved_required_definitions})
	unset(_st_cterm_saved_required_definitions)
	if(NOT ST_CTERM_HAVE_LOCALTIME_R)
		message(FATAL_ERROR "The POSIX C library must provide localtime_r for RIP file queries")
	endif()
	set_source_files_properties(cterm_rip_time_posix.c PROPERTIES
		COMPILE_DEFINITIONS _POSIX_C_SOURCE=200809L)
	set_source_files_properties(cterm_apc_cache_posix.c PROPERTIES
		COMPILE_DEFINITIONS "${_st_cterm_apc_definitions}")
endif()

add_library(st_cterm STATIC
	cterm_igs.c
	cterm_igs_state.c
	cterm_igs_raster.c
	cterm.c
	cterm_apc.c
	cterm_apc_audio.c
	cterm_apc_cache.c
	${ST_CTERM_APC_CACHE_SOURCE}
	cterm_atascii.c
	cterm_cterm.c
	cterm_dec.c
	cterm_ecma48.c
	cterm_render.c
	cterm_skypix_walkers.c
	cterm_skypix.c
	cterm_skypix_audio.c
	cterm_skypix_parser.c
	cterm_skypix_ilbm.c
	cterm_text.c
	cterm_petscii.c
	cterm_prestel.c
	cterm_rip_asset.c
	${ST_CTERM_RIP_LOOKUP_SOURCE}
	cterm_rip_engine.c
	cterm_rip_file_query.c
	cterm_rip_fonts.c
	cterm_rip_icon_file.c
	cterm_rip_icons.c
	cterm_rip_parser.c
	cterm_rip_raster.c
	${ST_CTERM_RIP_TIME_SOURCE}
	st_cterm.c
	cterm_vt52.c)
target_include_directories(st_cterm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_cterm PUBLIC st_console st_codepage st_font_catalog st_hyperlink
	st_audio st_music st_base64 st_time st_utf8 st_unicode_text st_session_history)
target_link_libraries(st_cterm PRIVATE st_amiga_font st_fonts syncterm_threads
	st_graphics_protocol st_igs st_screen_mode
	st_audio_file st_pixel_image st_md5 st_hex
	syncterm_string_compat syncterm_string_util syncterm_filesystem
	syncterm_path syncterm_ini)
if(WIN32)
	target_link_libraries(st_cterm PRIVATE st_windows_utf)
endif()
set_target_properties(st_cterm PROPERTIES C_STANDARD ${SYNCTERM_C_STANDARD}
	C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(MSVC)
	# Use the portable C runtime APIs, not Microsoft-only _s replacements.
	target_compile_definitions(st_cterm PRIVATE _CRT_SECURE_NO_WARNINGS)
	target_compile_options(st_cterm PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
else()
	target_compile_options(st_cterm PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
endif()

if(BUILD_TESTING)
	syncterm_add_test_case(st_terminal_tests st_cterm_igs_input_test tests/st_cterm_igs_input_test.c)
	target_link_libraries(st_cterm_igs_input_test PRIVATE st_cterm st_graphics_protocol st_igs)
	set_target_properties(st_cterm_igs_input_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_igs_input_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_igs_input_test PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_igs_input COMMAND st_terminal_tests st_cterm_igs_input_test)
	set_tests_properties(cterm_igs_input PROPERTIES TIMEOUT 60)
	syncterm_add_test_case(st_terminal_tests st_cterm_igs_init_test tests/st_cterm_igs_init_test.c)
	target_link_libraries(st_cterm_igs_init_test PRIVATE st_cterm st_graphics_protocol st_igs)
	set_target_properties(st_cterm_igs_init_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_igs_init_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_igs_init_test PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_igs_init COMMAND st_terminal_tests st_cterm_igs_init_test)
	set_tests_properties(cterm_igs_init PROPERTIES TIMEOUT 60)
	syncterm_add_test_case(st_terminal_tests st_cterm_igs_resolution_test tests/st_cterm_igs_resolution_test.c)
	target_link_libraries(st_cterm_igs_resolution_test PRIVATE st_cterm st_graphics_protocol st_igs)
	set_target_properties(st_cterm_igs_resolution_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_igs_resolution_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_igs_resolution_test PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_igs_resolution COMMAND st_terminal_tests st_cterm_igs_resolution_test)
	set_tests_properties(cterm_igs_resolution PROPERTIES TIMEOUT 60)
	syncterm_add_test_case(st_terminal_tests st_cterm_igs_scaling_test tests/st_cterm_igs_scaling_test.c)
	target_link_libraries(st_cterm_igs_scaling_test PRIVATE st_cterm st_graphics_protocol)
	set_target_properties(st_cterm_igs_scaling_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_igs_scaling_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_igs_scaling_test PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_igs_scaling COMMAND st_terminal_tests st_cterm_igs_scaling_test)
	set_tests_properties(cterm_igs_scaling PROPERTIES TIMEOUT 60)
	syncterm_add_test_case(st_terminal_tests st_cterm_igs_writing_mode_test tests/st_cterm_igs_writing_mode_test.c)
	target_link_libraries(st_cterm_igs_writing_mode_test PRIVATE st_cterm)
	set_target_properties(st_cterm_igs_writing_mode_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_igs_writing_mode_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_igs_writing_mode_test PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_igs_writing_mode COMMAND st_terminal_tests st_cterm_igs_writing_mode_test)
	set_tests_properties(cterm_igs_writing_mode PROPERTIES TIMEOUT 60)
	syncterm_add_test_case(st_terminal_tests st_cterm_igs_pen_test tests/st_cterm_igs_pen_test.c)
	target_link_libraries(st_cterm_igs_pen_test PRIVATE st_cterm)
	set_target_properties(st_cterm_igs_pen_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_igs_pen_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_igs_pen_test PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_igs_pen COMMAND st_terminal_tests st_cterm_igs_pen_test)
	set_tests_properties(cterm_igs_pen PROPERTIES TIMEOUT 60)
	syncterm_add_test_case(st_terminal_tests st_cterm_igs_palette_test tests/st_cterm_igs_palette_test.c)
	target_link_libraries(st_cterm_igs_palette_test PRIVATE st_cterm)
	set_target_properties(st_cterm_igs_palette_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_igs_palette_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_igs_palette_test PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_igs_palette COMMAND st_terminal_tests st_cterm_igs_palette_test)
	set_tests_properties(cterm_igs_palette PROPERTIES TIMEOUT 60)
	syncterm_add_test_case(st_terminal_tests st_cterm_igs_hollow_test tests/st_cterm_igs_hollow_test.c)
	target_link_libraries(st_cterm_igs_hollow_test PRIVATE st_cterm)
	set_target_properties(st_cterm_igs_hollow_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_igs_hollow_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_igs_hollow_test PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_igs_hollow COMMAND st_terminal_tests st_cterm_igs_hollow_test)
	set_tests_properties(cterm_igs_hollow PROPERTIES TIMEOUT 60)
	syncterm_add_test_case(st_terminal_tests st_cterm_igs_fill_test tests/st_cterm_igs_fill_test.c)
	target_link_libraries(st_cterm_igs_fill_test PRIVATE st_cterm st_graphics_protocol)
	set_target_properties(st_cterm_igs_fill_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_igs_fill_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_igs_fill_test PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_igs_fill COMMAND st_terminal_tests st_cterm_igs_fill_test)
	set_tests_properties(cterm_igs_fill PROPERTIES TIMEOUT 60)
	syncterm_add_test_case(st_terminal_tests st_cterm_igs_type_test tests/st_cterm_igs_type_test.c)
	target_link_libraries(st_cterm_igs_type_test PRIVATE st_cterm)
	set_target_properties(st_cterm_igs_type_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_igs_type_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_igs_type_test PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_igs_type COMMAND st_terminal_tests st_cterm_igs_type_test)
	set_tests_properties(cterm_igs_type PROPERTIES TIMEOUT 60)
	syncterm_add_test_case(st_terminal_tests st_cterm_igs_marker_test tests/st_cterm_igs_marker_test.c)
	target_link_libraries(st_cterm_igs_marker_test PRIVATE st_cterm)
	set_target_properties(st_cterm_igs_marker_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_igs_marker_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_igs_marker_test PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_igs_marker COMMAND st_terminal_tests st_cterm_igs_marker_test)
	set_tests_properties(cterm_igs_marker PROPERTIES TIMEOUT 60)
	syncterm_add_test_case(st_terminal_tests st_cterm_igs_line_test tests/st_cterm_igs_line_test.c)
	target_link_libraries(st_cterm_igs_line_test PRIVATE st_cterm)
	set_target_properties(st_cterm_igs_line_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_igs_line_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_igs_line_test PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_igs_line COMMAND st_terminal_tests st_cterm_igs_line_test)
	set_tests_properties(cterm_igs_line PROPERTIES TIMEOUT 120)
	syncterm_add_test_case(st_terminal_tests st_cterm_igs_drawto_test tests/st_cterm_igs_drawto_test.c)
	target_link_libraries(st_cterm_igs_drawto_test PRIVATE st_cterm)
	set_target_properties(st_cterm_igs_drawto_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_igs_drawto_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_igs_drawto_test PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_igs_drawto COMMAND st_terminal_tests st_cterm_igs_drawto_test)
	set_tests_properties(cterm_igs_drawto PROPERTIES TIMEOUT 120)
	syncterm_add_test_case(st_terminal_tests st_cterm_igs_x10_test tests/st_cterm_igs_x10_test.c)
	target_link_libraries(st_cterm_igs_x10_test PRIVATE st_cterm)
	set_target_properties(st_cterm_igs_x10_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_igs_x10_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_igs_x10_test PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_igs_x10 COMMAND st_terminal_tests st_cterm_igs_x10_test)
	set_tests_properties(cterm_igs_x10 PROPERTIES TIMEOUT 120)
	syncterm_add_test_case(st_terminal_tests st_cterm_igs_polyline_test tests/st_cterm_igs_polyline_test.c)
	target_link_libraries(st_cterm_igs_polyline_test PRIVATE st_cterm)
	set_target_properties(st_cterm_igs_polyline_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_igs_polyline_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_igs_polyline_test PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_igs_polyline COMMAND st_terminal_tests st_cterm_igs_polyline_test)
	set_tests_properties(cterm_igs_polyline PROPERTIES TIMEOUT 120)
	syncterm_add_test_case(st_terminal_tests st_cterm_igs_rectangle_test tests/st_cterm_igs_rectangle_test.c)
	target_link_libraries(st_cterm_igs_rectangle_test PRIVATE st_cterm)
	set_target_properties(st_cterm_igs_rectangle_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_igs_rectangle_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_igs_rectangle_test PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_igs_rectangle COMMAND st_terminal_tests st_cterm_igs_rectangle_test)
	set_tests_properties(cterm_igs_rectangle PROPERTIES TIMEOUT 120)
	syncterm_add_test_case(st_terminal_tests st_cterm_igs_box_test tests/st_cterm_igs_box_test.c)
	target_link_libraries(st_cterm_igs_box_test PRIVATE st_cterm)
	set_target_properties(st_cterm_igs_box_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_igs_box_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_igs_box_test PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_igs_box COMMAND st_terminal_tests st_cterm_igs_box_test)
	set_tests_properties(cterm_igs_box PROPERTIES TIMEOUT 120)
	syncterm_add_test_case(st_terminal_tests st_cterm_igs_round_box_test tests/st_cterm_igs_round_box_test.c)
	target_link_libraries(st_cterm_igs_round_box_test PRIVATE st_cterm)
	set_target_properties(st_cterm_igs_round_box_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_igs_round_box_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_igs_round_box_test PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_igs_round_box COMMAND st_terminal_tests st_cterm_igs_round_box_test)
	set_tests_properties(cterm_igs_round_box PROPERTIES TIMEOUT 120)
	syncterm_add_test_case(st_terminal_tests st_cterm_igs_circle_test tests/st_cterm_igs_circle_test.c)
	target_link_libraries(st_cterm_igs_circle_test PRIVATE st_cterm)
	set_target_properties(st_cterm_igs_circle_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_igs_circle_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_igs_circle_test PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_igs_circle COMMAND st_terminal_tests st_cterm_igs_circle_test)
	set_tests_properties(cterm_igs_circle PROPERTIES TIMEOUT 120)
	syncterm_add_test_case(st_terminal_tests st_cterm_igs_ellipse_test tests/st_cterm_igs_ellipse_test.c)
	target_link_libraries(st_cterm_igs_ellipse_test PRIVATE st_cterm)
	set_target_properties(st_cterm_igs_ellipse_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_igs_ellipse_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_igs_ellipse_test PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_igs_ellipse COMMAND st_terminal_tests st_cterm_igs_ellipse_test)
	set_tests_properties(cterm_igs_ellipse PROPERTIES TIMEOUT 120)
	syncterm_add_test_case(st_terminal_tests st_cterm_igs_arc_test tests/st_cterm_igs_arc_test.c)
	target_link_libraries(st_cterm_igs_arc_test PRIVATE st_cterm)
	set_target_properties(st_cterm_igs_arc_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_igs_arc_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_igs_arc_test PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_igs_arc COMMAND st_terminal_tests st_cterm_igs_arc_test)
	set_tests_properties(cterm_igs_arc PROPERTIES TIMEOUT 120)
	syncterm_add_test_case(st_terminal_tests st_cterm_igs_ellarc_test tests/st_cterm_igs_ellarc_test.c)
	target_link_libraries(st_cterm_igs_ellarc_test PRIVATE st_cterm)
	set_target_properties(st_cterm_igs_ellarc_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_igs_ellarc_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_igs_ellarc_test PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_igs_ellarc COMMAND st_terminal_tests st_cterm_igs_ellarc_test)
	set_tests_properties(cterm_igs_ellarc PROPERTIES TIMEOUT 120)
	syncterm_add_test_case(st_terminal_tests st_cterm_igs_pie_test tests/st_cterm_igs_pie_test.c)
	target_link_libraries(st_cterm_igs_pie_test PRIVATE st_cterm)
	set_target_properties(st_cterm_igs_pie_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_igs_pie_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_igs_pie_test PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_igs_pie COMMAND st_terminal_tests st_cterm_igs_pie_test)
	set_tests_properties(cterm_igs_pie PROPERTIES TIMEOUT 120)
	syncterm_add_test_case(st_terminal_tests st_cterm_igs_ellpie_test tests/st_cterm_igs_ellpie_test.c)
	target_link_libraries(st_cterm_igs_ellpie_test PRIVATE st_cterm)
	set_target_properties(st_cterm_igs_ellpie_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_igs_ellpie_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_igs_ellpie_test PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_igs_ellpie COMMAND st_terminal_tests st_cterm_igs_ellpie_test)
	set_tests_properties(cterm_igs_ellpie PROPERTIES TIMEOUT 120)
	syncterm_add_test_case(st_terminal_tests st_cterm_igs_polygon_test tests/st_cterm_igs_polygon_test.c)
	target_link_libraries(st_cterm_igs_polygon_test PRIVATE st_cterm)
	set_target_properties(st_cterm_igs_polygon_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_igs_polygon_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_igs_polygon_test PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_igs_polygon COMMAND st_terminal_tests st_cterm_igs_polygon_test)
	set_tests_properties(cterm_igs_polygon PROPERTIES TIMEOUT 120)
	syncterm_add_test_case(st_terminal_tests st_cterm_igs_flood_test tests/st_cterm_igs_flood_test.c)
	target_link_libraries(st_cterm_igs_flood_test PRIVATE st_cterm)
	set_target_properties(st_cterm_igs_flood_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_igs_flood_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_igs_flood_test PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_igs_flood COMMAND st_terminal_tests st_cterm_igs_flood_test)
	set_tests_properties(cterm_igs_flood PROPERTIES TIMEOUT 120)
	syncterm_add_test_case(st_terminal_tests st_cterm_igs_text_attributes_test tests/st_cterm_igs_text_attributes_test.c)
	target_link_libraries(st_cterm_igs_text_attributes_test PRIVATE st_cterm)
	set_target_properties(st_cterm_igs_text_attributes_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_igs_text_attributes_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_igs_text_attributes_test PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_igs_text_attributes COMMAND st_terminal_tests st_cterm_igs_text_attributes_test)
	set_tests_properties(cterm_igs_text_attributes PROPERTIES TIMEOUT 120)
	option(ST_CTERM_TEST_EXTERNAL_ASSETS "Use optional external RIP test assets" ON)
	# These replay/inventory tools are built explicitly for external comparisons.
	add_executable(st_cterm_apc_replay EXCLUDE_FROM_ALL tests/st_cterm_apc_replay.c)
	target_link_libraries(st_cterm_apc_replay PRIVATE st_cterm)
	set_target_properties(st_cterm_apc_replay PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_cterm_apc_replay PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_cterm_apc_replay PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_apc_replay PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	syncterm_add_test_case(st_terminal_tests st_cterm_apc_test tests/st_cterm_apc_test.c)
	target_link_libraries(st_cterm_apc_test PRIVATE st_cterm st_audio_file st_pixel_image st_md5
		syncterm_filesystem)
	target_compile_definitions(st_cterm_apc_test PRIVATE
		ST_APC_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/apc-test-cache")
	if(NOT WIN32)
		target_compile_definitions(st_cterm_apc_test PRIVATE
			ST_APC_TEST_POSIX=1 ${_st_cterm_apc_definitions})
	endif()
	set_target_properties(st_cterm_apc_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_cterm_apc_test PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_cterm_apc_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_apc_test PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_apc COMMAND st_terminal_tests st_cterm_apc_test)
	syncterm_add_test_case(st_terminal_tests st_cterm_apc_audio_test tests/st_cterm_apc_audio_test.c)
	target_link_libraries(st_cterm_apc_audio_test PRIVATE st_cterm)
	target_compile_definitions(st_cterm_apc_audio_test PRIVATE
		ST_APC_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/apc-audio-test-cache")
	set_target_properties(st_cterm_apc_audio_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_cterm_apc_audio_test PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_cterm_apc_audio_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_apc_audio_test PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_apc_audio COMMAND st_terminal_tests st_cterm_apc_audio_test)
	add_executable(st_cterm_skypix_audio_test tests/st_cterm_skypix_audio_test.c
		tests/st_cterm_skypix_audio_test_bindings.c)
	target_link_libraries(st_cterm_skypix_audio_test PRIVATE st_cterm st_fonts)
	set_target_properties(st_cterm_skypix_audio_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_cterm_skypix_audio_test PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_cterm_skypix_audio_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_skypix_audio_test PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_skypix_audio COMMAND st_cterm_skypix_audio_test)
	set_tests_properties(cterm_skypix_audio PROPERTIES TIMEOUT 30)
	syncterm_add_test_case(st_terminal_tests st_cterm_midi_test tests/st_cterm_midi_test.c)
	target_link_libraries(st_cterm_midi_test PRIVATE st_cterm syncterm_filesystem syncterm_threads st_time)
	target_compile_definitions(st_cterm_midi_test PRIVATE ST_MIDI_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/midi-cache")
	set_target_properties(st_cterm_midi_test PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	add_test(NAME cterm_midi COMMAND st_terminal_tests st_cterm_midi_test)
	set_tests_properties(cterm_midi PROPERTIES TIMEOUT 30)
	syncterm_add_test_case(st_terminal_tests st_cterm_numeric_test tests/st_cterm_numeric_test.c)
	target_link_libraries(st_cterm_numeric_test PRIVATE st_cterm)
	set_target_properties(st_cterm_numeric_test PROPERTIES C_STANDARD 17
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_cterm_numeric_test PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_cterm_numeric_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_numeric_test PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_numeric COMMAND st_terminal_tests st_cterm_numeric_test)
	syncterm_add_test_case(st_terminal_tests st_cterm_sixel_test tests/st_cterm_sixel_test.c)
	target_link_libraries(st_cterm_sixel_test PRIVATE st_cterm)
	set_target_properties(st_cterm_sixel_test PROPERTIES C_STANDARD 17
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_cterm_sixel_test PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_cterm_sixel_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_sixel_test PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_sixel COMMAND st_terminal_tests st_cterm_sixel_test)
	syncterm_add_test_case(st_terminal_tests st_cterm_benchmark ARGS tests/st_cterm_benchmark.c)
	target_link_libraries(st_cterm_benchmark PRIVATE st_cterm st_time)
	set_target_properties(st_cterm_benchmark PROPERTIES C_STANDARD ${SYNCTERM_C_STANDARD}
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_cterm_benchmark PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_cterm_benchmark PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_benchmark PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_stream_consistency COMMAND st_terminal_tests st_cterm_benchmark 100)

	syncterm_add_test_case(st_terminal_tests st_cterm_rip_icon_file_test
		tests/st_cterm_rip_icon_file_test.c)
	target_link_libraries(st_cterm_rip_icon_file_test PRIVATE st_cterm)
	# The external RIP assets are optional test inputs, not a configuration
	# prerequisite. SyncTERM v1.10a is the rendering reference.
	if(ST_CTERM_TEST_EXTERNAL_ASSETS AND EXISTS "/synchronet/tmp/ripterm/ICONS/RLIB.ICN")
		target_compile_definitions(st_cterm_rip_icon_file_test PRIVATE
			RIP_ICON_PATH="/synchronet/tmp/ripterm/ICONS/RLIB.ICN")
	endif()
	set_target_properties(st_cterm_rip_icon_file_test PROPERTIES C_STANDARD ${SYNCTERM_C_STANDARD}
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_cterm_rip_icon_file_test PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_cterm_rip_icon_file_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_rip_icon_file_test PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_rip_icon_file COMMAND st_terminal_tests st_cterm_rip_icon_file_test)
	syncterm_add_test_case(st_terminal_tests st_cterm_rip_transfer_test tests/st_cterm_rip_transfer_test.c)
	target_link_libraries(st_cterm_rip_transfer_test PRIVATE st_cterm syncterm_filesystem)
	target_compile_definitions(st_cterm_rip_transfer_test PRIVATE
		RIP_TRANSFER_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/rip-transfer-test")
	set_target_properties(st_cterm_rip_transfer_test PROPERTIES C_STANDARD ${SYNCTERM_C_STANDARD}
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_cterm_rip_transfer_test PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_cterm_rip_transfer_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_rip_transfer_test PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_rip_transfer COMMAND st_terminal_tests st_cterm_rip_transfer_test)
	if(NOT WIN32)
		target_compile_definitions(st_cterm_rip_transfer_test PRIVATE
			ST_RIP_TRANSFER_TEST_POSIX=1 ${_st_cterm_apc_definitions})
	endif()

	syncterm_add_test_case(st_terminal_tests st_cterm_rip_assets_test
		tests/st_cterm_rip_assets_test.c)
	target_link_libraries(st_cterm_rip_assets_test PRIVATE st_cterm
		syncterm_filesystem syncterm_path)
	target_compile_definitions(st_cterm_rip_assets_test PRIVATE
		RIP_ASSET_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/rip-assets-test")
	if(ST_CTERM_TEST_EXTERNAL_ASSETS AND EXISTS "/synchronet/tmp/ripterm/ICONS/RLIB.ICN")
		target_compile_definitions(st_cterm_rip_assets_test PRIVATE
			RIP_ICON_PATH="/synchronet/tmp/ripterm/ICONS/RLIB.ICN")
	endif()
	set_target_properties(st_cterm_rip_assets_test PROPERTIES C_STANDARD ${SYNCTERM_C_STANDARD}
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_cterm_rip_assets_test PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_cterm_rip_assets_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_rip_assets_test PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_rip_assets COMMAND st_terminal_tests st_cterm_rip_assets_test)

	syncterm_add_test_case(st_terminal_tests st_cterm_test tests/st_cterm_test.c)
	target_link_libraries(st_cterm_test PRIVATE st_cterm)
	set_target_properties(st_cterm_test PROPERTIES C_STANDARD ${SYNCTERM_C_STANDARD}
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_cterm_test PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_cterm_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_test PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm COMMAND st_terminal_tests st_cterm_test)
	syncterm_add_test_case(st_terminal_tests st_cterm_text_test tests/st_cterm_text_test.c)
	target_link_libraries(st_cterm_text_test PRIVATE st_cterm st_fonts)
	set_target_properties(st_cterm_text_test PROPERTIES C_STANDARD ${SYNCTERM_C_STANDARD}
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_cterm_text_test PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_cterm_text_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_text_test PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_text COMMAND st_terminal_tests st_cterm_text_test)
	syncterm_add_test_case(st_terminal_tests st_cterm_keyboard_test tests/st_cterm_keyboard_test.c)
	target_link_libraries(st_cterm_keyboard_test PRIVATE st_cterm)
	set_target_properties(st_cterm_keyboard_test PROPERTIES C_STANDARD ${SYNCTERM_C_STANDARD}
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_cterm_keyboard_test PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_cterm_keyboard_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_keyboard_test PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_keyboard COMMAND st_terminal_tests st_cterm_keyboard_test)

	syncterm_add_test_case(st_terminal_tests st_cterm_rip_parser_test tests/st_cterm_rip_parser_test.c)
	target_link_libraries(st_cterm_rip_parser_test PRIVATE st_cterm)
	set_target_properties(st_cterm_rip_parser_test PROPERTIES C_STANDARD ${SYNCTERM_C_STANDARD}
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_cterm_rip_parser_test PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_cterm_rip_parser_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_rip_parser_test PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_rip_parser COMMAND st_terminal_tests st_cterm_rip_parser_test)

	syncterm_add_test_case(st_terminal_tests st_cterm_rip_engine_test tests/st_cterm_rip_engine_test.c)
	target_link_libraries(st_cterm_rip_engine_test PRIVATE st_cterm)
	set_target_properties(st_cterm_rip_engine_test PROPERTIES C_STANDARD ${SYNCTERM_C_STANDARD}
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_cterm_rip_engine_test PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_cterm_rip_engine_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_rip_engine_test PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_rip_engine COMMAND st_terminal_tests st_cterm_rip_engine_test)

	syncterm_add_test_case(st_terminal_tests st_cterm_rip_raster_test tests/st_cterm_rip_raster_test.c)
	target_link_libraries(st_cterm_rip_raster_test PRIVATE st_cterm)
	set_target_properties(st_cterm_rip_raster_test PROPERTIES C_STANDARD ${SYNCTERM_C_STANDARD}
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_cterm_rip_raster_test PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_cterm_rip_raster_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_rip_raster_test PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_rip_raster COMMAND st_terminal_tests st_cterm_rip_raster_test)

	syncterm_add_test_case(st_terminal_tests st_cterm_rip_live_test tests/st_cterm_rip_live_test.c)
	target_link_libraries(st_cterm_rip_live_test PRIVATE st_cterm)
	set_target_properties(st_cterm_rip_live_test PROPERTIES C_STANDARD ${SYNCTERM_C_STANDARD}
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_cterm_rip_live_test PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_cterm_rip_live_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_rip_live_test PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_rip_live COMMAND st_terminal_tests st_cterm_rip_live_test)

	syncterm_add_test_case(st_terminal_tests st_cterm_rip_variables_test
		tests/st_cterm_rip_variables_test.c)
	target_link_libraries(st_cterm_rip_variables_test PRIVATE st_cterm
		syncterm_filesystem syncterm_ini syncterm_path)
	target_compile_definitions(st_cterm_rip_variables_test PRIVATE
		RIP_VARIABLES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/rip-variables-test")
	set_target_properties(st_cterm_rip_variables_test PROPERTIES C_STANDARD ${SYNCTERM_C_STANDARD}
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_cterm_rip_variables_test PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_cterm_rip_variables_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_rip_variables_test PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_rip_variables COMMAND st_terminal_tests st_cterm_rip_variables_test)

	syncterm_add_test_case(st_terminal_tests st_cterm_rip_unicode_text_test
		tests/st_cterm_rip_unicode_text_test.c)
	target_link_libraries(st_cterm_rip_unicode_text_test PRIVATE st_cterm)
	set_target_properties(st_cterm_rip_unicode_text_test PROPERTIES C_STANDARD ${SYNCTERM_C_STANDARD}
		C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_cterm_rip_unicode_text_test PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_cterm_rip_unicode_text_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_rip_unicode_text_test PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_rip_unicode_text
		COMMAND st_terminal_tests st_cterm_rip_unicode_text_test)

	# A local v1.10a capture can verify the external corpus input without
	# adding either the RIP file or its derived frame to this source tree.
	if(ST_CTERM_TEST_EXTERNAL_ASSETS AND EXISTS "/synchronet/tmp/ripterm/rips/gl-morfr.rip"
		AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/v110a_gl_morfr_expected.h")
		syncterm_add_test_case(st_terminal_tests st_cterm_rip_v110a_live_test
			tests/st_cterm_rip_v110a_live_test.c)
		target_link_libraries(st_cterm_rip_v110a_live_test PRIVATE st_cterm)
		target_compile_definitions(st_cterm_rip_v110a_live_test PRIVATE
			RIP_V110A_GLMORFR_PATH="/synchronet/tmp/ripterm/rips/gl-morfr.rip")
		set_target_properties(st_cterm_rip_v110a_live_test PROPERTIES
			C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
		if(MSVC)
			target_compile_definitions(st_cterm_rip_v110a_live_test PRIVATE _CRT_SECURE_NO_WARNINGS)
			target_compile_options(st_cterm_rip_v110a_live_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
		else()
			target_compile_options(st_cterm_rip_v110a_live_test PRIVATE
				-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
		endif()
		add_test(NAME cterm_rip_v110a_live
			COMMAND st_terminal_tests st_cterm_rip_v110a_live_test)
	endif()

	add_executable(st_cterm_rip_corpus_inventory EXCLUDE_FROM_ALL
		tests/st_cterm_rip_corpus_inventory.c)
	target_link_libraries(st_cterm_rip_corpus_inventory PRIVATE st_cterm)
	set_target_properties(st_cterm_rip_corpus_inventory PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_cterm_rip_corpus_inventory PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_cterm_rip_corpus_inventory PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_rip_corpus_inventory PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()

	add_executable(st_cterm_rip_corpus_replay EXCLUDE_FROM_ALL
		tests/st_cterm_rip_corpus_replay.c)
	target_link_libraries(st_cterm_rip_corpus_replay PRIVATE st_term)
	set_target_properties(st_cterm_rip_corpus_replay PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_definitions(st_cterm_rip_corpus_replay PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(st_cterm_rip_corpus_replay PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_rip_corpus_replay PRIVATE
			-Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()

	find_package(Python3 COMPONENTS Interpreter QUIET)
	if(Python3_Interpreter_FOUND)
		add_test(NAME cterm_rip_oracle
			COMMAND Python3::Interpreter
				"${CMAKE_CURRENT_SOURCE_DIR}/tests/test_compare_legacy_rip_live.py")
	endif()
endif()

if(BUILD_TESTING)
	syncterm_add_test_case(st_terminal_tests st_cterm_skypix_test tests/st_cterm_skypix_test.c)
	target_link_libraries(st_cterm_skypix_test PRIVATE st_cterm st_amiga_font st_fonts)
	set_target_properties(st_cterm_skypix_test PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	add_test(NAME cterm_skypix COMMAND st_terminal_tests st_cterm_skypix_test)
	syncterm_add_test_case(st_terminal_tests st_cterm_skypix_cursor_test tests/st_cterm_skypix_cursor_test.c)
	target_link_libraries(st_cterm_skypix_cursor_test PRIVATE st_cterm st_amiga_font st_fonts)
	set_target_properties(st_cterm_skypix_cursor_test PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	add_test(NAME cterm_skypix_cursor COMMAND st_terminal_tests st_cterm_skypix_cursor_test)
	if(MSVC)
		target_compile_options(st_cterm_skypix_cursor_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_skypix_cursor_test PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
endif()

if(BUILD_TESTING)
	add_executable(st_skypix_vector_probe tests/st_skypix_vector_probe.c cterm_skypix_walkers.c)
	target_include_directories(st_skypix_vector_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
	target_link_libraries(st_skypix_vector_probe PRIVATE st_amiga_font)
	set_target_properties(st_skypix_vector_probe PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	add_test(NAME skypix_rom_vectors COMMAND Python3::Interpreter
		${CMAKE_CURRENT_SOURCE_DIR}/tests/test_skypix_vectors.py $<TARGET_FILE:st_skypix_vector_probe>)
endif()

if(BUILD_TESTING)
	foreach(_test IN ITEMS st_cterm_skypix_test st_skypix_vector_probe)
		if(MSVC)
			target_compile_options(${_test} PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
		else()
			target_compile_options(${_test} PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
		endif()
	endforeach()
endif()

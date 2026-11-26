# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermTestRunner.cmake")

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCheckC17Source.cmake")

if(WIN32)
	set(ST_CTERM_APC_CACHE_SOURCE cterm_apc_cache_windows.c)

else()
	set(ST_CTERM_APC_CACHE_SOURCE cterm_apc_cache_posix.c)
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
	set(CMAKE_REQUIRED_DEFINITIONS ${_st_cterm_saved_required_definitions})
	unset(_st_cterm_saved_required_definitions)
	set_source_files_properties(cterm_apc_cache_posix.c PROPERTIES
		COMPILE_DEFINITIONS "${_st_cterm_apc_definitions}")
endif()

add_library(st_cterm STATIC
	cterm_key.c
	cterm.c
	cterm_musicomm.c
	cterm_apc.c
	cterm_apc_audio.c
	cterm_apc_cache.c
	${ST_CTERM_APC_CACHE_SOURCE}
	cterm_atascii.c
	cterm_cterm.c
	cterm_dec.c
	cterm_ecma48.c
	cterm_render.c
	cterm_text.c
	cterm_petscii.c
	cterm_prestel.c
	st_cterm.c
	st_cterm_text_window.c
	st_cterm_window_definition.c
	cterm_vt52.c)
target_include_directories(st_cterm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_cterm PUBLIC st_console st_codepage st_font_catalog st_hyperlink
	st_audio st_music st_base64 st_time st_utf8 st_unicode_text st_session_history)
target_link_libraries(st_cterm PRIVATE st_atari_st_audio st_fonts syncterm_threads
	st_bgi st_screen_mode
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
	add_executable(st_cterm_text_window_test tests/st_cterm_text_window_test.c
		tests/st_cterm_text_window_fault_impl.c)
	target_link_libraries(st_cterm_text_window_test PRIVATE st_cterm syncterm_string_compat)
	set_target_properties(st_cterm_text_window_test PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_text_window_test PRIVATE /W4)
	else()
		target_compile_options(st_cterm_text_window_test PRIVATE -Wall -Wextra -Wpedantic)
	endif()
	add_test(NAME cterm_text_window COMMAND st_cterm_text_window_test)

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
	target_link_libraries(st_cterm_apc_test PRIVATE st_cterm st_rip154 st_audio_file st_pixel_image st_md5
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
	syncterm_add_test_case(st_terminal_tests st_cterm_music_render_test tests/st_cterm_music_render_test.c)
	target_link_libraries(st_cterm_music_render_test PRIVATE st_cterm syncterm_array_list)
	syncterm_target_atomics(st_cterm_music_render_test)
	target_compile_definitions(st_cterm_music_render_test PRIVATE ST_AUDIO_TESTING
		ST_APC_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/music-render-test-cache")
	set_target_properties(st_cterm_music_render_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_music_render_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_music_render_test PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_music_render COMMAND st_terminal_tests st_cterm_music_render_test)
	set_tests_properties(cterm_music_render PROPERTIES TIMEOUT 60)
	syncterm_add_test_case(st_terminal_tests st_cterm_musicomm_test tests/st_cterm_musicomm_test.c)
	target_link_libraries(st_cterm_musicomm_test PRIVATE st_cterm st_atari_st_audio)
	syncterm_target_atomics(st_cterm_musicomm_test)
	set_target_properties(st_cterm_musicomm_test PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_cterm_musicomm_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_cterm_musicomm_test PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME cterm_musicomm COMMAND st_terminal_tests st_cterm_musicomm_test)
	set_tests_properties(cterm_musicomm PROPERTIES TIMEOUT 30)

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

	syncterm_add_test_case(st_terminal_tests st_cterm_test tests/st_cterm_test.c)
	target_link_libraries(st_cterm_test PRIVATE st_cterm st_rip154)
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


endif()

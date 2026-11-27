# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermDependencies.cmake")
syncterm_dependency(NAME LIBZIP PACKAGE libzip HEADER zip.h LIBRARIES zip DEFAULT SHARED REQUIRED LINK_REQUIRED PROBE [=[
#include <zip.h>
int main(void) {
	zip_t *zip = zip_open("test.zip", ZIP_RDONLY | ZIP_CHECKCONS, 0);
	if (zip) zip_discard(zip);
	return 0;
}
]=])
add_library(st_ddofx STATIC st_ddofx_package.c st_ddofx.c st_ddofx_graphics.c st_ddofx_text_windows.c st_ddofx_drawing.c st_ddofx_bitmap_text.c st_ddofx_vga_text.c st_ddofx_lite_screen.c st_ddofx_sprites.c st_ddofx_sprite_bank.c st_ddofx_collision.c st_ddofx_animation.c st_ddofx_simple.c st_ddofx_grid.c st_ddofx_image.c st_ddofx_bars.c st_ddofx_marquee.c st_ddofx_clocks.c st_ddofx_flic.c st_ddofx_movies.c st_ddofx_landscape.c st_ddofx_ray_world.c st_ddofx_ray_view.c st_ddofx_ray_environment.c st_ddofx_ray_record.c st_ddofx_ray_profiles.c st_ddofx_joystick.c st_ddofx_pcx.c st_ddofx_script.c st_ddofx_info.c st_ddofx_text_files.c st_ddofx_events.c st_ddofx_keys.c st_ddofx_mouse.c st_ddofx_pointer.c st_ddofx_buttons.c st_ddofx_text_fields.c st_ddofx_lists.c st_ddofx_choices.c st_ddofx_resource.c st_ddofx_samples.c)
target_link_libraries(st_ddofx PUBLIC st_tracker PRIVATE st_file_lock ${ST_DEP_LIBZIP_TARGET} st_cterm st_fonts st_cmf st_time syncterm_string_util syncterm_string_compat syncterm_filesystem syncterm_path syncterm_value_convert)
syncterm_dependency_math(st_ddofx)
syncterm_target_atomics(st_ddofx)
target_include_directories(st_ddofx PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(st_ddofx PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(MSVC)
	target_compile_options(st_ddofx PRIVATE /W4)
else()
	target_compile_options(st_ddofx PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(BUILD_TESTING)
	add_executable(st_ddofx_pcx_test tests/st_ddofx_pcx_test.c)
	target_link_libraries(st_ddofx_pcx_test PRIVATE st_ddofx st_cterm)
	set_target_properties(st_ddofx_pcx_test PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	add_test(NAME ddofx_pcx COMMAND st_ddofx_pcx_test)
	add_executable(st_ddofx_music_test tests/st_ddofx_music_test.c)
	target_link_libraries(st_ddofx_music_test PRIVATE st_ddofx st_cterm)
	set_target_properties(st_ddofx_music_test PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	add_test(NAME ddofx_music COMMAND st_ddofx_music_test)
	add_executable(st_ddofx_script_test tests/st_ddofx_script_test.c)
	target_link_libraries(st_ddofx_script_test PRIVATE st_ddofx)
	set_target_properties(st_ddofx_script_test PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	add_test(NAME ddofx_script COMMAND st_ddofx_script_test)
endif()

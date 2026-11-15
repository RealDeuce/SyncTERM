# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
set(_rip3_font_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts")
set(_rip3_font_data "${CMAKE_CURRENT_BINARY_DIR}/builtin_fonts.c")
set(_rip3_font_files)
foreach(name IN ITEMS BRUSH.RFF COBB.RFF DEFAULT.RFF DIXON.RFF EUREKA.RFF
	MARIN.RFF OAKLAND.RFF SYMBOL.RFF RIPscrip.maf)
	list(APPEND _rip3_font_files "${_rip3_font_directory}/${name}")
endforeach()
add_custom_command(OUTPUT "${_rip3_font_data}"
	COMMAND "${CMAKE_COMMAND}" "-DASSET_DIRECTORY=${_rip3_font_directory}"
		"-DOUTPUT=${_rip3_font_data}" -P "${CMAKE_CURRENT_SOURCE_DIR}/GenerateFontData.cmake"
	DEPENDS ${_rip3_font_files} "${_rip3_font_directory}/manifest.json" GenerateFontData.cmake
	VERBATIM)
add_library(st_rip3 STATIC st_rip3_number.c st_rip3_maf.c st_rip3_rff.c "${_rip3_font_data}")
target_include_directories(st_rip3 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_rip3 PUBLIC syncterm_io_error)
set_target_properties(st_rip3 PROPERTIES
	C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(MSVC)
	target_compile_options(st_rip3 PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
else()
	target_compile_options(st_rip3 PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
endif()

if(BUILD_TESTING)
	add_executable(st_rip3_number_test tests/st_rip3_number_test.c)
	target_link_libraries(st_rip3_number_test PRIVATE st_rip3)
	set_target_properties(st_rip3_number_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_rip3_number_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_rip3_number_test PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME rip3_number COMMAND st_rip3_number_test)
	add_executable(st_rip3_maf_test tests/st_rip3_maf_test.c)
	target_link_libraries(st_rip3_maf_test PRIVATE st_rip3)
	set_target_properties(st_rip3_maf_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_rip3_maf_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_rip3_maf_test PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME rip3_maf COMMAND st_rip3_maf_test)
	add_executable(st_rip3_rff_test tests/st_rip3_rff_test.c)
	target_link_libraries(st_rip3_rff_test PRIVATE st_rip3)
	set_target_properties(st_rip3_rff_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(st_rip3_rff_test PRIVATE /W4 $<$<CONFIG:Debug>:/WX>)
	else()
		target_compile_options(st_rip3_rff_test PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)
	endif()
	add_test(NAME rip3_rff COMMAND st_rip3_rff_test)
	add_test(NAME rip3_font_assets COMMAND "${CMAKE_COMMAND}"
		"-DTEST_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/asset-check"
		"-DGENERATOR=${CMAKE_CURRENT_SOURCE_DIR}/GenerateFontData.cmake"
		-P "${CMAKE_CURRENT_SOURCE_DIR}/tests/font_assets.cmake")
endif()

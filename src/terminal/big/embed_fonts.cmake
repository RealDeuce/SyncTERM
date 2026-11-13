# Original font bytes are inputs; generated C belongs in the build tree.
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
set(_st_big_font_names Triplex Small Sans Gothic)
set(_st_big_font_hashes
	b687918d590a22273fbb30967e0e50cb9e4ff9d3c3c97591bc6ec5460d655c9a
	dc51f652a1d863273eb291830b3fba5e283b9dbe65b366b0b9eae02e7b237f08
	9bae0df17f137f3b9171f19dde0ebc6090c37677105d3b78ef427e719ddb8036
	b784419930e54435f28763e9f4cf2854a785082b9b330e7ac93c0f5590b06814)
set(_st_big_font_source "/* Generated original V100 font bytes. C code copyright (c) 2026 Stephen Hurd.\n * Embedded data copyright (c) 1987 Borland International; original notices retained. */\n#include \"st_big_font.h\"\n")
foreach(_st_big_font_index RANGE 0 3)
	list(GET _st_big_font_names ${_st_big_font_index} _st_big_font_name)
	list(GET _st_big_font_hashes ${_st_big_font_index} _st_big_font_expected)
	set(_st_big_font_path "${CMAKE_CURRENT_SOURCE_DIR}/fonts/BIG103-${_st_big_font_name}.CHR")
	set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_st_big_font_path}")
	file(SHA256 "${_st_big_font_path}" _st_big_font_actual)
	if(NOT _st_big_font_actual STREQUAL _st_big_font_expected)
		message(FATAL_ERROR "Original BIG ${_st_big_font_name} font checksum mismatch")
	endif()
	file(READ "${_st_big_font_path}" _st_big_font_hex HEX)
	string(REGEX REPLACE "(..)" "0x\\1," _st_big_font_bytes "${_st_big_font_hex}")
	string(REGEX REPLACE "(0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,)" "\\1\n" _st_big_font_bytes "${_st_big_font_bytes}")
	string(APPEND _st_big_font_source "static const uint8_t font_${_st_big_font_name}[]={\n${_st_big_font_bytes}\n};\n")
endforeach()
string(APPEND _st_big_font_source "const uint8_t *st_big_font_data(unsigned number, size_t *size)\n{\n\tif (size==NULL) return NULL;\n\t*size=0;\n\tswitch (number) {\n")
foreach(_st_big_font_index RANGE 0 3)
	list(GET _st_big_font_names ${_st_big_font_index} _st_big_font_name)
	math(EXPR _st_big_font_id "${_st_big_font_index}+1")
	string(APPEND _st_big_font_source "\tcase ${_st_big_font_id}: *size=sizeof(font_${_st_big_font_name}); return font_${_st_big_font_name};\n")
endforeach()
string(APPEND _st_big_font_source "\tdefault: return NULL;\n\t}\n}\n")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/st_big_font_data.c" CONTENT "${_st_big_font_source}" @ONLY)

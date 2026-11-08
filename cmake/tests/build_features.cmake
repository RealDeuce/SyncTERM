# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
file(READ "${BUILD_DIR}/syncterm-configuration.txt" report)
file(STRINGS "${BUILD_DIR}/syncterm-generated/st_build_options.inc" rows
	REGEX "^ST_BUILD_OPTION")
list(LENGTH rows count)
if(NOT count EQUAL 37)
	message(FATAL_ERROR "Incomplete Build Options inventory: ${count}")
endif()
foreach(item "Unifont (SyncTERM Unicode)" UNSCII "Noto Emoji" "Noto Color Emoji"
	"DeuceSSH (SSH)" "OpenSSL (TLS, symmetric ciphers)" "Botan 3 (TLS, symmetric ciphers)"
	"Encrypted BBS lists" "Operation Overkill ][ Terminal" "JPEG XL support"
	SDL "X11 Support" Xinerama XRandR XRender GDI Wayland Quartz "Win32 Console"
	ncurses ANSI libretro "Testing bitmap backend" OSS ALSA WASAPI PortAudio
	PipeWire PulseAudio "Core Audio" libsndfile WinMM "Apple DLS" "ALSA Sequencer"
	FluidSynth libADLMIDI)
	string(FIND "${report}" "${item}" present)
	if(present EQUAL -1)
		message(FATAL_ERROR "Configuration report omitted ${item}")
	endif()
endforeach()
foreach(row IN LISTS rows)
	if(NOT row MATCHES [=[^ST_BUILD_OPTION\("([^"]+)", "([^"]+)", (.+)\)$]=])
		message(FATAL_ERROR "Invalid generated Build Options row: ${row}")
	endif()
	string(FIND "${report}" "  ${CMAKE_MATCH_2}: " present)
	if(present EQUAL -1)
		message(FATAL_ERROR "Generated application row missing from report: ${row}")
	endif()
endforeach()

# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/SynctermAtomics.cmake")

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(STAudio LANGUAGES C)
	include("${CMAKE_CURRENT_LIST_DIR}/../cmake/SynctermCStandard.cmake")
	include(CTest)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/SynctermCheckC17Source.cmake")
find_package(PkgConfig QUIET)

option(ST_AUDIO_BUILD_AUDIBLE_SMOKE_TEST
	"Build the manually-run audible audio smoke test" OFF)

if(NOT TARGET st_audio_mixer)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../audio_mixer"
		"${CMAKE_CURRENT_BINARY_DIR}/audio_mixer")
endif()
function(st_audio_backend_option name default description)
	set(ST_AUDIO_${name} "${default}" CACHE STRING "${description} (AUTO, ON, or OFF)")
	set_property(CACHE ST_AUDIO_${name} PROPERTY STRINGS AUTO ON OFF)
	if(NOT ST_AUDIO_${name} STREQUAL "AUTO"
	    AND NOT ST_AUDIO_${name} STREQUAL "ON"
	    AND NOT ST_AUDIO_${name} STREQUAL "OFF")
		message(FATAL_ERROR "ST_AUDIO_${name} must be AUTO, ON, or OFF")
	endif()
endfunction()

st_audio_backend_option(ALSA AUTO "Build the runtime-loaded ALSA backend")
st_audio_backend_option(OSS AUTO "Build the native OSS backend")
st_audio_backend_option(PORTAUDIO AUTO "Build the runtime-loaded PortAudio backend")
st_audio_backend_option(PULSE_AUDIO AUTO "Build the runtime-loaded PulseAudio backend")
st_audio_backend_option(PIPEWIRE AUTO "Build the runtime-loaded PipeWire backend")
st_audio_backend_option(SDL OFF "Build the opt-in, SDL-main-thread-aware backend")
st_audio_backend_option(CORE_AUDIO AUTO "Build the system CoreAudio backend")
st_audio_backend_option(WASAPI AUTO "Build the system WASAPI backend")

set(ST_AUDIO_HAVE_ALSA 0)
set(ST_AUDIO_HAVE_OSS 0)
set(ST_AUDIO_HAVE_PORTAUDIO 0)
set(ST_AUDIO_HAVE_PULSE_AUDIO 0)
set(ST_AUDIO_HAVE_PIPEWIRE 0)
set(ST_AUDIO_HAVE_SDL 0)
set(ST_AUDIO_HAVE_CORE_AUDIO 0)
set(ST_AUDIO_HAVE_WASAPI 0)
set(_st_audio_sources audio.c audio_synthesis.c audio_backends.c
	st_audio_runtime.c)
set(_st_audio_runtime_loader FALSE)

function(st_audio_use_pkg_backend option prefix package header source variable)
	if(ST_AUDIO_${option} STREQUAL "OFF")
		set(${variable} 0 PARENT_SCOPE)
		return()
	endif()
	if(PkgConfig_FOUND)
		pkg_check_modules(${prefix} QUIET ${package})
	endif()
	if(${prefix}_FOUND)
		set(_st_audio_saved_required_includes ${CMAKE_REQUIRED_INCLUDES})
		set(CMAKE_REQUIRED_INCLUDES ${${prefix}_INCLUDE_DIRS})
		syncterm_check_c17_source("#include <${header}>\nint main(void) { return 0; }"
			ST_AUDIO_${option}_HEADERS_WORK)
		set(CMAKE_REQUIRED_INCLUDES ${_st_audio_saved_required_includes})
	endif()
	if(${prefix}_FOUND AND ST_AUDIO_${option}_HEADERS_WORK)
		set(${variable} 1 PARENT_SCOPE)
		set(_st_audio_sources ${_st_audio_sources} ${source} PARENT_SCOPE)
		set(_st_audio_runtime_loader TRUE PARENT_SCOPE)
		set(_st_audio_include_dirs ${_st_audio_include_dirs}
		    ${${prefix}_INCLUDE_DIRS} PARENT_SCOPE)
	elseif(ST_AUDIO_${option} STREQUAL "ON")
		message(FATAL_ERROR
			"ST_AUDIO_${option}=ON, but ${package} development headers were not found")
	else()
		set(${variable} 0 PARENT_SCOPE)
	endif()
endfunction()

st_audio_use_pkg_backend(ALSA ST_AUDIO_ALSA_PC alsa
	alsa/asoundlib.h audio_backend_alsa.c ST_AUDIO_HAVE_ALSA)
st_audio_use_pkg_backend(PORTAUDIO ST_AUDIO_PORTAUDIO_PC portaudio-2.0
	portaudio.h audio_backend_portaudio.c ST_AUDIO_HAVE_PORTAUDIO)
st_audio_use_pkg_backend(PULSE_AUDIO ST_AUDIO_PULSE_PC libpulse-simple
	pulse/simple.h audio_backend_pulse.c ST_AUDIO_HAVE_PULSE_AUDIO)
st_audio_use_pkg_backend(PIPEWIRE ST_AUDIO_PIPEWIRE_PC libpipewire-0.3
	pipewire/pipewire.h audio_backend_pipewire.c ST_AUDIO_HAVE_PIPEWIRE)
st_audio_use_pkg_backend(SDL ST_AUDIO_SDL_PC sdl2
	SDL.h audio_backend_sdl.c ST_AUDIO_HAVE_SDL)

set(ST_AUDIO_OSS_HEADER_SYS_SOUNDCARD 0)
set(ST_AUDIO_OSS_HEADER_MACHINE_SOUNDCARD 0)
set(ST_AUDIO_OSS_LIBRARIES)
if(NOT ST_AUDIO_OSS STREQUAL "OFF")
	syncterm_check_c17_source([=[
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>
#include <unistd.h>
int main(void)
{
	int fd = open("/dev/dsp", O_WRONLY);
	int format = AFMT_S16_NE;
	return fd < 0 ? 0 : ioctl(fd, SNDCTL_DSP_SETFMT, &format);
}
]=] ST_AUDIO_OSS_SYS_SOUNDCARD_WORKS)
	if(ST_AUDIO_OSS_SYS_SOUNDCARD_WORKS)
		set(ST_AUDIO_HAVE_OSS 1)
		set(ST_AUDIO_OSS_HEADER_SYS_SOUNDCARD 1)
	else()
		syncterm_check_c17_source([=[
#include <fcntl.h>
#include <machine/soundcard.h>
#include <sys/ioctl.h>
#include <unistd.h>
int main(void)
{
	int fd = open("/dev/dsp", O_WRONLY);
	int format = AFMT_S16_NE;
	return fd < 0 ? 0 : ioctl(fd, SNDCTL_DSP_SETFMT, &format);
}
]=] ST_AUDIO_OSS_MACHINE_SOUNDCARD_WORKS)
		if(ST_AUDIO_OSS_MACHINE_SOUNDCARD_WORKS)
			set(ST_AUDIO_HAVE_OSS 1)
			set(ST_AUDIO_OSS_HEADER_MACHINE_SOUNDCARD 1)
		endif()
	endif()
	if(NOT ST_AUDIO_HAVE_OSS)
		set(_st_audio_saved_required_libraries ${CMAKE_REQUIRED_LIBRARIES})
		set(CMAKE_REQUIRED_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES} ossaudio)
		syncterm_check_c17_source([=[
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>
#include <unistd.h>
int main(void)
{
	int fd = open("/dev/dsp", O_WRONLY);
	int format = AFMT_S16_NE;
	return fd < 0 ? 0 : ioctl(fd, SNDCTL_DSP_SETFMT, &format);
}
]=] ST_AUDIO_OSS_SYS_SOUNDCARD_WITH_OSSAUDIO_WORKS)
		set(CMAKE_REQUIRED_LIBRARIES ${_st_audio_saved_required_libraries})
		if(ST_AUDIO_OSS_SYS_SOUNDCARD_WITH_OSSAUDIO_WORKS)
			set(ST_AUDIO_HAVE_OSS 1)
			set(ST_AUDIO_OSS_HEADER_SYS_SOUNDCARD 1)
			set(ST_AUDIO_OSS_LIBRARIES ossaudio)
		endif()
	endif()
	if(ST_AUDIO_HAVE_OSS)
		list(APPEND _st_audio_sources audio_backend_oss.c)
	elseif(ST_AUDIO_OSS STREQUAL "ON")
		message(FATAL_ERROR "ST_AUDIO_OSS=ON, but a usable OSS API was not found")
	endif()
endif()

if(NOT ST_AUDIO_CORE_AUDIO STREQUAL "OFF")
	find_library(ST_AUDIO_AUDIO_TOOLBOX_FRAMEWORK AudioToolbox)
	set(_st_audio_saved_required_libraries ${CMAKE_REQUIRED_LIBRARIES})
	set(CMAKE_REQUIRED_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES}
		${ST_AUDIO_AUDIO_TOOLBOX_FRAMEWORK})
	syncterm_check_c17_source([=[
#include <AudioToolbox/AudioToolbox.h>
int main(void)
{
	AudioQueueRef queue = 0;
	return AudioQueueStart(queue, 0) == noErr;
}
]=] ST_AUDIO_CORE_AUDIO_WORKS)
	set(CMAKE_REQUIRED_LIBRARIES ${_st_audio_saved_required_libraries})
	if(ST_AUDIO_CORE_AUDIO_WORKS)
		set(ST_AUDIO_HAVE_CORE_AUDIO 1)
		list(APPEND _st_audio_sources audio_backend_coreaudio.c)
	elseif(ST_AUDIO_CORE_AUDIO STREQUAL "ON")
		message(FATAL_ERROR
			"ST_AUDIO_CORE_AUDIO=ON, but AudioToolbox is unavailable")
	endif()
endif()

if(NOT ST_AUDIO_WASAPI STREQUAL "OFF")
	set(_st_audio_saved_required_libraries ${CMAKE_REQUIRED_LIBRARIES})
	set(CMAKE_REQUIRED_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES}
		avrt ole32 uuid)
	syncterm_check_c17_source([=[
#define COBJMACROS
#include <initguid.h>
#include <audioclient.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#include <windows.h>
int main(void)
{
	IMMDeviceEnumerator *enumerator = 0;
	DWORD index = 0;
	(void)AvSetMmThreadCharacteristicsW(L"Audio", &index);
	int flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK
	    | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
	    | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
	(void)flags;
	return CoCreateInstance(&CLSID_MMDeviceEnumerator, 0,
	    CLSCTX_INPROC_SERVER, &IID_IMMDeviceEnumerator,
	    (void **)&enumerator) == S_OK ? 0 : 0;
}
]=] ST_AUDIO_WASAPI_WORKS)
	set(CMAKE_REQUIRED_LIBRARIES ${_st_audio_saved_required_libraries})
	if(ST_AUDIO_WASAPI_WORKS)
		set(ST_AUDIO_HAVE_WASAPI 1)
		list(APPEND _st_audio_sources audio_backend_wasapi.c)
	elseif(ST_AUDIO_WASAPI STREQUAL "ON")
		message(FATAL_ERROR "ST_AUDIO_WASAPI=ON, but WASAPI is unavailable")
	endif()
endif()

set(ST_AUDIO_HAVE_SPEAKER_TONE 0)
set(ST_AUDIO_SPEAKER_HEADER_MACHINE_SPEAKER 0)
set(ST_AUDIO_HAVE_KIOCSOUND 0)
set(ST_AUDIO_KIOCSOUND_HEADER_LINUX_KD 0)
if(WIN32)
	list(APPEND _st_audio_sources audio_beep_windows.c)
else()
	syncterm_check_c17_source([=[
#include <machine/speaker.h>
#include <sys/ioctl.h>
int main(void) { tone_t tone = { 440, 10 }; return SPKRTONE + tone.frequency; }
]=] ST_AUDIO_MACHINE_SPEAKER_WORKS)
	if(ST_AUDIO_MACHINE_SPEAKER_WORKS)
		set(ST_AUDIO_HAVE_SPEAKER_TONE 1)
		set(ST_AUDIO_SPEAKER_HEADER_MACHINE_SPEAKER 1)
	else()
		syncterm_check_c17_source([=[
#include <machine/spkr.h>
#include <sys/ioctl.h>
int main(void) { tone_t tone = { 440, 10 }; return SPKRTONE + tone.frequency; }
]=] ST_AUDIO_MACHINE_SPKR_WORKS)
		if(ST_AUDIO_MACHINE_SPKR_WORKS)
			set(ST_AUDIO_HAVE_SPEAKER_TONE 1)
		endif()
	endif()
	syncterm_check_c17_source([=[
#include <sys/kbio.h>
#include <sys/ioctl.h>
int main(void) { return KIOCSOUND; }
]=] ST_AUDIO_KIOCSOUND_WORKS)
	if(ST_AUDIO_KIOCSOUND_WORKS)
		set(ST_AUDIO_HAVE_KIOCSOUND 1)
	else()
		syncterm_check_c17_source([=[
#include <linux/kd.h>
#include <sys/ioctl.h>
int main(void) { return KIOCSOUND; }
]=] ST_AUDIO_LINUX_KIOCSOUND_WORKS)
		if(ST_AUDIO_LINUX_KIOCSOUND_WORKS)
			set(ST_AUDIO_HAVE_KIOCSOUND 1)
			set(ST_AUDIO_KIOCSOUND_HEADER_LINUX_KD 1)
		endif()
	endif()
	list(APPEND _st_audio_sources audio_beep_posix.c)
endif()

configure_file(audio_config.h.in audio_config.h @ONLY)

if(_st_audio_runtime_loader AND NOT TARGET syncterm_dynamic_library)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../dynamic_library"
		"${CMAKE_CURRENT_BINARY_DIR}/dynamic_library")
endif()

add_library(st_audio STATIC ${_st_audio_sources})
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
	set_source_files_properties(
		audio_backend_alsa.c audio_backend_portaudio.c
		audio_backend_pulse.c audio_backend_pipewire.c audio_backend_sdl.c
		PROPERTIES COMPILE_OPTIONS -Wno-pedantic)
endif()
target_include_directories(st_audio
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
	PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(st_audio SYSTEM PRIVATE ${_st_audio_include_dirs})
target_link_libraries(st_audio PUBLIC st_audio_mixer syncterm_io_error)
if(_st_audio_runtime_loader)
	target_link_libraries(st_audio PRIVATE syncterm_dynamic_library)
endif()
if(ST_AUDIO_HAVE_CORE_AUDIO)
	target_link_libraries(st_audio PRIVATE ${ST_AUDIO_AUDIO_TOOLBOX_FRAMEWORK})
endif()
if(ST_AUDIO_HAVE_WASAPI)
	target_link_libraries(st_audio PRIVATE avrt ole32)
endif()
if(ST_AUDIO_OSS_LIBRARIES)
	target_link_libraries(st_audio PRIVATE ${ST_AUDIO_OSS_LIBRARIES})
endif()
if(ST_AUDIO_MIXER_MATH_IN_LIBM)
	target_link_libraries(st_audio PRIVATE m)
endif()
syncterm_target_atomics(st_audio)
set_target_properties(st_audio PROPERTIES
	C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)

function(st_audio_warnings target)
	if(MSVC)
		target_compile_options(${target} PRIVATE /W4 /WX)
	else()
		target_compile_options(${target} PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
endfunction()
st_audio_warnings(st_audio)

if(BUILD_TESTING)
	target_compile_definitions(st_audio PRIVATE ST_AUDIO_TESTING=1)
	add_executable(st_audio_test tests/audio_test.c)
	target_include_directories(st_audio_test PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR})
	target_compile_definitions(st_audio_test PRIVATE ST_AUDIO_TESTING=1)
	target_link_libraries(st_audio_test PRIVATE st_audio)
	syncterm_target_atomics(st_audio_test)
	set_target_properties(st_audio_test PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	st_audio_warnings(st_audio_test)
	add_test(NAME st_audio COMMAND st_audio_test)
	set_tests_properties(st_audio PROPERTIES TIMEOUT 20)
endif()

if(ST_AUDIO_BUILD_AUDIBLE_SMOKE_TEST)
	add_executable(st_audio_audible_smoke tests/audio_audible_smoke.c)
	target_link_libraries(st_audio_audible_smoke PRIVATE st_audio)
	set_target_properties(st_audio_audible_smoke PROPERTIES
		C_STANDARD ${SYNCTERM_C_STANDARD} C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	st_audio_warnings(st_audio_audible_smoke)
endif()

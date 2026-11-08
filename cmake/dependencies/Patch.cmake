# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)
# Keep pinned sources buildable on supported hosts and honor the selected
# client-library configuration.
if(NAME STREQUAL "BOTAN")
	# Botan 3.14.1 uses std::ranges::equal without its declaring header.
	# Some standard libraries supply it transitively; MSVC's does not.
	set(path "${SOURCE}/src/lib/x509/ocsp.cpp")
	file(READ "${path}" text)
	if(NOT text MATCHES "#include <algorithm>")
		if(NOT text MATCHES "#include <botan/ocsp.h>")
			message(FATAL_ERROR "Pinned Botan OCSP header patch no longer applies")
		endif()
		string(REPLACE "#include <botan/ocsp.h>" "#include <botan/ocsp.h>\n#include <algorithm>" text "${text}")
		file(WRITE "${path}" "${text}")
	endif()
	# MinGW also runs under Windows' command-line length limit. Upstream
	# enables archive response files only for its separately named windows OS.
	set(path "${SOURCE}/configure.py")
	file(READ "${path}" text)
	set(original "${text}")
	foreach(rule cmdline_args response_file)
		set(operator "!=")
		set(replacement "not in")
		if(rule STREQUAL "response_file")
			set(operator "==")
			set(replacement "in")
		endif()
		set(before "'build_static_lib_using_${rule}': options.build_static_lib and osinfo.basename ${operator} 'windows'")
		set(after "'build_static_lib_using_${rule}': options.build_static_lib and osinfo.basename ${replacement} ['windows', 'mingw']")
		string(FIND "${text}" "${after}" patched)
		if(patched EQUAL -1)
			string(FIND "${text}" "${before}" unpatched)
			if(unpatched EQUAL -1)
				message(FATAL_ERROR "Pinned Botan archive response-file patch no longer applies: ${rule}")
			endif()
			string(REPLACE "${before}" "${after}" text "${text}")
		endif()
	endforeach()
	if(NOT text STREQUAL original)
		file(WRITE "${path}" "${text}")
	endif()
	set(path "${SOURCE}/src/build-data/ninja.in")
	file(READ "${path}" text)
	set(before "@%{response_file_dir}/static.txt")
	set(after "\"@%{response_file_dir}/static.txt\"")
	string(FIND "${text}" "${after}" patched)
	if(patched EQUAL -1)
		string(FIND "${text}" "${before}" unpatched)
		if(unpatched EQUAL -1)
			message(FATAL_ERROR "Pinned Botan archive response-file quoting patch no longer applies")
		endif()
		string(REPLACE "${before}" "${after}" text "${text}")
		file(WRITE "${path}" "${text}")
	endif()
elseif(NAME STREQUAL "PULSEAUDIO")
	foreach(relative src/meson.build src/pulse/meson.build)
		file(READ "${SOURCE}/${relative}" text)
		if(MODE STREQUAL "STATIC")
			string(REPLACE "shared_library(" "library(" text "${text}")
		endif()
		if(relative STREQUAL "src/meson.build")
			string(REPLACE "subdir('utils')" "# Client libraries only." text "${text}")
		endif()
		file(WRITE "${SOURCE}/${relative}" "${text}")
	endforeach()
elseif(NAME STREQUAL "PIPEWIRE")
	file(READ "${SOURCE}/src/modules/network-utils.h" network)
	if(NOT network MATCHES "#include <netinet/in.h>")
		string(REPLACE "#include <netdb.h>" "#include <sys/socket.h>\n#include <netinet/in.h>\n#include <netdb.h>" network "${network}")
		file(WRITE "${SOURCE}/src/modules/network-utils.h" "${network}")
	endif()
	file(READ "${SOURCE}/src/pipewire/thread.c" thread)
	if(NOT thread MATCHES "#ifdef SCHED_RESET_ON_FORK")
		string(REPLACE "SPA_FLAG_UPDATE(new_policy, SCHED_RESET_ON_FORK, reset_on_fork);"
			"/* Optional scheduler policy bit. */\n#ifdef SCHED_RESET_ON_FORK\n\tSPA_FLAG_UPDATE(new_policy, SCHED_RESET_ON_FORK, reset_on_fork);\n#else\n\t(void)reset_on_fork;\n#endif" thread "${thread}")
		file(WRITE "${SOURCE}/src/pipewire/thread.c" "${thread}")
	endif()
	if(NOT HAVE_EBADFD)
		file(READ "${SOURCE}/src/pipewire/pipewire.c" init)
		# This error denotes pw_init() not having been called, rather than
		# an actual file descriptor. Use the portable invalid-argument result.
		string(REPLACE "errno = EBADFD;" "errno = EINVAL;" init "${init}")
		file(WRITE "${SOURCE}/src/pipewire/pipewire.c" "${init}")
	endif()
	file(READ "${SOURCE}/src/pipewire/context.c" context)
	if(NOT context MATCHES "SyncTERM: available resource limits")
		# The upstream table names Linux-only limits unconditionally. Guard
		# each declaration by capability instead of selecting an OS family.
		string(REGEX REPLACE "(\t\t)\\[(RLIMIT_[A-Z_]+)\\]([^\n]*)"
			"#ifdef \\2\n\\1[\\2]\\3\n#endif" context "${context}")
		string(REPLACE "static int adjust_rlimits(" "/* SyncTERM: available resource limits. */\nstatic int adjust_rlimits(" context "${context}")
		file(WRITE "${SOURCE}/src/pipewire/context.c" "${context}")
	endif()
	if(NOT HAVE_ECHRNG)
		# This pinned header uses a non-POSIX errno for an invalid channel
		# range. Keep the range failure on targets which lack that errno.
		file(GLOB audio_headers "${SOURCE}/spa/include/spa/param/audio/*.h")
		foreach(header IN LISTS audio_headers)
			file(READ "${header}" layout)
			string(REPLACE "-ECHRNG" "-ERANGE" layout "${layout}")
			file(WRITE "${header}" "${layout}")
		endforeach()
	endif()
	# Keep the matching protocol/SPA modules and configuration the client
	# requires. A library-only build cannot create a functional audio stream.
	file(READ "${SOURCE}/src/modules/meson.build" original)
	set(modules "subdir('module-rt')\nmodule_sources = []\npipewire_module_protocol_deps = [mathlib, dl_lib, pipewire_dep]\n")
	foreach(module rt protocol_native client_node metadata adapter)
		string(FIND "${original}" "pipewire_module_${module} = shared_library(" start)
		if(start EQUAL -1)
			message(FATAL_ERROR "Pinned PipeWire client module patch no longer applies: ${module}")
		endif()
		string(SUBSTRING "${original}" ${start} -1 remainder)
		string(FIND "${remainder}" "\n)" end)
		if(end EQUAL -1)
			message(FATAL_ERROR "Unterminated PipeWire module ${module}")
		endif()
		math(EXPR length "${end} + 2")
		string(SUBSTRING "${remainder}" 0 ${length} definition)
		string(APPEND modules "${definition}\n")
	endforeach()
	file(WRITE "${SOURCE}/src/modules/meson.build" "${modules}")
	file(WRITE "${SOURCE}/src/meson.build" "subdir('pipewire')\nsubdir('modules')\nsubdir('daemon')\n")
	file(READ "${SOURCE}/spa/meson.build" spa)
	string(REPLACE "subdir('tools')" "# SyncTERM builds the SPA client plugins only." spa "${spa}")
	file(WRITE "${SOURCE}/spa/meson.build" "${spa}")
	file(WRITE "${SOURCE}/spa/plugins/meson.build"
		"subdir('support')\nsubdir('audioconvert')\n")
	file(WRITE "${SOURCE}/src/daemon/meson.build" [=[
configure_file(input: 'client.conf.in', output: 'client.conf',
  configuration: configuration_data(), install_dir: pipewire_confdatadir)
]=])
	file(WRITE "${SOURCE}/src/daemon/client.conf.in" [=[
context.spa-libs = {
    audio.convert.* = audioconvert/libspa-audioconvert
    support.* = support/libspa-support
}
context.modules = [
    { name = libpipewire-module-rt flags = [ ifexists nofail ] }
    { name = libpipewire-module-protocol-native }
    { name = libpipewire-module-client-node }
    { name = libpipewire-module-adapter }
    { name = libpipewire-module-metadata }
]
]=])
endif()

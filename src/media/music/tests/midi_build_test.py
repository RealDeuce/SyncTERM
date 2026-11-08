#!/usr/bin/env python3
"""Exercise real archive/closure selection without installed MIDI libraries.

The mock public ABI comes from the adapter's symbol list. This tests CMake
policy and linking, not third-party ABI correctness or synthesized sound.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import os
from pathlib import Path
import re
import shutil
import subprocess
import struct
import sys
import tempfile

cmake, source, compiler = sys.argv[1:4]
source = Path(source)
if not shutil.which("pkg-config") or not shutil.which("c++") or os.name == "nt":
    sys.exit(77)


def run(*args, expected=True, env=None):
    result = subprocess.run([str(x) for x in args], stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, env=env)
    if (result.returncode == 0) != expected:
        raise AssertionError(" ".join(str(x) for x in args) + "\n" + result.stdout)
    return result.stdout


with tempfile.TemporaryDirectory(prefix="syncterm-midi-cmake-") as directory:
    root = Path(directory)
    prefix, provider, consumer = root / "prefix", root / "provider", root / "consumer"
    for path in (prefix / "include/alsa", prefix / "lib/pkgconfig", provider, consumer):
        path.mkdir(parents=True, exist_ok=True)
    specs = [
        ("ALSA", "alsa", "asound", "alsa/asoundlib.h", "alsa"),
        ("FLUIDSYNTH", "fluidsynth", "fluidsynth", "fluidsynth.h", "fluid"),
        ("ADLMIDI", "libADLMIDI", "ADLMIDI", "adlmidi.h", "adl"),
    ]
    types = "#include <stddef.h>\n#include <stdint.h>\n"
    for name in ("snd_seq", "snd_seq_addr", "snd_seq_event", "snd_midi_event",
                 "fluid_settings", "fluid_synth", "fluid_cmd_handler", "fluid_midi_router", "fluid_player"):
        types += f"typedef struct {name} {name}_t;\n"
    types += "struct ADL_MIDIPlayer; typedef uint8_t ADL_UInt8; typedef uint16_t ADL_UInt16;\n"
    providers_cmake = "cmake_minimum_required(VERSION 3.22)\nproject(MidiProviders LANGUAGES C CXX)\n"
    providers_cmake += f'set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "{prefix.as_posix()}/lib")\n'
    providers_cmake += f'set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "{prefix.as_posix()}/lib")\n'
    providers_cmake += "set(CMAKE_POSITION_INDEPENDENT_CODE ON)\nadd_library(midi_private STATIC private.c)\nadd_library(midi_cpp STATIC private.cpp)\n"
    (provider / "private.c").write_text("int midi_fixture_dependency(void) { return 1; }\n")
    (provider / "private.cpp").write_text('#include <string>\nextern "C" int midi_fixture_cpp_dependency(void) { std::string s(1000, \'x\'); return (int)s.size(); }\n')
    for name, package, library, header, symbols in specs:
        dependency = "midi_fixture_dependency" if name == "ALSA" else "midi_fixture_cpp_dependency"
        private = "midi_private" if name == "ALSA" else "midi_cpp"
        declarations, definitions = [], []
        text = (source / f"{symbols}_symbols.h").read_text()
        for result, function, arguments in re.findall(r"\bX\(([^,]+),\s*(\w+),\s*\((.*?)\)\)", text):
            result = result.strip()
            declarations.append(f"{result} {function}({arguments});")
            parameters, discard = [], []
            if arguments != "void":
                for i, arg in enumerate(arguments.split(",")):
                    parameters.append(f"{arg.strip()} a{i}")
                    discard.append(f"(void)a{i};")
            returned = "return;" if result == "void" else f"return ({result})0;"
            definitions.append(f"{result} {function}({', '.join(parameters) or 'void'}) {{ {' '.join(discard)} (void){dependency}(); {returned} }}")
        (prefix / "include" / header).write_text(types + '\n#ifdef __cplusplus\nextern "C" {\n#endif\n' + "\n".join(declarations) + "\n#ifdef __cplusplus\n}\n#endif\n")
        (provider / f"{library}.c").write_text(f'#include <{header}>\nint {dependency}(void);\n' + "\n".join(definitions))
        providers_cmake += f"add_library({library} STATIC {library}.c)\ntarget_include_directories({library} PRIVATE \"{prefix.as_posix()}/include\")\nset_target_properties({library} PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)\n"
        providers_cmake += f'''add_library({library}_shared SHARED {library}.c)
target_include_directories({library}_shared PRIVATE "{prefix.as_posix()}/include")
target_link_libraries({library}_shared PRIVATE {private})
set_target_properties({library}_shared PROPERTIES OUTPUT_NAME {library} SUFFIX ".so"
 C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
'''
        (prefix / f"lib/pkgconfig/{package}.pc").write_text(f"prefix={prefix}\nlibdir=${{prefix}}/lib\nincludedir=${{prefix}}/include\nName: {package}\nDescription: SyncTERM build test fixture\nVersion: 99.0\nLibs: -L${{libdir}} -l{library}\nLibs.private: -l{private}\nCflags: -I${{includedir}}\n")
    (provider / "CMakeLists.txt").write_text(providers_cmake)
    run(cmake, "-S", provider, "-B", root / "providers-build", f"-DCMAKE_C_COMPILER={compiler}")
    run(cmake, "--build", root / "providers-build", "-j1")
    (consumer / "main.c").write_text('''#include "music_config.h"
#include <alsa/asoundlib.h>
#include <fluidsynth.h>
#include <adlmidi.h>
int midi_fixture(void) {
    int value = 0;
#if ST_MIDI_LINK_ALSA == 2 || ST_MIDI_LINK_ALSA == 4
    value += snd_seq_close(0);
#endif
#if ST_MIDI_LINK_FLUIDSYNTH == 2 || ST_MIDI_LINK_FLUIDSYNTH == 4
    value += fluid_synth_sfcount(0);
#endif
#if ST_MIDI_LINK_ADLMIDI == 2 || ST_MIDI_LINK_ADLMIDI == 4
    value += adl_getBanksCount();
#endif
    return value;
}
''')
    (consumer / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.22)
project(MidiConsumer LANGUAGES C)
# Exercise the POSIX routing policy with mock headers on any native host.
set(APPLE FALSE)
set(WIN32 FALSE)
set(SYNCTERM_C_STANDARD 17)
include("{(source / '../../../cmake/SynctermCheckC17Source.cmake').resolve().as_posix()}")
include("{(source / 'MidiBackends.cmake').as_posix()}")
configure_file("{(source / 'music_config.h.in').as_posix()}" music_config.h @ONLY)
if(DEFINED ARCHIVE_TEST_PATH)
    syncterm_is_static_archive("${{ARCHIVE_TEST_PATH}}" valid)
    file(WRITE "${{CMAKE_BINARY_DIR}}/archive.txt" "${{valid}}")
endif()
file(WRITE "${{CMAKE_BINARY_DIR}}/modes.txt" "${{ST_MIDI_LINK_ALSA}};${{ST_MIDI_LINK_FLUIDSYNTH}};${{ST_MIDI_LINK_ADLMIDI}}")
add_library(consumer MODULE main.c)
target_include_directories(consumer PRIVATE "${{CMAKE_BINARY_DIR}}" "{prefix.as_posix()}/include")
target_link_libraries(consumer PRIVATE ${{_st_midi_deps}})
set_target_properties(consumer PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
''')
    env = dict(os.environ, PKG_CONFIG_PATH="", PKG_CONFIG_LIBDIR=str(prefix / "lib/pkgconfig"))

    def configure(label, modes, expected=None, extra=()):
        build = root / label
        args = [cmake, "-S", consumer, "-B", build, f"-DCMAKE_C_COMPILER={compiler}",
                f"-DCMAKE_PREFIX_PATH={prefix}", "-DST_MIDI_WINMM=OFF", "-DST_MIDI_APPLE=OFF"]
        args += [f"-DST_MIDI_{row[0]}={'OFF' if mode == 'OFF' else 'AUTO' if mode == 'AUTO' else 'ON'}" for row, mode in zip(specs, modes)]
        args += [f"-DST_DEP_{row[0]}_LINKAGE={mode if mode in ('RUNTIME', 'STATIC', 'SHARED') else 'AUTO'}" for row, mode in zip(specs, modes)]
        args += [f"-DST_DEP_{row[0]}_SOURCE=INSTALLED" for row in specs]
        args += [f"-DST_DEP_{row[0]}_INCLUDE_DIR={prefix / 'include'}" for row in specs]
        output = run(*args, *extra, expected=expected is not None, env=env)
        if expected is not None:
            assert (build / "modes.txt").read_text() == expected, (label, output)
            run(cmake, "--build", build, "-j1", env=env)
        print(label, "passed", flush=True)
        return build

    configure("auto", ["AUTO"] * 3, "1;1;1")
    configure("runtime", ["RUNTIME"] * 3, "1;1;1")
    configure("static", ["STATIC"] * 3, "2;2;2")
    configure("shared", ["SHARED"] * 3, "4;4;4")
    configure("all-linkages", ["SHARED", "STATIC", "RUNTIME"], "4;2;1")
    configure("mixed", ["RUNTIME", "STATIC", "OFF"], "1;2;0")
    configure("off", ["OFF"] * 3, "0;0;0")
    bad = root / "bad-headers"
    for _, _, _, header, _ in specs:
        path = bad / header
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("#error missing required public MIDI API\n")
    missing = [f"-DST_DEP_{row[0]}_INCLUDE_DIR={bad}" for row in specs]
    configure("auto-explicit-bad-headers", ["AUTO"] * 3, extra=missing)
    configure("runtime-missing", ["RUNTIME", "OFF", "OFF"], extra=missing)
    configure("archive-missing", ["STATIC", "OFF", "OFF"], extra=[f"-DST_DEP_ALSA_LIBRARIES={root / 'missing.a'}"])
    shared = root / "shared.so"
    shared.write_bytes(b"\x7fELFnot-an-archive")
    configure("shared-rejected", ["STATIC", "OFF", "OFF"], extra=[f"-DST_DEP_ALSA_LIBRARIES={shared}"])
    pc = prefix / "lib/pkgconfig/alsa.pc"
    pc.write_text(pc.read_text().replace("-lmidi_private", ""))
    configure("closure-missing", ["STATIC", "OFF", "OFF"])

    raw = next((prefix / "lib").glob("libasound.a")).read_bytes()
    for wide in (False, True):
        header = (struct.pack(">IIiiQQII", 0xcafebabf, 1, 0x01000007, 3, 40, len(raw), 0, 0)
                  if wide else struct.pack(">IIiiIII", 0xcafebabe, 1, 7, 3, 28, len(raw), 0))
        for static in (False, True):
            fat = root / (f"fat-{wide}-{static}.a")
            fat.write_bytes(header + (raw if static else b"\x7fELF-not-an-archive"))
            build = configure(f"fat-{wide}-{static}", ["OFF"] * 3, "0;0;0",
                              [f"-DARCHIVE_TEST_PATH={fat}"])
            assert (build / "archive.txt").read_text() == ("TRUE" if static else "FALSE")

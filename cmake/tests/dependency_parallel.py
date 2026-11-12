#!/usr/bin/env python3
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
"""Measure workers in real nested dependency builds, including shared budgets."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import time


if sys.argv[1] == '--worker':
    root = Path(sys.argv[2])
    guard = root / 'guard'
    marker = root / f'{os.getpid()}.active'

    def lock():
        deadline = time.monotonic() + 30
        while True:
            try:
                guard.mkdir()
                return
            except FileExistsError:
                if time.monotonic() > deadline:
                    raise RuntimeError('Timed out waiting for worker accounting')
                time.sleep(0.01)

    lock()
    try:
        marker.touch()
        count = len(list(root.glob('*.active')))
        maximum = root / 'maximum'
        previous = int(maximum.read_text()) if maximum.exists() else 0
        maximum.write_text(str(max(count, previous)))
    finally:
        guard.rmdir()
    time.sleep(0.3)
    lock()
    try:
        marker.unlink()
    finally:
        guard.rmdir()
    if len(sys.argv) > 3:
        Path(sys.argv[3]).touch()
    sys.exit(0)


cmake = sys.argv[1]
root = Path(sys.argv[2]).resolve()
modules = Path(__file__).resolve().parent.parent
worker = Path(__file__).resolve()
root.mkdir(parents=True, exist_ok=True)
shutil.copytree(modules, root / 'modules', dirs_exist_ok=True)
recipes = root / 'modules/dependencies/Recipes.cmake'
recipes.write_text(recipes.read_text().replace(
    'if(name STREQUAL "BOTAN")', '''if(name MATCHES "^(FIXTURE|SECOND)$")
set(package fixture)
set(header fixture.h)
set(libs fixture)
elseif(name STREQUAL "BOTAN")''', 1))
source = root / 'package'
source.mkdir(exist_ok=True)
(source / 'fixture.h').write_text('int fixture(void);\n')
(source / 'fixture.c').write_text('int fixture(void) { return 0; }\n')
(source / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.22)
project(Fixture C)
add_library(fixture fixture.c)
set_target_properties(fixture PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
foreach(i RANGE 1 6)
  add_custom_target(worker_${i} ALL COMMAND "${WORKER_PYTHON}" "${WORKER_SCRIPT}"
    --worker "${WORKER_ROOT}" VERBATIM)
endforeach()
install(TARGETS fixture ARCHIVE DESTINATION lib)
install(FILES fixture.h DESTINATION include)
''')
archive = root / 'fixture.tar'
with tarfile.open(archive, 'w') as package:
    for path in source.iterdir():
        package.add(path, arcname=path.name)
digest = hashlib.sha256(archive.read_bytes()).hexdigest()
(root / 'modules/dependencies/lock.json').write_text(json.dumps({'dependencies': {
    name: {'version': '1', 'url': str(archive), 'sha256': digest}
    for name in ('FIXTURE', 'SECOND')}}))
parent = root / 'parent'
parent.mkdir(exist_ok=True)
(parent / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.22)
project(Parallel C)
include("${MODULES}/SynctermDependencies.cmake")
foreach(name FIXTURE SECOND)
  set(ST_DEP_${name}_SOURCE BUNDLED CACHE STRING "")
  set(ST_DEP_${name}_LINKAGE STATIC CACHE STRING "")
  syncterm_dependency(NAME ${name} HEADER fixture.h LIBRARIES fixture
    DEFAULT STATIC REQUIRED PROBE "int main(void) { return 0; }")
endforeach()
add_custom_target(check ALL DEPENDS st_build_fixture st_build_second)
''')
# The real recipe passes these options to each dependency's configure step.
text = recipes.read_text().replace('set(package fixture)', '''set(options
  "-DWORKER_PYTHON=${WORKER_PYTHON}" "-DWORKER_SCRIPT=${WORKER_SCRIPT}"
  "-DWORKER_ROOT=${WORKER_ROOT}")
set(package fixture)''', 1)
recipes.write_text(text)
environment = dict(os.environ)
environment.pop('MAKEFLAGS', None)
environment.pop('CMAKE_BUILD_PARALLEL_LEVEL', None)
environment.pop('CMAKE_GENERATOR', None)


def run(label, args, env, success=True):
    result = subprocess.run(args, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (root / f'{label}.log').write_bytes(result.stdout)
    if (result.returncode == 0) != success:
        raise RuntimeError(f'{label} failed:\n{result.stdout.decode(errors="replace")}')
    if b'jobserver unavailable' in result.stdout or b'-j0 forced' in result.stdout:
        raise RuntimeError(f'{label} lost the jobserver')
    return result.stdout


def check(label, generator, outer_jobs, jobs, expected, shared=True, make=None,
          makeflags=None):
    build = root / label
    if build.exists():
        shutil.rmtree(build)
    workers = root / f'{label}-workers'
    if workers.exists():
        shutil.rmtree(workers)
    workers.mkdir()
    (workers / 'maximum').write_text('0')
    env = dict(environment)
    if jobs is not None:
        env['CMAKE_BUILD_PARALLEL_LEVEL'] = jobs
    if makeflags is not None:
        env['MAKEFLAGS'] = makeflags
    args = [cmake, '-S', str(parent), '-B', str(build), '-G', generator,
            f'-DMODULES={root / "modules"}', f'-DWORKER_PYTHON={sys.executable}',
            f'-DWORKER_SCRIPT={worker}', f'-DWORKER_ROOT={workers}',
            f'-DSYNCTERM_DEPENDENCY_CACHE={root / "cache"}']
    if make:
        args.append(f'-DCMAKE_MAKE_PROGRAM={make}')
    run(f'{label}-configure', args, environment)
    if not shared:
        # Exercise the fallback used by CMake < 3.28 as well as non-Make generators.
        for settings in (build / 'dependencies/build').glob('*/settings/*.cmake'):
            settings.write_text(settings.read_text().replace(
                'set(dep_job_server [==[TRUE]==])', 'set(dep_job_server [==[FALSE]==])'))
    output = run(f'{label}-build', [cmake, '--build', str(build), '--config', 'Debug',
                                  '--parallel', str(outer_jobs)], env, success=expected is not None)
    if expected is None:
        if b'CMAKE_BUILD_PARALLEL_LEVEL must be a positive integer' not in output:
            raise RuntimeError(f'{label}: invalid limit did not fail with the expected diagnostic')
        return
    maximum = int((workers / 'maximum').read_text())
    if maximum != expected:
        raise RuntimeError(f'{label}: expected {expected} concurrent workers, observed {maximum}')
    # ExternalProject logs nested output, so inspect those logs for lost budgets too.
    for log in build.glob('dependencies/build/*/external/src/*-stamp/*-build-*.log'):
        data = log.read_bytes()
        if b'jobserver unavailable' in data or b'forced in submake' in data:
            raise RuntimeError(f'{label}: lost jobserver in {log}')


make = shutil.which('gmake') or shutil.which('make')
version = subprocess.run([cmake, '--version'], capture_output=True, text=True, check=True).stdout
cmake_version = tuple(map(int, version.split()[2].split('.')[:2]))
if make and 'GNU Make' in subprocess.run([make, '--version'], capture_output=True, text=True).stdout:
    if cmake_version >= (3, 28):
        # An environment limit must not replace the parent's shared -j2 budget.
        check('make-shared', 'Unix Makefiles', 2, '5', 2, make=make)
        make_version = subprocess.run([make, '--version'], capture_output=True, text=True).stdout
        if tuple(map(int, make_version.split()[2].split('.')[:2])) >= (4, 4):
            check('make-pipe', 'Unix Makefiles', 2, '5', 2, make=make,
                  makeflags='--jobserver-style=pipe')
    check('make-fallback', 'Unix Makefiles', 1, '3', 3, shared=False, make=make)
    check('make-default', 'Unix Makefiles', 1, None, 1, shared=False, make=make)
    check('make-empty', 'Unix Makefiles', 1, '', 1, shared=False, make=make)
if shutil.which('ninja'):
    check('ninja-explicit', 'Ninja', 1, '3', 3)
    check('ninja-default', 'Ninja', 1, None, 1)
    check('ninja-invalid-zero', 'Ninja', 1, '0', None)
    check('ninja-invalid-text', 'Ninja', 1, 'invalid', None)


def check_foreign(backend, generator, shared, jobs, expected):
    label = f'{backend}-shared' if shared else f'{backend}-fallback'
    directory = root / label
    if directory.exists():
        shutil.rmtree(directory)
    directory.mkdir()
    workers = directory / 'workers'
    workers.mkdir()
    child = directory / 'child'
    child.mkdir()
    # A CMake-generated child gives the foreign recipe a real Make/Ninja build.
    (child / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.22)
project(Workers NONE)
foreach(i RANGE 1 6)
  add_custom_target(worker_${i} ALL COMMAND "${WORKER_PYTHON}" "${WORKER_SCRIPT}"
    --worker "${WORKER_ROOT}" VERBATIM)
endforeach()
''')
    if backend == 'meson':
        def meson_string(value):
            return str(value).replace('\\', '/').replace("'", "\\'")

        (child / 'meson.build').write_text(
            "project('workers')\nforeach i : range(6)\n"
            "custom_target('worker' + i.to_string(), output: 'worker' + i.to_string(),\n"
            f"command: ['{meson_string(sys.executable)}', '{meson_string(worker)}', "
            f"'--worker', '{meson_string(workers)}', '@OUTPUT@'],\n"
            "build_by_default: true)\nendforeach\n")
        run(f'{label}-child', ['meson', 'setup', str(directory / 'work'), str(child)], environment)
    else:
        run(f'{label}-child', [cmake, '-S', str(child), '-B', str(directory / 'work'),
                             '-G', generator, f'-DWORKER_PYTHON={sys.executable}',
                             f'-DWORKER_SCRIPT={worker}', f'-DWORKER_ROOT={workers}'], environment)
    settings = directory / 'settings.cmake'
    settings.write_text(f'set(kind {backend})\nset(dep_job_server {"TRUE" if shared else "FALSE"})\n')
    parent = directory / 'parent'
    parent.mkdir()
    (parent / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.22)
project(Outer NONE)
set(aware)
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
  set(aware JOB_SERVER_AWARE TRUE)
endif()
add_custom_target(foreign ALL COMMAND "${CMAKE_COMMAND}" "-DSETTINGS=${SETTINGS}"
  "-DWORK=${WORK}" -DSTEP=build -P "${BUILD_SCRIPT}" ${aware} VERBATIM)
''')
    run(f'{label}-parent', [cmake, '-S', str(parent), '-B', str(directory / 'outer'),
                          '-G', 'Unix Makefiles', f'-DCMAKE_MAKE_PROGRAM={make}',
                          f'-DSETTINGS={settings}', f'-DWORK={directory / "work"}',
                          f'-DBUILD_SCRIPT={modules / "dependencies/Build.cmake"}'], environment)
    env = dict(environment, CMAKE_BUILD_PARALLEL_LEVEL=jobs)
    run(f'{label}-build', [cmake, '--build', str(directory / 'outer'), '--parallel', '2'], env)
    maximum = int((workers / 'maximum').read_text())
    if maximum != expected:
        raise RuntimeError(f'{label}: expected {expected} concurrent workers, observed {maximum}')


if make and 'GNU Make' in subprocess.run([make, '--version'], capture_output=True, text=True).stdout:
    for backend in ('autotools', 'openssl'):
        if backend == 'openssl' and not shutil.which('perl'):
            continue
        check_foreign(backend, 'Unix Makefiles', False, '3', 3)
        if cmake_version >= (3, 28):
            check_foreign(backend, 'Unix Makefiles', True, '5', 2)
    if shutil.which('ninja') and shutil.which('python3'):
        check_foreign('botan', 'Ninja', False, '3', 3)
        ninja_version = subprocess.run(['ninja', '--version'], capture_output=True, text=True).stdout
        make_version = subprocess.run([make, '--version'], capture_output=True, text=True).stdout
        if (cmake_version >= (3, 28)
                and tuple(map(int, make_version.split()[2].split('.')[:2])) >= (4, 4)
                and tuple(map(int, ninja_version.strip().split('.')[:2])) >= (1, 13)):
            check_foreign('botan', 'Ninja', True, '5', 2)
            if shutil.which('meson'):
                check_foreign('meson', 'Ninja', True, '5', 2)
        if shutil.which('meson'):
            check_foreign('meson', 'Ninja', False, '3', 3)

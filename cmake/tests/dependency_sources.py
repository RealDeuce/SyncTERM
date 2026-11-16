#!/usr/bin/env python3
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
"""Exercise source archives and cache/downloads; --archive-only skips builds."""
import functools
import hashlib
import http.server
import io
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import threading

if sys.argv[1] == '--build-pair':
    processes = []
    logs = []
    try:
        for directory in sys.argv[3:]:
            log = open(Path(directory).with_suffix('.build.log'), 'wb')
            logs.append(log)
            processes.append(subprocess.Popen([sys.argv[2], '--build', directory,
                                               '--config', 'Debug', '--parallel', '2'],
                                              stdout=log, stderr=subprocess.STDOUT))
        results = [process.wait() for process in processes]
    finally:
        for process in processes:
            if process.poll() is None:
                process.terminate()
                process.wait()
        for log in logs:
            log.close()
    sys.exit(1 if any(results) else 0)

class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *_):
        pass

root = Path(sys.argv[2]).resolve()
root.mkdir(parents=True, exist_ok=True)

# Use an independently written PAX archive, including the UTF-8 pathname
# metadata used by real dependency releases such as FLAC.
unicode_root = root / 'unicode-archive'
unicode_root.mkdir(parents=True, exist_ok=True)
archive = unicode_root / 'source;unicode.tar'
filename = 'r\u00e9sum\u00e9-\u2603.txt'
payload = b'Unicode dependency archive fixture\n'
with tarfile.open(archive, 'w', format=tarfile.PAX_FORMAT) as package:
    entry = tarfile.TarInfo(f'package/{filename}')
    entry.size = len(payload)
    package.addfile(entry, io.BytesIO(payload))
driver = unicode_root / 'extract.cmake'
driver.write_text('''include("${FETCH_SCRIPT}")
if(NOT "$ENV{LC_ALL}" STREQUAL "C")
  message(FATAL_ERROR "Extraction changed the caller's locale")
endif()
''', encoding='utf-8')
subprocess.run([sys.argv[1],
                f'-DFETCH_SCRIPT={Path(__file__).resolve().parent.parent / "dependencies/Fetch.cmake"}',
                f'-DSOURCE_URL={archive}',
                f'-DSHA256={hashlib.sha256(archive.read_bytes()).hexdigest()}',
                f'-DCACHE_FILE={unicode_root / "cache.tar"}',
                f'-DDESTINATION={unicode_root / "extracted"}',
                '-DDOWNLOADS=OFF', '-P', str(driver)],
               env=dict(os.environ, LC_ALL='C'), check=True)
if (unicode_root / 'extracted' / filename).read_bytes() != payload:
    raise RuntimeError('Unicode dependency source contents changed')
if sys.argv[3:] == ['--archive-only']:
    sys.exit(0)

handler = functools.partial(QuietHandler, directory=str(root))
with http.server.ThreadingHTTPServer(('127.0.0.1', 0), handler) as server:
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        result = subprocess.run([sys.argv[1], f'-DTEST_ROOT={root}',
                                 f'-DTEST_BASE_URL=http://127.0.0.1:{server.server_port}',
                                 f'-DPARALLEL_BUILDER={Path(__file__).resolve()}',
                                 f'-DPYTHON={sys.executable}',
                                 '-P', str(Path(__file__).with_suffix('.cmake'))])
    finally:
        server.shutdown()
        worker.join()
if result.returncode == 0:
    # Model a hosted CI job restoring only the compiled dependencies into a
    # fresh parent build at the same absolute path. Checkout input timestamps
    # are normalized by the workflow; pinned source/recipe changes instead
    # invalidate its exact cache key. Restoring files must actually avoid
    # recompilation, including transitive dependencies.
    build = root / 'build'
    with tempfile.TemporaryDirectory(prefix='cache-restore-', dir=root) as temporary:
        original = Path(temporary) / 'original'
        build.rename(original)
        build.mkdir()
        shutil.copytree(original / 'dependencies', build / 'dependencies', symlinks=True)
        objects = [p for p in (build / 'dependencies').rglob('*')
                   if p.suffix in ('.o', '.obj')]
        if not objects:
            raise RuntimeError('Cache restore fixture has no compiled dependency objects')
        timestamps = {p: p.stat().st_mtime_ns for p in objects}
        for p in (root / 'modules').rglob('*'):
            if p.is_file():
                os.utime(p, (946684800, 946684800))
        commands = [
            ('cache-restore-configure', [sys.argv[1], '-S', str(root / 'consumer'),
             '-B', str(build), f'-DMODULES={root / "modules"}',
             f'-DSYNCTERM_DEPENDENCY_CACHE={root / "cache"}']),
            ('cache-restore-build', [sys.argv[1], '--build', str(build),
             '--config', 'Debug', '--parallel', '2']),
        ]
        for name, command in commands:
            with open(root / f'{name}.log', 'wb') as log:
                subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
        rebuilt = [str(p) for p, timestamp in timestamps.items()
                   if p.stat().st_mtime_ns != timestamp]
        if rebuilt:
            raise RuntimeError(f'Cached dependency objects were recompiled: {rebuilt}')
sys.exit(result.returncode)

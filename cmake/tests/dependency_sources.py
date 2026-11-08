#!/usr/bin/env python3
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
"""Exercise cache/download behavior using a job-owned loopback HTTP server."""
import functools
import http.server
from pathlib import Path
import subprocess
import sys
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
sys.exit(result.returncode)

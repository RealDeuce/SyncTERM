#!/usr/bin/env python3
"""Exercise the C transfer worker against native, independent command-line peers."""
import argparse
import os
from pathlib import Path
import socket
import subprocess
import tempfile

KINDS = {'zmodem': 0, 'ymodem': 1, 'ymodem-g': 2, 'xmodem-1k': 3,
         'xmodem-128': 4, 'xmodem-crc': 5, 'xmodem-chksum': 6, 'xmodem-g': 11}

def transfer(driver, executable, protocol, upload, source, destination, files, *, crc16=False, receive_events=False):
    is_sexyz = Path(executable).name == 'sexyz'
    if upload:
        if protocol == 'zmodem': command = ['rz'] if is_sexyz else ['--zmodem', '-b', '-q']
        # This sexyz revision strands the terminating ACK in its producer
        # buffer by default. -s bypasses that independent receiver defect.
        elif protocol == 'ymodem': command = ['-s', 'ry'] if is_sexyz else ['--ymodem', '-b', '-q']
        elif protocol == 'ymodem-g': command = ['-g', 'rg']
        else: command = ['rc', 'received.bin'] if is_sexyz else ['--xmodem', '-c', '-b', '-q', 'received.bin']
        args = [driver, 'PORT', 'send', str(KINDS[protocol]), str(destination)] + [str(source / f) for f in files]
    else:
        if protocol == 'zmodem': command = ['sz'] if is_sexyz else ['--zmodem', '-b', '-q']
        elif protocol in ('ymodem', 'ymodem-g'): command = ['sy'] if is_sexyz else ['--ymodem', '-b', '-q']
        else: command = ['sX'] if is_sexyz else ['--xmodem', '-k', '-b', '-q']
        command += [str(source / f) for f in files]
        args = [driver, 'PORT', 'recv', str(KINDS[protocol]), str(destination)]
        if protocol.startswith('xmodem'): args += ['received.bin']
    if crc16: command.insert(0, '-o')
    if is_sexyz: command.insert(0, '-raw')
    with socket.socket() as listener, tempfile.TemporaryFile() as local_log, tempfile.TemporaryFile() as remote_log:
        listener.bind(('127.0.0.1', 0)); listener.listen(1); listener.settimeout(10)
        args[1] = str(listener.getsockname()[1])
        environment = dict(os.environ)
        environment.pop('ST_TRANSFER_RECEIVE_EVENTS', None)
        if receive_events: environment['ST_TRANSFER_RECEIVE_EVENTS'] = '1'
        local = subprocess.Popen(args, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                 stderr=local_log, env=environment)
        remote = None
        try:
            connection, _ = listener.accept()
            with connection:
                remote = subprocess.Popen([executable] + command, cwd=destination,
                                          stdin=connection, stdout=connection, stderr=remote_log)
            code = local.wait(timeout=35)
            remote_code = remote.wait(timeout=5)
            local_log.seek(0)
            local_report = local_log.read().decode(errors="replace")
            if code or remote_code: raise RuntimeError(f'exit codes {code}, {remote_code}')
        except BaseException as exc:
            local.kill(); local.wait()
            if remote is not None: remote.kill(); remote.wait()
            local_log.seek(0); remote_log.seek(0)
            raise AssertionError(f'{protocol} upload={upload} crc16={crc16}: {exc}\n'
                                 + local_log.read().decode(errors='replace')[-15000:] + '\n'
                                 + remote_log.read().decode(errors='replace')[-5000:]) from exc
    for f in files:
        output = destination / ('received.bin' if protocol.startswith('xmodem') else f)
        expected = (source / f).read_bytes()
        actual = output.read_bytes()
        if protocol.startswith('xmodem'):
            assert actual[:len(expected)] == expected, (protocol, 'payload mismatch')
            assert len(actual) - len(expected) < 128 and all(c == 26 for c in actual[len(expected):]), (protocol, 'padding mismatch')
        else: assert actual == expected, (protocol, f, 'file mismatch')
    return local_report

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--driver', required=True)
    parser.add_argument('--sexyz')
    parser.add_argument('--lsx')
    parser.add_argument('--lrx')
    parser.add_argument('--case', choices=['zmodem', 'xymodem', 'all'], default='all')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='st-transfer-interop-') as temp:
        root = Path(temp); source = root / 'source'; source.mkdir()
        files = ['binary.dat', 'empty.dat', 'name with spaces.dat']
        (source / files[0]).write_bytes(bytes(range(256)) * 301 + b'tail')
        (source / files[1]).write_bytes(b'')
        (source / files[2]).write_bytes(b'\0\xff\x18\x11\x13\r\n' * 997)
        cases = []
        if args.case in ('zmodem', 'all'):
            for upload in (False, True):
                if args.sexyz:
                    for crc16 in (False, True): cases.append((args.sexyz, 'zmodem', upload, files, crc16))
                program = args.lrx if upload else args.lsx
                if program: cases.append((program, 'zmodem', upload, files, False))
        if args.case in ('xymodem', 'all'):
            for upload in (False, True):
                protocols = ('xmodem-128', 'xmodem-1k', 'ymodem', 'ymodem-g') if upload else ('xmodem-crc', 'xmodem-chksum', 'xmodem-g', 'ymodem', 'ymodem-g')
                for protocol in protocols:
                    batch = files if protocol.startswith('ymodem') else files[:1]
                    if args.sexyz: cases.append((args.sexyz, protocol, upload, batch, False))
                    program = args.lrx if upload else args.lsx
                    # Use sexyz for streaming X/YMODEM-g; the installed
                    # lrzsz 0.12.20 peer fails these streaming exchanges.
                    if program and protocol not in ('ymodem-g', 'xmodem-g'):
                        cases.append((program, protocol, upload, batch, False))
        for number, (program, protocol, upload, selected, crc16) in enumerate(cases):
            # This native sexyz version rejects ZRPOS 0 for an empty source.
            # Exercise empty ZMODEM downloads against independent lrzsz instead.
            if Path(program).name == 'sexyz' and protocol == 'zmodem' and not upload:
                selected = [name for name in selected if (source / name).stat().st_size != 0]
            destination = root / str(number); destination.mkdir()
            print(f'{Path(program).name} {protocol} upload={upload} crc16={crc16}', flush=True)
            transfer(args.driver, program, protocol, upload, source, destination, selected, crc16=crc16)
        if args.case in ('zmodem', 'all'):
            for program in (args.sexyz, args.lsx):
                if not program: continue
                destination = root / (Path(program).name + '-events')
                destination.mkdir()
                print(f'{Path(program).name} zmodem receive events', flush=True)
                # Events must describe exactly the written bytes, including
                # split 8-KiB subpackets, rather than resuming past a prefix.
                (destination / files[0]).write_bytes(b'old asset')
                transfer(args.driver, program, 'zmodem', False, source, destination,
                         files[::2], receive_events=True)
                for mode in ('partial', 'complete', 'mismatch'):
                    destination = root / (Path(program).name + '-' + mode)
                    destination.mkdir()
                    payload = (source / files[0]).read_bytes()
                    previous = payload[:4093] if mode == 'partial' else payload if mode == 'complete' else b'wrong prefix'
                    (destination / files[0]).write_bytes(previous)
                    print(f'{Path(program).name} zmodem receive existing={mode}', flush=True)
                    report = transfer(args.driver, program, 'zmodem', False, source, destination, files[:1])
                    if mode == 'partial': assert 'Resuming binary.dat at 4093' in report, report
                    else: assert 'Resuming' not in report, report
        assert cases, 'No native peers configured'
if __name__ == '__main__': main()

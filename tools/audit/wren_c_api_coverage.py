#!/usr/bin/env python3
"""Verify the frozen native-interface ledger and its Wren assessment evidence.

Run at the repository root after configuring a native CMake build:
  python3 tools/audit/wren_c_api_coverage.py --check --build-dir build-wren-input
Use --extract to emit the current native Clang inventory instead. This checks
inventory/evidence drift, not semantic truth or the desirability of new APIs.
The report documents conditional declarations and intentional exclusions.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause

import argparse
import concurrent.futures
import csv
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys


def exclusion_reason(path):
    """Recognize implementation domains, never silently discard a new header."""
    if 'tests' in path.parts or path.name == 'st_console_test.h':
        return 'Test fixtures/control and expected-result helpers, not production script capabilities.'
    if 'generated' in path.parts or path.name.endswith('_data.h'):
        return 'Generated immutable Unicode/font/catalog/protocol data consumed by the owning public component.'
    if 'wren_vm' in path.parts:
        return 'Bundled Wren compiler/VM/optional-module internals; standard language semantics are not C host features to port.'
    if 're1' in path.parts:
        return 'Bundled regex implementation; scripts already use the host match-hook contract, not raw regex engine ownership.'
    if 'ssh' in path.parts:
        return 'Third-party/private SSH implementation; active connection and st_sftp.h are the application boundaries.'
    if 'private' in path.parts or 'internal' in path.name:
        return 'Private component representation/callback plumbing behind the public component owner.'
    owners = {
        Path('src/terminal/cterm'): ('st_cterm.h', 'CTerm parser/RIP/SkyPix protocol internals; st_cterm.h and term session own parsing, resources and lifetime.'),
        Path('src/terminal/ooii'): ('st_ooii.h', 'OOII immutable resource tables and private parser implementation behind st_ooii.h.'),
        Path('src/media/console'): ('st_console.h', 'Console backend, rasterizer and display-worker mechanisms behind st_console.h; not independent script displays.'),
        Path('src/net/sftp_client'): ('st_sftp.h', 'SSH/SFTP adapter machinery; st_sftp.h is the typed provider boundary assessed in the ledger.'),
        Path('src/scripting/wren_host'): ('st_wren_host.h', 'Foreign payloads, embedding and native hook plumbing; script declarations and dispatch are reviewed as evidence.')}
    if path.parent in owners:
        public, reason = owners[path.parent]
        if path.name != public:
            return reason
    music_private = {
        Path('src/media/music/adl_symbols.h'),
        Path('src/media/music/alsa_symbols.h'),
        Path('src/media/music/fluid_symbols.h'),
        Path('src/media/music/midi_file.h'),
    }
    if path in music_private:
        return 'Private MIDI adapter symbol tables and checked file iterator, consumed only by the session music implementation/tests behind st_music.h.'
    if path.parent == Path('src/media/unifont'):
        return 'Optional native font-pack loader owned by rendering; script rich-font omissions are recorded separately.'
    explicit = {
        'libretro.h': 'External libretro frontend ABI, not script-owned process/rendering lifetime.',
        'st_windows_launcher.h': 'Windows launcher-to-DLL entry ABI, not a script operation.',
        'path_windows.h': 'Windows path encoding/mechanism adapter behind portable path.h and filesystem grants.',
        'st_windows_utf.h': 'Win32 UTF-16 conversion adapter; host text uses portable UTF-8 NFC.',
        'cluster_columns.h': 'Private text presentation cluster-width helper; high-level widths are Screen.textWidth and Text.graphemes.',
        'decimal_double.h': 'Private decimal conversion representation behind value_convert.h.',
        'value_convert_time.h': 'Private calendar conversion implementation behind value_convert.h.'}
    return explicit.get(path.name)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def extract(header):
    """Extract declarations belonging to this header, not its includes."""
    source_bytes = Path(header).read_bytes()
    source = source_bytes.decode('utf-8')
    command = [compiler, '-x', 'c', '-std=c17', '-fsyntax-only',
               '-Xclang', '-ast-dump=json', '-Isrc', '-I' + str(build_dir)]
    command += ['-I' + directory for directory in include_dirs]
    if header.endswith('/string_compat.h'):
        command.append('-DSTRING_COMPAT_FORCE_FALLBACKS=1')
    command.append(header)
    process = subprocess.run(command, capture_output=True, text=True)
    if process.returncode:
        return {'header': header, 'error': process.stderr[:2000]}
    ast = json.loads(process.stdout)
    result = []
    records = {}

    def line(location):
        return location.get('line', source_bytes.count(
            b'\n', 0, location.get('offset', 0)) + 1)

    def add(symbol, location, declaration):
        result.append({'id': symbol, 'line': line(location),
                       'type': declaration})

    def record(node, prefix):
        children = node.get('inner', [])
        nested = {}
        for child in children:
            if child.get('kind') == 'RecordDecl' and child.get('completeDefinition'):
                location = child.get('loc', {})
                nested[(line(location), location.get('col'))] = child
        for child in children:
            if child.get('kind') != 'FieldDecl' or child.get('isImplicit'):
                continue
            name = child.get('name')
            if not name:
                continue
            key = prefix + '.' + name
            declaration = child.get('type', {}).get('qualType', '')
            add('field:' + key, child.get('loc', {}), declaration)
            anonymous = re.search(
                r'(?:anonymous|unnamed).*?:(\d+):(\d+)\)', declaration)
            coordinates = (int(anonymous[1]), int(anonymous[2])) if anonymous else None
            if coordinates in nested:
                record(nested[coordinates], key)
            elif re.fullmatch(r'(?:struct|union) \w+', declaration):
                # Expand records embedded by value, never pointer ownership.
                tag = declaration.split()[1]
                if tag in records and tag != prefix:
                    record(records[tag], key)

    roots = []
    for node in ast.get('inner', []):
        location = node.get('loc', {})
        if not location or 'includedFrom' in location:
            continue
        filename = location.get('file')
        if filename and Path(filename).resolve() != Path(header).resolve():
            continue
        if location.get('offset', len(source_bytes) + 1) >= len(source_bytes):
            continue
        roots.append(node)
        if (node.get('kind') == 'RecordDecl' and node.get('name')
                and node.get('completeDefinition')):
            records[node['name']] = node
    for node in roots:
        kind, name = node.get('kind'), node.get('name')
        location = node.get('loc', {})
        declaration = node.get('type', {}).get('qualType', '')
        if kind == 'FunctionDecl' and name:
            add('function:' + name, location, declaration)
        elif kind == 'RecordDecl' and name and node.get('completeDefinition'):
            record(node, name)
        elif kind == 'EnumDecl':
            for child in node.get('inner', []):
                if child.get('kind') == 'EnumConstantDecl':
                    add('enum:' + child['name'], child.get('loc', {}),
                        name or '<anonymous>')
        elif kind == 'TypedefDecl' and name and '(*' in declaration:
            add('callback:' + name, location, declaration)
        elif kind == 'VarDecl' and name and node.get('storageClass') == 'extern':
            add('variable:' + name, location, declaration)
    # Include every textual macro branch, including multiline and indented
    # definitions. Preserve alternatives under one symbol id. Header digests
    # also protect enum values, conditions, type aliases and comments/contracts.
    pattern = r'^[ \t]*#define[ \t]+(\w+)((?:[^\n]*\\\n)*[^\n]*)'
    for match in re.finditer(pattern, source, re.M):
        name, value = match.groups()
        if not value.strip() or name.endswith(('_H_', '_H')):
            continue
        result.append({'id': 'macro:' + name,
                       'line': source.count('\n', 0, match.start()) + 1,
                       'type': ' '.join(value.replace('\\\n', ' ').split())})
    by_id = {}
    for item in result:
        previous = by_id.get(item['id'])
        if previous and item['id'].startswith('macro:'):
            if item['type'] != previous['type']:
                previous['type'] += ' | alternative: ' + item['type']
        else:
            by_id[item['id']] = item
    return {'header': header, 'symbols': sorted(
        by_id.values(), key=lambda item: (item['line'], item['id']))}


def verify(data, excluded):
    metadata = json.loads(Path('docs/api/wren-c-api-assessments.json').read_text())
    with Path('docs/api/wren-c-api-coverage.tsv').open(newline='') as stream:
        ledger = list(csv.DictReader(stream, delimiter='\t'))
    errors = []
    def require(condition, message):
        if not condition:
            errors.append(message)
    require(metadata['status'] == 'complete', 'Accounting status is not complete.')
    current = {entry['header']: entry for entry in data}
    recorded = {entry['path']: entry for entry in metadata['headers']}
    require(current.keys() == recorded.keys(), 'Public header set changed; review scope.')
    recorded_excluded = {e['path']: e['reason'] for e in metadata['excluded_headers']}
    require(excluded == recorded_excluded, 'Excluded header set/reasons changed; review scope.')
    expected = {}
    for header, entry in current.items():
        if 'error' in entry:
            errors.append(header + ': ' + entry['error'])
            continue
        require(header in recorded and digest(header) == recorded[header]['sha256'], header + ': header changed.')
        require(header in recorded and recorded[header]['status'] == 'reviewed', header + ': unreviewed header.')
        for symbol in entry['symbols']:
            expected[(header, symbol['id'])] = (str(symbol['line']), symbol['type'])
    actual = {}
    groups = metadata['assessments']
    used = set()
    statuses = {'covered', 'partial', 'unmapped', 'host-owned', 'language-equivalent'}
    for row in ledger:
        key = (row['header'], row['symbol'])
        require(key not in actual, str(key) + ': duplicate ledger entry.')
        actual[key] = (row['line'], row['native_declaration'])
        group = row['assessment']
        require(group in groups, str(key) + ': missing assessment.')
        used.add(group)
    require(actual == expected, 'Native symbol/type/line inventory differs from frozen ledger.')
    require(used == groups.keys(), 'Assessment groups are missing or unused.')
    for key, assessment in groups.items():
        require(assessment.get('status') in statuses, key + ': invalid disposition.')
        for field in ('wren', 'reason', 'vm', 'evidence'):
            require(bool(assessment.get(field)), key + ': missing ' + field)
        if assessment['status'] in {'partial', 'unmapped'}:
            require(bool(assessment.get('finding')), key + ': missing finding id.')
        for evidence in assessment['evidence']:
            path = Path(evidence['path'])
            require(path.is_file(), key + ': missing evidence ' + str(path))
            if path.is_file():
                require(evidence['anchor'] in path.read_text(), key + ': absent evidence anchor in ' + str(path))
    for path, sha256 in metadata['evidence_files'].items():
        require(Path(path).is_file() and digest(path) == sha256, path + ': evidence changed; re-review mappings.')
    evidence_records = [e for g in groups.values() for e in g['evidence']]
    for section in ('consumer_evidence', 'test_evidence', 'dispatch_evidence'):
        evidence_records.extend(metadata[section])
    for finding in metadata['documentation_findings']:
        evidence_records.extend(finding['evidence'])
    for evidence in evidence_records:
        path = Path(evidence['path'])
        require(path.is_file() and evidence['anchor'] in path.read_text(),
                str(path) + ': missing consumer/test/documentation evidence anchor.')
    required_evidence = {e['path'] for e in evidence_records}
    require(required_evidence <= metadata['evidence_files'].keys(),
            'Some assessment evidence has no frozen digest.')
    report = Path('docs/WREN_C_API_COVERAGE.adoc').read_text()
    for finding in {g['finding'] for g in groups.values() if 'finding' in g}:
        require('[[finding-' + finding + ']]' in report,
                finding + ': absent from readable report.')
    for finding in metadata['documentation_findings']:
        require('[[' + finding['id'] + ']]' in report,
                finding['id'] + ': absent documentation finding.')
    for header in recorded:
        require(header in report, header + ': absent from report matrix.')
    require(len(set(metadata['verification']['test_names'])) ==
            metadata['verification']['tests_passed'],
            'Verification test count/names disagree.')
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print(f"Verified {len(current)} public headers, {len(excluded)} explicit header exclusions, "
          f"{len(ledger)} native items, {len(groups)} assessments and "
          f"{len({g['finding'] for g in groups.values() if 'finding' in g})} finding topics.")
    return 0


def main():
    global compiler, build_dir, include_dirs
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--check', action='store_true')
    mode.add_argument('--extract', action='store_true')
    parser.add_argument('--build-dir', type=Path, default=Path('build-wren-input'))
    parser.add_argument('--clang', default='clang')
    args = parser.parse_args()
    compiler, build_dir = args.clang, args.build_dir
    if not Path('src/scripting/wren_host/st_wren_host.h').is_file():
        parser.error('Run from the SyncTERM repository root.')
    if not (build_dir / 'syncterm_config.h').is_file():
        parser.error('--build-dir must name a configured native CMake build.')
    paths = sorted(Path('src').rglob('*.h'))
    excluded = {str(path): reason for path in paths if (reason := exclusion_reason(path))}
    headers = [str(path) for path in paths if str(path) not in excluded]
    include_dirs = sorted({str(p.parent) for p in paths if not any(x in p.parts for x in {'tests', 'generated', 'private', 'wren_vm', 're1', 'ssh'})})
    # Project-generated headers mirror src/. Dependency source/staging trees
    # must not become global include directories (e.g. a dependency's private
    # time.h can otherwise shadow the C library header in an unrelated API).
    include_dirs += sorted({str(p.parent) for p in (build_dir / 'src').rglob('*.h')})
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        data = list(pool.map(extract, headers))
    if args.extract:
        print(json.dumps(data, indent=2))
        return int(any('error' in entry for entry in data))
    return verify(data, excluded)


if __name__ == '__main__':
    raise SystemExit(main())

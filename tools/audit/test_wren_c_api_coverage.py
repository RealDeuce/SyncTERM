#!/usr/bin/env python3
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
"""Check API accounting without a parallel file-version manifest."""
import contextlib
import copy
import csv
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools.audit.wren_c_api_coverage import verify

work_root = Path(sys.argv.pop(1)).resolve()
work_root.mkdir(parents=True, exist_ok=True)


class AccountingChecks(unittest.TestCase):
    def setUp(self):
        self.previous = Path.cwd()
        self.directory = tempfile.TemporaryDirectory(dir=work_root)
        os.chdir(self.directory.name)
        Path('src').mkdir()
        Path('docs/api').mkdir(parents=True)
        Path('src/example.h').write_text('// Changed comment\nint example(void);\n')
        Path('src/evidence.c').write_text('// Added context\n// API anchor\n')
        Path('docs/WREN_C_API_COVERAGE.adoc').write_text('src/example.h\n')
        metadata = {
            'status': 'complete',
            'headers': [{'path': 'src/example.h', 'status': 'reviewed'}],
            'excluded_headers': [],
            'assessments': {'example': {
                'status': 'covered', 'wren': 'Example.call', 'reason': 'Native operation',
                'vm': 'Both', 'evidence': [{'path': 'src/evidence.c', 'anchor': 'API anchor'}]}},
            'consumer_evidence': [], 'test_evidence': [], 'dispatch_evidence': [],
            'documentation_findings': [],
            'verification': {'test_names': ['accounting'], 'tests_passed': 1}}
        Path('docs/api/wren-c-api-assessments.json').write_text(json.dumps(metadata))
        with Path('docs/api/wren-c-api-coverage.tsv').open('w', newline='') as stream:
            writer = csv.writer(stream, delimiter='\t')
            writer.writerow(['header', 'symbol', 'line', 'native_declaration', 'assessment'])
            writer.writerow(['src/example.h', 'function:example', '1', 'int (void)', 'example'])
        self.data = [{'header': 'src/example.h', 'symbols': [
            {'id': 'function:example', 'line': 2, 'type': 'int (void)'}]}]

    def tearDown(self):
        os.chdir(self.previous)
        self.directory.cleanup()

    def check(self, data=None):
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            return verify(self.data if data is None else data, {})

    def test_text_and_line_changes_need_no_hash_refresh(self):
        self.assertEqual(self.check(), 0)

    def test_inventory_and_type_changes_still_fail(self):
        changed = copy.deepcopy(self.data)
        changed[0]['symbols'][0]['type'] = 'void (void)'
        self.assertNotEqual(self.check(changed), 0)
        added = copy.deepcopy(self.data)
        added[0]['symbols'].append({'id': 'function:new_api', 'line': 3, 'type': 'int (void)'})
        self.assertNotEqual(self.check(added), 0)

    def test_missing_reference_anchor_still_fails(self):
        Path('src/evidence.c').write_text('// Removed reference\n')
        self.assertNotEqual(self.check(), 0)


if __name__ == '__main__':
    unittest.main()

#!/usr/bin/env python3
"""Build bundled fonts independently of the host text encoding/newlines."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import io
import hashlib
import json
import re
from pathlib import Path
import runpy
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]


class GeneratorTests(unittest.TestCase):
    def test_legacy_text_defaults(self):
        generator = runpy.run_path(str(ROOT / 'generate_fonts.py'))
        original_open = io.open
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'amiga_font_data.c'
            for default_encoding in ('cp1252', 'ascii'):
                with self.subTest(encoding=default_encoding):
                    def legacy_open(file, mode='r', buffering=-1, encoding=None,
                                    errors=None, newline=None, closefd=True, opener=None):
                        if 'b' not in mode:
                            if encoding is None:
                                encoding = default_encoding
                            if 'w' in mode and newline is None:
                                newline = '\r\n'
                        return original_open(file, mode, buffering, encoding, errors,
                                             newline, closefd, opener)

                    def text_encoding(encoding, *args):
                        return default_encoding if encoding is None else encoding

                    with patch('io.open', legacy_open), patch('io.text_encoding', text_encoding, create=True), patch.object(sys, 'argv',
                            ['generate_fonts.py', str(ROOT / 'data/manifest.json'), str(output)]):
                        generator['main']()
                    data = output.read_bytes()
                    self.assertTrue(data.startswith(b'/* Generated from pinned bitmap sources;'))
                    self.assertTrue(b'struct st_amiga_builtin st_amiga_builtins[]' in data)
                    self.assertFalse(b'\r' in data, 'generated source must use LF newlines')
                    if default_encoding == 'cp1252':
                        previous = hashlib.sha256(data).digest()
                    else:
                        self.assertEqual(hashlib.sha256(data).digest(), previous)

    def test_embedded_files_are_unchanged(self):
        generator = runpy.run_path(str(ROOT / 'generate_fonts.py'))
        manifest = json.loads((ROOT / 'data/manifest.json').read_text(encoding='utf-8'))
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'data.c'
            with patch.object(sys, 'argv', ['generate_fonts.py', str(ROOT / 'data/manifest.json'), str(output)]):
                generator['main']()
            arrays = re.findall(r'static const uint8_t file_([0-9]+)\[\] = \{([^}]+)\};',
                                output.read_text(encoding='utf-8'))
            self.assertEqual(len(arrays), len(manifest['fonts']))
            for index, values in arrays:
                entry = manifest['fonts'][int(index)]
                actual = bytes(int(value, 16) for value in values.split(',') if value.strip())
                self.assertTrue(actual == (ROOT / 'data' / entry['file']).read_bytes(), entry['file'])


if __name__ == '__main__':
    unittest.main()

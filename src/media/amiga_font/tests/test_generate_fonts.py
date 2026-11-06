#!/usr/bin/env python3
"""Build bundled fonts independently of the host text encoding/newlines."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import io
import hashlib
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
                    self.assertIn(b'const struct st_amiga_builtin st_amiga_builtins[]', data)
                    self.assertFalse(b'\r' in data, 'generated source must use LF newlines')
                    if default_encoding == 'cp1252':
                        previous = hashlib.sha256(data).digest()
                    else:
                        self.assertEqual(hashlib.sha256(data).digest(), previous)


if __name__ == '__main__':
    unittest.main()

# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import importlib.util
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

path = Path(__file__).resolve().parents[1] / 'update_emoji_catalog.py'
sys.path.insert(0, str(path.parent))
spec = importlib.util.spec_from_file_location('catalog', path)
catalog = importlib.util.module_from_spec(spec)
spec.loader.exec_module(catalog)

class GeneratorTests(unittest.TestCase):
    def test_gender_and_tone(self):
        entries = catalog.names(catalog.ROOT)
        canonical = {catalog.plain(text): text for text, _ in entries}
        for value in ('🤷', '🤷🏻', '🤷\u200d♀️', '🤷🏽\u200d♂️'):
            self.assertEqual(catalog.family(value, canonical), '🤷')
        self.assertEqual(catalog.family('🫱🏽\u200d🫲🏻', canonical), '🤝')
        self.assertNotEqual(catalog.family('🏳️\u200d⚧️', canonical), '🏳')

    def test_new_supported_entry_is_included(self):
        coverage = {'sequences': ['1F600', '1F603']}
        with patch.object(catalog, 'names', return_value=[('😀', 'first'), ('😃', 'new face'), ('😄', 'not rendered')]):
            generated = catalog.catalog(catalog.ROOT, coverage)
        self.assertIn('\\x6e\\x65\\x77\\x20\\x66\\x61\\x63\\x65', generated)
        self.assertNotIn('not rendered', generated)
        self.assertEqual(generated.count('\t{'), 2)

    def test_pinned_source_and_output(self):
        catalog.validate_repository()
        self.assertGreater(len(catalog.names(catalog.ROOT)), 3000)
        original = catalog.SOURCE_SHA256
        with patch.object(catalog, 'SOURCE_SHA256', '0' * 64):
            with self.assertRaisesRegex(ValueError, 'pinned'):
                catalog.names(catalog.ROOT)
        self.assertEqual(catalog.SOURCE_SHA256, original)

    def test_every_updater_checks_picker(self):
        paths = [*catalog.ROOT.glob('src/text/unicode_*/update_unicode_*.py'),
                 catalog.ROOT / 'src/media/fonts/update_fonts.py',
                 catalog.ROOT / 'src/media/fonts/update_emoji.py']
        self.assertEqual(len(paths), 6)
        for path in paths:
            with self.subTest(path=path.name):
                self.assertIn('update_emoji_catalog.py', path.read_text())

    def test_stale_font_and_catalog_fail(self):
        coverage = json.loads((catalog.ROOT / catalog.MANIFEST).read_text())
        coverage['font_sha256'] = '0' * 64
        with patch.object(catalog, 'font_coverage', return_value=coverage):
            with self.assertRaisesRegex(ValueError, 'Stale picker glyph coverage'):
                catalog.validate_repository()
        with patch.object(catalog, 'catalog', return_value='outdated catalog'):
            with self.assertRaisesRegex(ValueError, 'Stale emoji picker catalog'):
                catalog.validate_repository()

    def test_windows_font_line_endings(self):
        coverage = catalog.font_coverage(catalog.ROOT)
        data = (catalog.ROOT / 'src/media/fonts/generated/st_emoji_data.c').read_bytes()
        with patch.object(Path, 'read_bytes', return_value=data.replace(b'\n', b'\r\n')):
            self.assertEqual(catalog.font_coverage(catalog.ROOT), coverage)

if __name__ == '__main__':
    unittest.main()

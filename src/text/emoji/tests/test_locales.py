# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
from pathlib import Path
import random
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import update_emoji_locales as locales


def unpack(packed):
    """Independent test-side LZ4 reader, including overlapping backreferences."""
    data = iter(packed)
    output = bytearray()

    def extended(length):
        if length == 15:
            while True:
                extra = next(data)
                length += extra
                if extra != 255:
                    break
        return length

    while True:
        try:
            token = next(data)
        except StopIteration:
            break
        for _ in range(extended(token >> 4)):
            output.append(next(data))
        try:
            low = next(data)
        except StopIteration:
            break
        offset = low + 256 * next(data)
        for _ in range(4 + extended(token & 15)):
            output.append(output[-offset])
    return bytes(output)


class LocaleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = locales.source(locales.catalog.ROOT)

    def test_language_resolution(self):
        expected = {'fr-CA': 'fr-ca', 'fr-FR': 'fr', 'es-MX': 'es-mx',
                    'zh-TW': 'zh-hant', 'zh-HK': 'zh-hant-hk',
                    'sr-Latn-RS': 'sr-latn', 'iw': 'he', 'sh': 'sr-latn',
                    'de-Latn-DE': 'de', 'es-AR': 'es-419',
                    'es-Latn-AR': 'es-419', 'pt-AO': 'pt-pt',
                    'sr-Arab': None, 'zz': None}
        for request, resolved in expected.items():
            with self.subTest(request=request):
                self.assertEqual(locales.resolve(locales.key(request), self.data), resolved)

    def test_names_keywords_sequences_and_parents(self):
        fr = self.data['locales']['fr']
        self.assertEqual(fr['🕹'][0], 'manette de jeu')
        self.assertIn('miauler', fr['🐈'][1])
        self.assertIn('peau légèrement mate', fr['🤷🏽\u200d♂'][0])
        self.assertIn('Canada', fr['🇨🇦'][0])
        self.assertNotIn('🕹', self.data['locales']['fr-ca'])
        self.assertEqual(locales.parent('fr-ca', self.data['parents']), 'fr')
        self.assertEqual(locales.parent('zh-hant', self.data['parents']), 'root')
        self.assertEqual(locales.parent('es-ar', self.data['parents']), 'es-419')
        self.assertEqual(locales.sequence('🤷🏽\u200d♂️'), '🤷🏽\u200d♂')
        self.assertEqual(locales.sequence('🕹︎'), '🕹')

    def test_pins(self):
        with patch.object(locales, 'SOURCE_SHA256', '0' * 64):
            with self.assertRaisesRegex(ValueError, 'pinned'):
                locales.source(locales.catalog.ROOT)
        with self.assertRaisesRegex(ValueError, 'upstream pin'):
            locales.import_archive(b'invalid')

    def test_catalog_index_pin(self):
        locales.validate_catalog()
        with patch.object(locales, 'catalog_fingerprint', return_value='outdated'):
            with self.assertRaisesRegex(ValueError, 'Stale CLDR emoji indexes'):
                locales.validate_catalog()

    def test_lz4(self):
        rng = random.Random(7)
        for original in (b'', b'hello', b'a' * 10000,
                         bytes(range(256)) * 400,
                         bytes(rng.randrange(256) for _ in range(70000)),
                         '🤷🏽‍♂️ visage rieur '.encode() * 1000):
            with self.subTest(size=len(original)):
                self.assertEqual(unpack(locales.lz4_block(original)), original)
        self.assertEqual(locales.lz4_block(b'hello'), b'\x50hello')

    def test_all_parent_chains_terminate(self):
        for locale in self.data['locales']:
            visited = set()
            while locale != 'root':
                self.assertNotIn(locale, visited)
                visited.add(locale)
                locale = locales.parent(locale, self.data['parents'])


if __name__ == '__main__':
    unittest.main()

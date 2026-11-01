# Emoji picker catalog

The picker uses Unicode 18.0.0's fully-qualified emoji and English names from
`https://www.unicode.org/Public/18.0.0/emoji/emoji-test.txt`, vendored as
`data/emoji-test-18.0.0.txt`. SHA-256:
`8f3735cda1f92a779d78af67cf86066bb1f07143dc22f2ac29394d9bc57ab21a`.
This data uses the Unicode License v3 in
`../unicode_text/LICENSE.UNICODE`, also available in the in-program Licenses
viewer. SyncTERM's generator and catalog API use BSD-2-Clause.

`update_emoji_catalog.py` intersects those canonical sequences with the actual
shared single-scalar and cluster mappings in the three bundled emoji banks.
It excludes standalone components, qualification aliases, and unsupported
sequences. It retains selectors, joiners, and skin tones in inserted strings.
Unifont/UNSCII/codepage fonts still take their normal rendering precedence;
picker eligibility does not depend on which font happens to be selected.

The catalog retains Unicode source order. Tone and gender variants share a
representative, using a canonical neutral form when available. The generator
explicitly handles irregular families such as handshakes and holding hands;
unknown families remain separate rather than being dropped.

Run from a complete checkout:

```
python3 src/text/emoji/update_emoji_catalog.py --check
python3 src/text/emoji/update_emoji_catalog.py --refresh-coverage
```

The second command regenerates the coverage manifest and catalog. Normal
builds consume checked-in files and need no network or font tooling.
`update_emoji.py` refreshes both after rasterization. The other font and Unicode
update scripts validate consistency and fail if names, versions, catalog, or
glyph coverage need attention. A Unicode version update requires deliberately
updating the vendored input, version, digest, and variant tests here as well.
The font-source fingerprint uses LF line endings so Windows text checkouts
produce the same catalog. The vendored Unicode input retains its pinned bytes.

## Localized names and search

CLDR 49 provides the localized short names (`type="tts"`) and search keywords
from both `common/annotations` and `common/annotationsDerived`. The latter
includes gender, tone, joined, and flag sequences. Variation selectors are
removed only for annotation lookup; inserted emoji retain their original bytes.
The importer also retains CLDR parent locales, language aliases, and likely
subtags for regional and script resolution. A locale's own fields replace
inherited fields; missing names and keywords inherit independently. Later
language preferences and then English supply missing fields. English Unicode
names and CLDR names/keywords remain searchable in every language.

The upstream archive is `https://unicode.org/Public/cldr/49/core.zip`, SHA-256
`78403d8d6fb1fed6e208bf9001bf5be00bb5abfd9f8110dae062bf49309d6a89`.
`data/cldr-49-emoji.json.gz` is a filtered snapshot for the pinned Unicode
emoji inventory, including locales with only partial overrides. Its
**uncompressed** JSON SHA-256 is
`70122165a96b81b6d3bc8fc3d9f2208a2ff9a513f2c3bd881ca95a0ae6f21cb6`.
Compression headers and zlib versions do not affect that input check.
`LICENSE.CLDR` retains the archive's exact notice; its Unicode License v3
applies to these data. It is included in the in-program Licenses viewer.

```
python3 src/text/emoji/update_emoji_locales.py --fetch
python3 src/text/emoji/update_emoji_locales.py --check
```

`--fetch` downloads and verifies the pinned upstream archive, refreshes the
filtered snapshot and license, and regenerates the C tables. `--archive PATH`
uses a previously downloaded archive with the same checksum. With neither
option, generation uses only the checked-in snapshot. The generator encodes
each locale as an independent raw LZ4 block, decoded by the small private
reader in `st_emoji_locale.c`; no compression library or runtime data files are
required. Only requested locales and their parents are decoded. Contexts own
their decoded strings and cached search keys. The shared Unicode matcher
verifies candidate matches at grapheme boundaries.

The base catalog updater also regenerates localized tables when glyph coverage
changes. Its inexpensive index fingerprint check catches stale locale indexes;
`emoji_locales_generated` verifies the entire generated table against pinned
input, and the locale tests cover inheritance, aliases, script choice, names,
keywords, derived sequences, and the packed-data reader.

CTest's `emoji_catalog` checks every entry against the Unicode RGI oracle and
all three actual font banks. `emoji_catalog_generated` and
`emoji_catalog_generator` check reproducibility, pins, coverage, and grouping.
The headless `syncterm_emoji_menu` and `syncterm_emoji_connected` tests exercise
the UI and transmitted bytes; Wren and settings tests cover insertion and
persistence contracts separately.

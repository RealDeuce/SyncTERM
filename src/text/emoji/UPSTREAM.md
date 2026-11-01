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

CTest's `emoji_catalog` checks every entry against the Unicode RGI oracle and
all three actual font banks. `emoji_catalog_generated` and
`emoji_catalog_generator` check reproducibility, pins, coverage, and grouping.
The headless `syncterm_emoji_menu` and `syncterm_emoji_connected` tests exercise
the UI and transmitted bytes; Wren and settings tests cover insertion and
persistence contracts separately.

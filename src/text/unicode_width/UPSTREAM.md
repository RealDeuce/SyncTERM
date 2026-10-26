<!-- Copyright (c) 2026 Stephen Hurd -->
<!-- SPDX-License-Identifier: BSD-2-Clause -->

# Unicode width data

The generated width table is derived from `EastAsianWidth.txt` in the Unicode
Character Database 18.0.0 and `emoji-data.txt` for Unicode Emoji 18.0.
`DerivedCoreProperties.txt` supplies `Default_Ignorable_Code_Point` and
`UnicodeData.txt` supplies general categories `Mn`, `Me`, `Cf`, and `Cc`.
These properties have priority and give zero scalar advance. Otherwise,
SyncTERM treats East Asian Width `W` and `F`, plus the Emoji
`Emoji_Presentation` property, as two columns. Other scalar values, including
ambiguous-width and text-default emoji characters, are one column. This is a
fixed application policy and does not use the process locale or `wcwidth()`.
The grapheme/presentation layer preserves attached marks, selectors, and
joiners and handles emoji sequences as units. An isolated non-ignorable Mn/Me
cluster still gets a one-column visible fallback hint; this is distinct from
the mark's zero scalar advance.

Unicode data files are distributed under the Unicode License v3. See
<https://www.unicode.org/license.txt>. The exact source URL and SHA-256 digest
are recorded in `update_unicode_width.py`. A local regeneration directory uses
the pinned names `EastAsianWidth-18.0.0.txt`, `emoji-data-18.0.0.txt`,
`DerivedCoreProperties-18.0.0.txt`, and `UnicodeData-18.0.0.txt`.

<!-- Copyright (c) 2026 Stephen Hurd -->
<!-- SPDX-License-Identifier: BSD-2-Clause -->

# Unicode width data

The generated width table is derived from `EastAsianWidth.txt` in the Unicode
Character Database 17.0.0 and `emoji-data.txt` for Unicode Emoji 17.0.
SyncTERM treats East Asian Width `W` and `F`, plus the Emoji
`Emoji_Presentation` property, as two columns. Other scalar values, including
ambiguous-width and text-default emoji characters, are one column. This is a
fixed application policy and does not use the process locale or `wcwidth()`.
Variation selectors and emoji sequences are not scalar properties; a later
grapheme/presentation layer must handle those sequences as units.

Unicode data files are distributed under the Unicode License v3. See
<https://www.unicode.org/license.txt>. The exact source URL and SHA-256 digest
are recorded in `update_unicode_width.py`. A local regeneration directory uses
the pinned names `EastAsianWidth-17.0.0.txt` and `emoji-data-17.0.0.txt`.

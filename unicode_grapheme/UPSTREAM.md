<!-- Copyright (c) 2026 Stephen Hurd -->
<!-- SPDX-License-Identifier: BSD-2-Clause -->

# Unicode grapheme and emoji data

The generated tables use Unicode 17.0.0 `GraphemeBreakProperty.txt`,
`DerivedCoreProperties.txt`, and `emoji-data.txt`, plus the Unicode Emoji 17.0
RGI sequence files. They expose properties required by UAX #29 and maximal
matching of standardized RGI emoji sequences; the caller owns grapheme-break
state and policy.

Unicode data files are distributed under the Unicode License v3. Exact source
URLs and SHA-256 digests are recorded in `update_unicode_grapheme.py`.

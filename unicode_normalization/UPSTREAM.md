<!-- Copyright (c) 2026 Stephen Hurd -->
<!-- SPDX-License-Identifier: BSD-2-Clause -->

# Unicode normalization data

The generated tables use `UnicodeData.txt` and
`DerivedNormalizationProps.txt` from Unicode 17.0.0. They expose canonical
decomposition, canonical combining class, and canonical composition primitives;
the caller owns ordering and the complete normalization algorithm.

Unicode data files are distributed under the Unicode License v3. Exact source
URLs and SHA-256 digests are recorded in `update_unicode_normalization.py`.

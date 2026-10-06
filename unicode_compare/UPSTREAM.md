<!-- Copyright (c) 2026 Stephen Hurd -->
<!-- SPDX-License-Identifier: BSD-2-Clause -->

# Unicode comparison data

The generated tables use Unicode 18.0.0 `CaseFolding.txt`, `UnicodeData.txt`,
`PropList.txt`, and the UCA 18.0.0 `allkeys.txt` DUCET.  Exact URLs and
SHA-256 digests are recorded in `update_unicode_compare.py`.

Unicode data files are distributed under the Unicode License v3.  The runtime
uses the default non-Turkic full case fold and non-ignorable DUCET weights; it
does not use the process locale or an operating-system collation service.

The Unicode 18.0.0 `CollationTest.zip` archive has SHA-256
`02af87f32b787fef873eaa9a279420f41c6ea58caa15ed95821e911fc91bf662`.
After extracting it, the complete non-ignorable conformance ordering can be
checked with:

```sh
st_unicode_collation_conformance CollationTest_NON_IGNORABLE.txt
```

Surrogate rows are skipped because SyncTERM accepts strict UTF-8 scalar values,
which cannot encode surrogate code points. DUCET-equal rows are checked without
SyncTERM's final deterministic identical tie-break.

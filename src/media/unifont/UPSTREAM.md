# SyncTERM Unicode font data

`generated/st_unifont_data.c` is built from GNU Unifont 18.0.01 with UNSCII
8x16 glyphs overlaid on matching codepoints. UNSCII wins on duplicates.
Before the overlay, SyncTERM applies the CP437-compatible box drawing
overrides described in `../fonts/UPSTREAM.md`. These retain their
BSD-2-Clause copyright notice; the original CP437 font remains unchanged.
The modified font's primary name is **SyncTERM Unicode**.

GNU Unifont 18.0.01 offers the GNU GPL version 2 or later with the GNU font
embedding exception, or the SIL Open Font License 1.1. SyncTERM selects the
**SIL OFL 1.1** option for this modified font. The UNSCII overlay uses its
upstream public-domain option; see `../fonts/LICENSE.UNSCII`.

`LICENSE.OFL-1.1` contains the font name, modification description, upstream
copyright notice, and full OFL text. The copyright notice comes from the
COPYRIGHT property of `font/precompiled/unifont-18.0.01.bdf.gz` in the
upstream 18.0.01 source archive. The OFL text comes from that archive's
`OFL-1.1.txt` (line endings and trailing whitespace normalized). Source archive:
<https://unifoundry.com/pub/unifont/unifont-18.0.01/unifont-18.0.01.tar.gz>.
See also <https://unifoundry.com/unifont/index.html> for upstream terms.

The font notice and full license are embedded in executables built with
`SYNCTERM_ENABLE_UNIFONT=ON` and can be read under **SyncTERM Settings >
Licenses**, including offline and in safe mode. A build with the option
disabled does not include this font or its catalog entry. The OFL applies
to the font; original SyncTERM code remains under its existing BSD terms.

Exact font input URLs, versions, and SHA-256 digests are in
`src/media/fonts/update_fonts.py`. Regeneration retains the OFL identifier and notice
reference in the generated data header.

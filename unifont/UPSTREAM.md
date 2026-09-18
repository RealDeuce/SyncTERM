# GNU Unifont-derived font data

`generated/st_unifont_data.c` is built from GNU Unifont 17.0.05 with UNSCII
8x16 glyphs overlaid on matching codepoints.  UNSCII wins on duplicates.

GNU Unifont 17.0.05 is distributed under the GNU GPL version 2 or later with
the GNU font embedding exception, or under the SIL Open Font License 1.1.
See <https://unifoundry.com/unifont/index.html> for upstream terms.  This data
is isolated in optional library `st_unifont`.

SyncTERM distributes the derived raster data under GPLv2-or-later. The OFL
alternative is not used: this composite is modified raster data embedded in
the program rather than a standalone font carrying user-visible font naming
and licensing metadata. A SyncTERM executable built with
`SYNCTERM_ENABLE_UNIFONT=ON` must therefore be distributed under
GPLv2-or-later. A build with the option disabled does not contain this data.

Exact source URLs, versions, and SHA-256 digests are in `fonts/update_fonts.py`.

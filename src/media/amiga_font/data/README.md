Amiga bitmap strike sources from IcyTerm commit b568f6a087b7ececb28d0b45edd38f70a6bf692d.

The manifest pins each input and retains its original source path. YAFF headers
retain conversion history and original Workbench/Kickstart provenance. The
upstream repository license is retained in LICENSE-MIT. Native metrics and
encoded byte labels are preserved; generated C belongs in the build directory.

Diamond and Ruby baseline corrections are independently pinned to the native
JR-Comm disk-font metadata supplied with the JR-Comm analysis. Their original
native paths and SHA-256 identities are recorded per strike in the manifest.
No ROM or executable bytes are compiled into the application. The bitmap input
hashes continue to identify the unmodified upstream YAFF files.

Nominal cell widths are retained from native disk-font metadata where those
reference files are available, with per-strike source hashes in the manifest.
Other compiled strikes use the YAFF cell size or bounding width. Glyph kerning
and advances remain independent of this nominal grid width.

Times/15's nominal width is pinned to the native Workbench strike retained in
`FS-UAE/Hard Drives/Default/Fonts/times/15`, with its SHA-256 in the manifest.
It declares `tf_XSize=7`, while the converted YAFF has a 14-pixel bounding box.
Using that bounding width for JR-Comm's receive forecast prematurely wraps the
proportional text on Reign of Fire's welcome screen. The bitmap and individual
glyph advances are unchanged.

The SpitfireTN/syncterm-skypix report collection at commit
647972fb9789deb5942f471b51a43be85578eafe independently supplies matching native
strikes for Emerald, Garnet, Helvetica, Opal, Sapphire/14 and /19, Times/11
through /24, and Topaz/11. All glyph advances and normalized ink positions
were compared before pinning their nominal widths; 18 converted widths needed
correction. Source paths and SHA-256 values are retained in the manifest.
The existing Courier/Times alternate versions and JR-Comm Ruby/Diamond metrics
remain selected rather than replacing fonts solely to match the report bundle.

The native disk references and three-way comparison in `../reference/README.adoc`
establish that all 30 stock native strikes in that report bundle exactly match
the sampled Workbench 2.1/3.1 files. The `workbench-3.1_` YAFF filenames do not
establish compiled equivalence: five Courier conversions and five Ruby/Diamond
metric selections differ from those native files. Twenty compiled stock strikes
match the later native fonts exactly; none matches the entire corresponding
1.3 font. The reference files are not compiled or installed.
The later check also found exact matches for all 30 stock native strikes on a
catalogue-verified Workbench 2.04 Fonts disk, named `AmigaFonts2.0`. A Release 2
source is consistent with the report bundle; the identical 2.04/2.1/3.1 files
cannot identify its exact source release. Archive-labelled 2.0 and 2.05 images
are recorded with their provenance qualifications in the reference README.

report-font-reference.json pins the 38 requested family/size pairs across both
native and SPF collections. Twenty matching native strikes include expected
metrics and canonical FNV-1a-64 ink hashes. These were decoded independently
with SkyPix/tools/amiga_font.py's amitools HUNK relocator, not with SyncTERM.
For each byte (using the native default glyph outside tf_LoChar..tf_HiChar),
proportional bearing/advance are tf_CharKern and tf_CharKern+tf_CharSpace;
monospaced bearing/advance are floor((tf_XSize-glyph_width)/2) and tf_XSize.
Ink Y is row+tf_YSize-tf_Baseline-1. The JSON specifies the hash word order.
The C test hashes the compiled catalog independently and the Python test compares
only the matching variants; other entries assert availability, not equivalence.

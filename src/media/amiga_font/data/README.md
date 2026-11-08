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

# RIPterm 1.54 expected rasters

These are the original primitive `.rip` inputs and compressed reference pixels
from historical **RIPTERM** DOSBox captures. They are not SyncTERM snapshots.
See [the coverage and provenance notes](../../../docs/RIP154_CONFORMANCE.md)
for source hashes, historical commits, exact DAC comparison, command line-ending
normalization and the case-specific capture UI exclusions (including the mouse
pointer tip in two captures).

`captures.json` is the inventory. Each `.rgb6.z` expands with Python's standard
`zlib.decompress()` to 672,000 bytes: 640x350 top-down pixels, RGB order, one byte
per six-bit DAC channel. Full original capture content is retained; exclusions
are metadata, not modifications to expected pixels.

Update through `tools/testing/import_rip154_captures.py` using the original XWD
files. Do not generate expected rasters from the emulator under test.

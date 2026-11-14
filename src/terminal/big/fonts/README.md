# Shared current BGI fonts

BIG and RIP use the same immutable bitmap and CHR font data in
`../../bgi/st_bgi_bitmap.c` and `../../bgi/st_bgi_fonts.c`. There is one compiled
font set. BIG selects CHRs1..4 through borrowed views into that shared data;
RIP continues using bitmap0 and CHRs1..10. The original Borland notices and
file headers remain with the data.

The historical V100 files and their separate embedding script were removed
at the user's request. Do not carry a compatibility font variant unless
actual artwork demonstrates that one is needed. No such artwork has been
identified. Font-version metric/outline changes are intentional; BIG's
protocol layout, scaling, source acquisition and advance rules stay separate.

| Font / BIG ID | Bytes | SHA-256 |
| --- | ---: | --- |
| Triplex / 1 | 16677 | `831963618a5dbf3562e12c3b4b8bab3ed3879143de3c7cd9293be171a44282db` |
| Small / 2 | 5131 | `ea3f22a9bd93e0fd9e2bb7814ac60543100315aaf4e21dd70204561930095449` |
| Sans / 3 | 13596 | `edd4db0e5735ce0872d60137819e2b74a590d069be7585269b7b6d675e319bbc` |
| Gothic / 4 | 18063 | `8620ae9db9dd874b842f67f7153721a5681fef3cd9e99af0fde41f3b1194f183` |

`big_font` checks compiled CRC-32 values, complete header/body access, every
glyph and shared-data pointer identity. Bitmap data has unchanged SHA-256
`5e3e76618612db34d3d821f68fb223f3a446eb6fca871a5796e49e533daac032`.
Its compiled CRC-32 is `f7612cfe`. BIG's zero-to-O source-selection alias
remains protocol policy; there is no separate historical bitmap table.

Both protocols use the common bitmap row expander and CHR record decoder.
Their adapters retain their respective layout, bounds, magnification/read
order, cursor, pen-advance and error rules. No runtime font-file dependency
or foreign ROM/IVT emulator is added.

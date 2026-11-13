# BIG 1.03 original stroke fonts

These complete V100 files come from the supplied BIG 1.03 image, retained in
the local reference book at `~/IGS/docs/big/data/`. Chapter 28 identifies
glyph/layout data; chapter 30 identifies the borrowed startup registrations.
The BF 1.02 copies are byte-identical. Their original headers, including
the 1987 Borland International copyright notices, remain intact. Font data
retain those notices; the surrounding C/CMake code has its own BSD license.

| Font / wire ID | File bytes | Body bytes | SHA-256 |
| --- | ---: | ---: | --- |
| Triplex / 1 | 7241 | 7113 | `b687918d590a22273fbb30967e0e50cb9e4ff9d3c3c97591bc6ec5460d655c9a` |
| Small / 2 | 2138 | 2010 | `dc51f652a1d863273eb291830b3fba5e283b9dbe65b366b0b9eae02e7b237f08` |
| Sans / 3 | 5438 | 5310 | `9bae0df17f137f3b9171f19dde0ebc6090c37677105d3b78ef427e719ddb8036` |
| Gothic / 4 | 8560 | 8432 | `b784419930e54435f28763e9f4cf2854a785082b9b330e7ac93c0f5590b06814` |

All headers are 128 bytes. These are the original older assets, rather
than the newer arrays used by RIP. Gothic A has metric width 22, and Gothic
byte 22 has metric width zero but shares byte 21's stroke/advance program.
Small A has metric width 6 and a separate terminal MOVE to (6, 0).

Configuration verifies the pinned SHA-256 values and embeds every byte in C
generated inside the build tree. There is no runtime font-file dependency,
DOS font search or disk loader. `big_font` checks independent CRC-32 values
over the compiled arrays, headers, every glyph and selected literal records.
The common CHR body/record accessors preserve BYTE/WORD address arithmetic
and reject escaped native extents or unterminated address cycles. BIG's
registry, font selection, layout, scaling and raster semantics remain
separate consumers of that format code. The registry holds borrowed views;
registration does not invalidate a selected/loaded/active font binding.

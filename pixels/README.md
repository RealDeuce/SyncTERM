# `st_pixels` contract

`st_pixels` owns the small, backend-independent pixel-buffer operations used
by SyncTERM image decoding, scripting, and console rasterization.  A buffer
always has a primary plane and may have an alternate plane for blink-state
content.  `st_pixel_buffer` values use the console's native colour
representation: `0..65535` are palette indexes and a value with
`ST_PIXEL_COLOR_RGB_TAG` set contains direct RGB in its low 24 bits.  Fill,
clone, and blit preserve that representation without resolving it.

`st_rgb_buffer` is the deliberately narrower packed-`0x00RRGGBB` type used at
the scaler and host-upload boundary.  Palette resolution happens once in the
bitmap presenter before scaling, so the scaler never receives palette indexes
or tagged colours.

The public structures expose their planes and dimensions because SyncTERM's
image and drawing code performs substantial direct pixel access.  Buffers
created by this library are nonempty, contiguous, row-major allocations.
`st_pixel_buffer_ensure_alternate` initializes a new alternate plane from the
primary plane.  The matching removal and buffer-destruction functions keep
allocation and deallocation on the same component boundary.

Masks are contiguous row-major bit arrays.  The first pixel is the high bit of
the first byte.  Blits use nearest-neighbour integer scaling, independently
clip against the destination, support horizontal and vertical reflection, and
address a mask in unscaled source coordinates.  An alternate source plane is
preserved; where it is absent, the primary source supplies both destination
planes.  Self-blits behave as though the source were snapshotted before any
destination writes.

The persistent scaler accepts only `st_rgb_buffer` input and implements the
legacy scaling plan rather than exposing
its stages as independent filter choices.  Pointy scaling decomposes common
integer factors into pointy and xBR stages, applies residual integer
multiplication, and finishes with YCoCg interpolation where required.  Blocky
scaling uses integer multiplication plus interpolation.  Coordinate and
inverse-damage maps are prepared once per plan.  Each pointy-pipeline stage
retains its output so `st_pixel_scaler_scale_region` can expand damage by the
stage's dependency radius and update only the affected final rectangle.

`st_scaler_benchmark` is a standalone Release-build measurement tool and is
deliberately not a timing-sensitive CTest test.

<!-- Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause -->

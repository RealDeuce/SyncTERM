/* BIG VGA-profile address mapping over indexed pixels, references25/48/56. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_BIG_VIDEO_H_
#define ST_BIG_VIDEO_H_
#include <stddef.h>
#include <stdint.h>

enum { ST_BIG_VIDEO_BYTES=65536, ST_BIG_VIDEO_PENS=ST_BIG_VIDEO_BYTES*8 };
struct st_big_video {
	uint32_t *primary, *alternate;
	size_t count;
};
struct st_big_video_view {
	uint32_t *primary, *alternate;
	size_t count;
	uint16_t start; /* BIOS display-start byte address, separate from CPU ES. */
};
struct st_big_video_pixel {
	uint32_t primary, alternate;
	size_t visible; /* Linear visible index, or SIZE_MAX for backing storage. */
};
/* Borrowed native pixel tokens; no packed bitplanes or RGB conversion.
 * The visible view
 * is authoritative in its displayed address interval, including wrap;
 * backing entries there may be stale. Other addresses use actual backing.
 * Zero backing permits a visible-only binding. No allocation/initialization.
 * All calls are serialized; arrays must outlive their use. */
int st_big_video_bind(struct st_big_video *, uint32_t *primary, uint32_t *alternate, size_t count);
uint16_t st_big_video_active_segment(uint8_t page, uint16_t paragraphs);
uint16_t st_big_video_display_start(uint8_t page, uint16_t bios_page_bytes);
/* CPU segment+WORD offset must actually lie in A0000..AFFFF. Bit0 is the
 * leftmost pen of the addressed byte. No automatic CPU aperture wrap.
 * An optional visible view maps display-counter wrap independently.
 * Read results and write destinations/visible index are unchanged on error. */
int st_big_video_read(const struct st_big_video *, const struct st_big_video_view *,
	uint16_t segment, uint16_t offset, uint8_t bit, struct st_big_video_pixel *);
int st_big_video_write(struct st_big_video *, const struct st_big_video_view *,
	uint16_t segment, uint16_t offset, uint8_t bit, uint32_t primary, uint32_t alternate, size_t *visible);
/* Save the displayed interval to backing before changing views; load the
 * requested interval into the visible arrays afterward. Arrays must not
 * alias backing. Calls retain accepted prefixes on a backing-extent error;
 * accepted is always written. View metadata/publication belongs to caller. */
int st_big_video_save(struct st_big_video *, const struct st_big_video_view *, size_t *accepted);
int st_big_video_load(const struct st_big_video *, const struct st_big_video_view *, size_t *accepted);
#endif

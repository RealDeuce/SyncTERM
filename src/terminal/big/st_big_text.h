/* BIG text attributes; custom ratios are independent of bitmap magnification. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_BIG_TEXT_H_
#define ST_BIG_TEXT_H_
#include "st_big_state.h"
struct st_big_fonts;
struct st_big_text_sink {
	void *context;
	/* Optional serialized device custom-style call. The ordinary EGAVGA
	 * negative-direction entry returns without changing device state.
	 * Returned dimensions feed DDO caches; saved ratios use original args.
	 * Failure retains accepted effects and omits subsequent stores/tails. */
	int (*custom)(void *, const uint16_t arguments[4], uint16_t dimensions[2]);
};
/* Public setter: unsigned physical-max validation, dispatch or silent skip,
 * then public size0 only on normal return. No own glyph/point/pixel call.
 * NULL sink is the actual ordinary EGAVGA custom-style early return. */
int st_big_text_custom(struct st_big_state *, const uint16_t ratios[4], const struct st_big_text_sink *);
/* X3's four completed class0 values independently normalize signed1..4. */
int st_big_text_x3(struct st_big_state *, const uint16_t ratios[4], const struct st_big_text_sink *);
/* Ordinary supplied VGA settextstyle. Public WORDs precede narrowed DDO
 * and device stores. Borrowed registry only; missing resources fall back
 * with BGI-8 rather than opening a host font file. Direction is0 or1.
 * A NULL registry is sufficient for the font0 defaults shortcut. */
int st_big_text_style(struct st_big_state *, struct st_big_fonts *, uint16_t font, uint16_t direction, uint16_t size);
/* E reads font,size,direction completely before independent signed clamps. */
int st_big_text_e(struct st_big_state *, struct st_big_fonts *, const uint16_t arguments[3]);
#endif

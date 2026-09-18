/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_SCALER_INTERNAL_H_
#define ST_SCALER_INTERNAL_H_

#include <stdint.h>

void st_xbr_filter(const uint32_t *data, uint32_t *out, int width,
	int height, int multiplier);
void st_xbr_filter_region(const uint32_t *data, uint32_t *out, int width,
	int height, int multiplier, uint32_t x, uint32_t y,
	uint32_t region_width, uint32_t region_height);
void st_pointy_scale_odd(const uint32_t *source, uint32_t *destination,
	int width, int height, int multiplier);
void st_pointy_scale_odd_region(const uint32_t *source,
	uint32_t *destination, int width, int height, int multiplier,
	uint32_t x, uint32_t y, uint32_t region_width, uint32_t region_height);
void st_pointy_scale5(const uint32_t *source, uint32_t *destination,
	int width, int height);
void st_pointy_scale5_region(const uint32_t *source, uint32_t *destination,
	int width, int height, uint32_t x, uint32_t y, uint32_t region_width,
	uint32_t region_height);
void st_pointy_scale3(const uint32_t *source, uint32_t *destination,
	int width, int height);
void st_pointy_scale3_region(const uint32_t *source, uint32_t *destination,
	int width, int height, uint32_t x, uint32_t y, uint32_t region_width,
	uint32_t region_height);

#endif

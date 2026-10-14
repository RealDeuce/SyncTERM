/* RIP .ICN four-plane file format. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef CTERM_RIP_ICON_FILE_H_
#define CTERM_RIP_ICON_FILE_H_

#include <stddef.h>
#include <stdint.h>

/* Output buffers are allocated; the caller frees them. Extra file bytes are
 * allowed because legacy icon readers ignore the optional trailing marker. */
int cterm_rip_icon_decode(uint8_t **pixels, uint32_t *width,
    uint32_t *height, const uint8_t *bytes, size_t size);
int cterm_rip_icon_encode(uint8_t **bytes, size_t *size,
    const uint8_t *pixels, uint32_t width, uint32_t height);

#endif

/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_transfer_detect.h"

int
st_transfer_detect(struct st_transfer_detector *detector, uint8_t byte)
{
	static const uint8_t prefix[] = {0x18, 'B', '0'};

	if (detector->matched == sizeof(prefix)) {
		detector->matched = 0;
		if (byte == '0')
			return 1;
		if (byte == '1')
			return 2;
	}
	if (byte == prefix[detector->matched])
		++detector->matched;
	else
		detector->matched = byte == prefix[0] ? 1 : 0;
	return 0;
}

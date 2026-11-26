/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_CONTROLLER_EVENT_H_
#define ST_CONTROLLER_EVENT_H_
#include <stdint.h>

enum st_controller_record {
	ST_PAD_ADD = 1,
	ST_PAD_CAP,
	ST_PAD_VALUE,
	ST_PAD_FRAME,
	ST_PAD_REMOVE
};

struct st_controller_event {
	enum st_controller_record record;
	uint32_t                  device;
	unsigned                  type, code;
	int                       minimum, maximum, value;
	unsigned                  device_class, source;
};

#endif

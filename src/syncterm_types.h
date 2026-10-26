/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef SYNCTERM_TYPES_H_
#define SYNCTERM_TYPES_H_

#include "syncterm_config.h"

#if SYNCTERM_HAVE_SSIZE_T
	#include <sys/types.h>
#elif SYNCTERM_PLATFORM_WINDOWS
	#include <basetsd.h>
	typedef SSIZE_T ssize_t;
#else
	#error "ssize_t is unavailable on this target"
#endif

#endif

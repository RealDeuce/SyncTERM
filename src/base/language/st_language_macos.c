/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_language_internal.h"

#include <CoreFoundation/CoreFoundation.h>
#include <string.h>

int
st_language_macos(struct st_language_preferences *result)
{
	CFArrayRef languages = CFLocaleCopyPreferredLanguages();
	int status = 0;

	if (languages == NULL)
		return 0;
	for (CFIndex i = 0; i < CFArrayGetCount(languages); i++) {
		CFStringRef language = CFArrayGetValueAtIndex(languages, i);
		char tag[256];

		if (CFGetTypeID(language) != CFStringGetTypeID()
		    || !CFStringGetCString(language, tag, sizeof(tag),
		        kCFStringEncodingASCII))
			continue;
		status = st_language_add(result, tag, strlen(tag));
		if (status != 0)
			break;
	}
	CFRelease(languages);
	return status;
}

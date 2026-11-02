/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */

#include "wren.h"

#include <stdio.h>
#include <stdlib.h>

static void
report_error(WrenVM *vm, WrenErrorType type, const char *module,
	int line, const char *message)
{
	(void)vm;
	(void)type;
	fprintf(stderr, "%s:%d: %s\n", module == NULL ? "?" : module, line, message);
}

int
main(void)
{
	WrenConfiguration config;
	wrenInitConfiguration(&config);
	config.errorFn = report_error;
	WrenVM *vm = wrenNewVM(&config);
	if (vm == NULL)
		return EXIT_FAILURE;
	WrenInterpretResult result = wrenInterpret(vm, "unicode_string",
	    "var text = \"\\u00e9\\U0001f600e\\u0301\\0\"\n"
	    "if (text.count != 5 || text.bytes.count != 10) Fiber.abort(\"scalar and byte lengths\")\n"
	    "var scalars = text.codePoints.toList\n"
	    "if (scalars.count != 5 || scalars[0] != 233 || scalars[1] != 128512 || scalars[2] != 101 || scalars[3] != 769 || scalars[4] != 0) Fiber.abort(\"scalar iteration\")\n"
	    "var characters = text.toList\n"
	    "if (characters.count != 5 || characters[1] != \"\\U0001f600\" || characters[3] != \"\\u0301\" || characters[4].bytes[0] != 0) Fiber.abort(\"character iteration\")\n"
	    "// String and codePoints subscripts take byte offsets, unlike Lists.\n"
	    "if (text[2] != \"\\U0001f600\" || text.codePoints[2] != 128512 || text.codePoints[1] != -1) Fiber.abort(\"byte-offset subscripts\")\n"
	    "if (text[1].bytes.count != 1 || text[1].bytes[0] != 169) Fiber.abort(\"continuation byte subscript\")\n"
	    "if (text.indexOf(\"\\U0001f600\") != 2 || text.bytes[9] != 0) Fiber.abort(\"byte search and embedded NUL\")\n"
	    "// Ranges visit byte offsets but decode complete scalars at each lead.\n"
	    "if (text[0...1] != \"\\u00e9\" || text[1...2] != \"\") Fiber.abort(\"range scalar expansion\")\n"
	    "if (\"\\u00e9\" == \"e\\u0301\" || \"e\\u0301\".count != 2) Fiber.abort(\"no implicit NFC or grapheme count\")\n"
	    "var invalid = String.fromByte(255)\n"
	    "if (invalid.count != 1 || invalid.bytes.count != 1 || invalid.codePoints.toList[0] != -1) Fiber.abort(\"count does not validate UTF-8\")\n");
	wrenFreeVM(vm);
	return result == WREN_RESULT_SUCCESS ? EXIT_SUCCESS : EXIT_FAILURE;
}

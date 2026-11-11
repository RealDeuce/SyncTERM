/* IGS signed-word math and numeric console/shared-buffer formatting. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_igs.h"
#include "cterm_vt52.h"
#include "io_error.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static bool
valid_index(int index)
{
	return index >= 0 && index < 128;
}

int
cterm_igs_math_write(struct cterm_igs *igs, int operation, int a, int b, int index)
{
	if (!valid_index(index))
		return IO_ERROR_OUT_OF_RANGE;
	int32_t value = a;
	switch (operation) {
	case 0: break;
	case 1: value += b; break;
	case 2: value -= b; break;
	case 3: value *= b; break;
	case 4:
	case 5:
		if (b == 0) return IO_ERROR_OUT_OF_RANGE;
		/* Widen before dividing: -32768/-1 has a defined quotient and
		 * mathematical remainder, independent of 68000 overflow state. */
		value = operation == 4 ? value / b : value % b;
		break;
	default: return IO_ERROR_INVALID_ARGUMENT;
	}
	uint16_t word = (uint16_t)value;
	igs->math[index] = (int16_t)(word > 32767 ? (int32_t)word - 65536 : word);
	return 0;
}

int
cterm_igs_math_clear(struct cterm_igs *igs, int first, int last)
{
	if (first < 0)
		return IO_ERROR_OUT_OF_RANGE;
	if (first > 127) first = 127;
	/* Increment in the host domain so last=32767 still includes word 127. */
	int end = last >= 127 ? 128 : last + 1;
	if (end > first)
		memset(igs->math + first, 0, (size_t)(end-first) * sizeof(*igs->math));
	return 0;
}

static int
decimal(const struct cterm_igs *igs, int index, char text[7])
{
	if (!valid_index(index))
		return IO_ERROR_OUT_OF_RANGE;
	/* Six decimal characters plus NUL cover every signed word. */
	return snprintf(text, 7, "%d", (int)igs->math[index]);
}

int
cterm_igs_math_format(struct cterm_igs *igs, int index)
{
	char text[7];
	int size = decimal(igs, index, text);
	if (size < 0) return size;
	memcpy(igs->shared_text, text, (size_t)size + 1);
	igs->shared_text_size = (size_t)size;
	return 0;
}

int
cterm_igs_math_console(struct cterm_igs *igs, int index)
{
	char text[7];
	int size = decimal(igs, index, text);
	if (size < 0) return size;
	int status = cterm_vt52_output(igs->terminal, (const uint8_t *)text, (size_t)size);
	static const uint8_t newline[] = {'\r', '\n'};
	return status == 0 ? cterm_vt52_output(igs->terminal, newline, sizeof(newline)) : status;
}

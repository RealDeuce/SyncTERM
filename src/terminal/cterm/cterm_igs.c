/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_igs.h"
#include "cterm_igs_audio.h"
#include "cterm_igs_music.h"
#include "cterm.h"
#include "cterm_internal.h"
#include "cterm_render.h"
#include "cterm_vt52.h"
#include "io_error.h"
#include "st_time.h"
#include "st_graphics_parser.h"
#include "st_igs_image.h"

#include <stdlib.h>
#include <string.h>

static const uint8_t attention[] = {'G','#'};
static const struct st_graphics_dialect dialect = {
	.attention = attention, .attention_size = sizeof(attention),
	.chain_indicator = true, .chain = '>', .number_skip_crlf = true,
	.number_digits = 4,
};
/* Fixed unscaled signatures share prefixes; counts remain per command. */
static const struct st_graphics_parameter unscaled_parameters[] = {
	{.kind = ST_GRAPHICS_NUMBER}, {.kind = ST_GRAPHICS_NUMBER},
	{.kind = ST_GRAPHICS_NUMBER}, {.kind = ST_GRAPHICS_NUMBER},
	{.kind = ST_GRAPHICS_NUMBER}, {.kind = ST_GRAPHICS_NUMBER},
	{.kind = ST_GRAPHICS_NUMBER}, {.kind = ST_GRAPHICS_NUMBER},
	{.kind = ST_GRAPHICS_NUMBER},
};
static const struct st_graphics_parameter point_parameters[] = {
	{.kind = ST_GRAPHICS_NUMBER, .axis = ST_GRAPHICS_X},
	{.kind = ST_GRAPHICS_NUMBER, .axis = ST_GRAPHICS_Y},
	{.kind = ST_GRAPHICS_NUMBER, .axis = ST_GRAPHICS_X},
	{.kind = ST_GRAPHICS_NUMBER, .axis = ST_GRAPHICS_Y},
};
/* Extended selectors reuse the same descriptor and parameter data as the
 * top-level byte grammar. X itself owns the unscaled selector field. */
static int palette_group_next(void *, const struct st_graphics_command_view *, struct st_graphics_parameter *);
static int pattern_load_next(void *, const struct st_graphics_command_view *, struct st_graphics_parameter *);
static int random_range_next(void *, const struct st_graphics_command_view *, struct st_graphics_parameter *);
static int math_next(void *, const struct st_graphics_command_view *, struct st_graphics_parameter *);
static int bitmap_next(void *, const struct st_graphics_command_view *, struct st_graphics_parameter *);
static int unscaled_tail_next(void *, const struct st_graphics_command_view *, struct st_graphics_parameter *);
static const struct st_graphics_command extended_commands[] = {
	{0, point_parameters, 4, unscaled_tail_next},
	{1, unscaled_parameters, 2, NULL},
	{2, unscaled_parameters, 1, random_range_next},
	{7, unscaled_parameters, 1, pattern_load_next},
	{8, unscaled_parameters, 4, NULL},
	{10, point_parameters, 2, NULL},
	{11, unscaled_parameters, 2, bitmap_next},
	{12, unscaled_parameters, 1, palette_group_next},
	{13, unscaled_parameters, 1, math_next},
	{14, unscaled_parameters, 1, NULL},
	{15, unscaled_parameters, 9, NULL},
};

/* Dynamic arity inspects a private numeric-state copy. Real operands,
 * including a random selector/kind, advance once at execution. */
static int
preview_first(void *context, const struct st_graphics_command_view *view, int *value)
{
	if (view->count == 0)
		return IO_ERROR_PROTOCOL;
	struct cterm_igs preview = *(struct cterm_igs *)context;
	return cterm_igs_argument_value(&preview, &view->arguments[0], value);
}

static const struct st_graphics_parameter text_parameter = {
	.kind = ST_GRAPHICS_TEXT, .size = 128, .terminator = '@',
	.skip_crlf = true, .empty_is_terminator = true,
};

static bool
reuse_text(const struct cterm_igs *igs)
{
	return (igs->loop_mode == 1 && igs->loop_kind != '@')
	    || (igs->loop_mode == 0 && igs->loop_kind == 'Z');
}

static int
bitmap_next(void *context, const struct st_graphics_command_view *view,
    struct st_graphics_parameter *result)
{
	*result = (struct st_graphics_parameter){.kind = ST_GRAPHICS_END};
	if (view->count == 3) return 0;
	struct cterm_igs preview = *(struct cterm_igs *)context;
	int operation, section;
	int status = cterm_igs_argument_value(&preview,&view->arguments[0],&operation);
	if (status != 0) return status;
	if (operation == 0) *result = unscaled_parameters[0];
	else if (operation == 1 || operation == 2) {
		status = cterm_igs_argument_value(&preview,&view->arguments[1],&section);
		if (status == 0) *result = (struct st_graphics_parameter){.kind = ST_GRAPHICS_BYTES,
		    .size = cterm_igs_bitmap_size(section)};
	}
	return status;
}

static int
music_next(void *context, const struct st_graphics_command_view *view,
    struct st_graphics_parameter *result)
{
	struct cterm_igs *igs = context;
	struct cterm_igs_music *m = igs->music;
	*result = (struct st_graphics_parameter){.kind = ST_GRAPHICS_END};
	int operation = m->operation;
	int status = m->command ? 0 : preview_first(context, view, &operation);
	if (status != 0) return status;
	if (operation == 2 || operation == 5 || operation == 6) {
		if (operation == 6 && m->count > 5 && view->count < 3) *result = unscaled_parameters[0];
	} else if (view->count == 1) *result = unscaled_parameters[0];
	else if (view->count == 2) {
		struct cterm_igs preview = *igs;
		int count;
		status = cterm_igs_argument_value(&preview, &view->arguments[1], &count);
		if (status == 0) *result = (struct st_graphics_parameter){.kind = ST_GRAPHICS_BYTES,
		    .size = count > 0 && count <= CTERM_IGS_MUSIC_BYTES ? (size_t)count : 0};
	}
	return status;
}

static int
math_next(void *context, const struct st_graphics_command_view *view,
    struct st_graphics_parameter *result)
{
	*result = (struct st_graphics_parameter){.kind = ST_GRAPHICS_END};
	struct cterm_igs preview = *(struct cterm_igs *)context;
	if (preview.math_printing) {
		if (view->count < 5) *result = point_parameters[view->count-3];
		return 0;
	}
	int operation;
	int status = cterm_igs_argument_value(&preview, &view->arguments[0], &operation);
	if (status != 0) return status;
	if (operation < 0 || operation > 7) return 0;
	size_t count = operation >= 1 && operation <= 5 ? 4 : 3;
	if (operation == 7 && view->count >= 2) {
		int kind;
		status = cterm_igs_argument_value(&preview, &view->arguments[1], &kind);
		if (status != 0) return status;
		if (kind == 1) count = 5;
	}
	if (view->count < count)
		*result = operation == 7 && view->count >= 3
		    ? point_parameters[view->count-3] : unscaled_parameters[0];
	return 0;
}

static int
pattern_load_next(void *context, const struct st_graphics_command_view *view,
    struct st_graphics_parameter *result)
{
	*result = (struct st_graphics_parameter){.kind = ST_GRAPHICS_END};
	int slot;
	const struct cterm_igs *igs = context;
	int status = 0;
	if (igs->pattern_loading)
		slot = igs->pattern_slot;
	else
		status = preview_first(context, view, &slot);
	if (status != 0)
		return status;
	/* Native out-of-slot stores escape into unrelated data or code. */
	if (slot < 0 || slot > 7)
		return IO_ERROR_OUT_OF_RANGE;
	if (view->count < 17)
		*result = reuse_text(context) ? (struct st_graphics_parameter){.kind = ST_GRAPHICS_BYTES}
		    : text_parameter;
	return 0;
}

static int
palette_group_base(int group)
{
	unsigned base = ((unsigned)(group > 3 ? 3 : group) & 0x3fffu) * 4u;
	return base > 32767 ? (int)base - 65536 : (int)base;
}

/* Run the same ordered stores on private state for arity and execution.
 * A later r/R operand must see earlier stores, including a negative lower
 * bound until the second operand has been evaluated. */
static int
random_range_arguments(struct cterm_igs *igs, const struct st_graphics_command_view *view,
    bool complete, bool *extra)
{
	if (view->count < 1 || view->count > 3)
		return IO_ERROR_PROTOCOL;
	int first, second;
	int status = cterm_igs_argument_value(igs, &view->arguments[0], &first);
	if (status != 0) return status;
	igs->random_range[0][0] = first;
	*extra = false;
	if (view->count == 1)
		return complete ? IO_ERROR_PROTOCOL : 0;
	status = cterm_igs_argument_value(igs, &view->arguments[1], &second);
	if (status != 0) return status;
	if (second != igs->random_range[0][0])
		igs->random_range[0][1] = second;
	if (igs->random_range[0][0] < 0)
		igs->random_range[0][0] = 0;
	*extra = igs->random_range[0][0] == second;
	if (*extra) {
		igs->random_range[1][0] = igs->random_range[0][0];
		if (view->count == 3) {
			status = cterm_igs_argument_value(igs, &view->arguments[2], &igs->random_range[1][1]);
			if (status != 0) return status;
		}
		else if (complete)
			return IO_ERROR_PROTOCOL;
	}
	else if (view->count == 3)
		return IO_ERROR_PROTOCOL;
	if (complete)
		for (unsigned i = 0; i < 2; ++i)
			if (igs->random_range[i][1] < igs->random_range[i][0])
				/* Keep the maximum valid word as a singleton, rather than
				 * wrapping into an impossible, negative upper bound. */
				igs->random_range[i][1] = igs->random_range[i][0] < 32767
				    ? igs->random_range[i][0] + 1 : 32767;
	return 0;
}

static int
random_range_next(void *context, const struct st_graphics_command_view *view,
    struct st_graphics_parameter *result)
{
	*result = (struct st_graphics_parameter){.kind = ST_GRAPHICS_END};
	if (view->count == 3)
		return 0;
	struct cterm_igs preview = *(struct cterm_igs *)context;
	bool extra;
	int status = random_range_arguments(&preview, view, false, &extra);
	if (status == 0 && (view->count == 1 || extra))
		*result = unscaled_parameters[0];
	return status;
}

static size_t
palette_group_count(int base)
{
	/* Only this reachable base has a wrapped, negative upper bound. */
	return base == 32764 ? 0 : 4;
}

static int
palette_group_next(void *context, const struct st_graphics_command_view *view,
    struct st_graphics_parameter *result)
{
	*result = (struct st_graphics_parameter){.kind = ST_GRAPHICS_END};
	int group;
	int status = preview_first(context, view, &group);
	if (status == 0 && view->count - 1 < palette_group_count(palette_group_base(group)))
		*result = unscaled_parameters[0];
	return status;
}

static int
type_next(void *context, const struct st_graphics_command_view *view,
    struct st_graphics_parameter *result)
{
	*result = (struct st_graphics_parameter){.kind = ST_GRAPHICS_END};
	if (view->count >= 3)
		return 0;
	int kind;
	int status = preview_first(context, view, &kind);
	if (status == 0 && (kind == 1 || kind == 2))
		*result = unscaled_parameters[0];
	return status;
}

static int
effect_next(void *context, const struct st_graphics_command_view *view,
    struct st_graphics_parameter *result)
{
	int selector = -1;
	int status = preview_first(context,view,&selector);
	size_t count = selector == 20 ? 7 : selector == 22 || selector == 23 ? 2 : 1;
	*result = status == 0 && view->count < count ? unscaled_parameters[0]
	    : (struct st_graphics_parameter){.kind = ST_GRAPHICS_END};
	return status;
}

static int
text_next(void *context, const struct st_graphics_command_view *view,
    struct st_graphics_parameter *result)
{
	const struct cterm_igs *igs = context;
	*result = view->count == 2 && !reuse_text(igs)
	    ? text_parameter
	    : (struct st_graphics_parameter){.kind = ST_GRAPHICS_END};
	return 0;
}

static int
unscaled_tail_next(void *context, const struct st_graphics_command_view *view,
    struct st_graphics_parameter *result)
{
	(void)context;
	*result = view->count == 4 ? unscaled_parameters[0]
	    : (struct st_graphics_parameter){.kind = ST_GRAPHICS_END};
	return 0;
}

static int
arc_next(void *context, const struct st_graphics_command_view *view,
    struct st_graphics_parameter *result)
{
	(void)context;
	/* Both coordinate prefixes are followed by exactly two unscaled angles.
	 * Use their metadata so the X/Y/X and X/Y/X/Y forms share this tail. */
	size_t n = view->count;
	bool complete = n >= 2 && view->arguments[n-1].axis == ST_GRAPHICS_UNSCALED
	    && view->arguments[n-2].axis == ST_GRAPHICS_UNSCALED;
	*result = complete ? (struct st_graphics_parameter){.kind = ST_GRAPHICS_END}
	    : unscaled_parameters[0];
	return 0;
}

static int
point_count(int count, int minimum)
{
	return count < minimum ? minimum : count > CTERM_IGS_MAX_POINTS ? CTERM_IGS_MAX_POINTS : count;
}

static int
counted_points_next(void *context, const struct st_graphics_command_view *view,
    struct st_graphics_parameter *result)
{
	*result = (struct st_graphics_parameter){.kind = ST_GRAPHICS_END};
	int count;
	int status = preview_first(context, view, &count);
	if (status == 0 && view->count-1 < (size_t)point_count(count, view->code == 'f' ? 1 : 2)*2)
		*result = point_parameters[(view->count-1)%2];
	return status;
}

static int
grab_next(void *context, const struct st_graphics_command_view *view,
    struct st_graphics_parameter *result)
{
	int operation;
	int status = preview_first(context,view,&operation);
	if (status != 0) return status;
	size_t count = operation == 0 || operation == 3 || operation == 4 ? 8 : operation == 1 ? 6 : 4;
	*result = view->count < count ? point_parameters[(view->count-2)%2]
	    : (struct st_graphics_parameter){.kind = ST_GRAPHICS_END};
	return 0;
}

static int
extended_next(void *context, const struct st_graphics_command_view *view,
    struct st_graphics_parameter *result)
{
	*result = (struct st_graphics_parameter){.kind = ST_GRAPHICS_END};
	int selector;
	struct cterm_igs preview = *(struct cterm_igs *)context;
	int status = 0;
	if (preview.bitmap_loading)
		selector = 11;
	else if (preview.math_printing)
		selector = 13;
	else if (preview.pattern_loading)
		selector = 7;
	else
		status = cterm_igs_argument_value(&preview, &view->arguments[0], &selector);
	if (status != 0)
		return status;
	for (size_t i = 0; i < sizeof(extended_commands)/sizeof(*extended_commands); ++i) {
		const struct st_graphics_command *command = &extended_commands[i];
		if (selector == command->code) {
			struct st_graphics_command_view tail = {.code = command->code,
			    .chained = view->chained, .arguments = view->arguments + 1, .count = view->count - 1};
			if (tail.count < command->count)
				*result = command->parameters[tail.count];
			else if (command->next != NULL)
				/* Dynamic payloads see numeric state after the selector;
				 * neither this preview nor their own commits real state. */
				status = command->next(&preview, &tail, result);
			break;
		}
	}
	return status;
}

static const struct st_graphics_command commands[] = {
	{'G', unscaled_parameters, 2, grab_next},
	{'b', unscaled_parameters, 1, effect_next},
	{'n', unscaled_parameters, 6, NULL},
	{'N', unscaled_parameters, 1, music_next},
	{'I', unscaled_parameters, 1, NULL},
	{'R', unscaled_parameters, 2, NULL},
	{'g', unscaled_parameters, 1, NULL},
	{'M', unscaled_parameters, 1, NULL},
	{'C', unscaled_parameters, 2, NULL},
	{'c', unscaled_parameters, 2, NULL},
	{'p', unscaled_parameters, 2, NULL},
	{'m', unscaled_parameters, 2, NULL},
	{'r', unscaled_parameters, 1, NULL},
	{'k', unscaled_parameters, 1, NULL},
	{'v', unscaled_parameters, 1, NULL},
	{'w', unscaled_parameters, 1, NULL},
	{'l', unscaled_parameters, 1, NULL},
	{'s', unscaled_parameters, 1, NULL},
	{'d', unscaled_parameters, 1, NULL},
	{'i', unscaled_parameters, 2, NULL},
	{'t', unscaled_parameters, 1, NULL},
	{'q', unscaled_parameters, 1, NULL},
	{'S', unscaled_parameters, 4, NULL},
	{'H', unscaled_parameters, 1, NULL},
	{'A', unscaled_parameters, 3, NULL},
	{'T', unscaled_parameters, 1, type_next},
	{'P', point_parameters, 2, NULL},
	{'L', point_parameters, 4, NULL},
	{'D', point_parameters, 2, NULL},
	{'z', unscaled_parameters, 1, counted_points_next},
	{'f', unscaled_parameters, 1, counted_points_next},
	{'F', point_parameters, 2, NULL},
	{'E', unscaled_parameters, 3, NULL},
	{'W', point_parameters, 2, text_next},
	{'Z', point_parameters, 4, NULL},
	{'B', point_parameters, 4, unscaled_tail_next},
	{'U', point_parameters, 4, unscaled_tail_next},
	{'O', point_parameters, 3, NULL},
	{'Q', point_parameters, 4, NULL},
	{'K', point_parameters, 3, arc_next},
	{'J', point_parameters, 4, arc_next},
	{'V', point_parameters, 3, arc_next},
	{'Y', point_parameters, 4, arc_next},
	{'X', unscaled_parameters, 1, extended_next},
};

static void
cancel_input(struct cterm_igs *igs)
{
	igs->reset_pending = false;
	cterm_render_record_error(igs->terminal, cterm_igs_music_cancel(igs));
	st_graphics_parser_cancel(igs->parser);
	igs->input_deadline = igs->text_deadline = igs->wait_deadline = 0;
	igs->pending_resolution = -1;
	igs->pending_palette = 0;
	igs->timed_pause = igs->pause_key = false;
	igs->note = (struct cterm_igs_note){0};
	igs->pattern_loading = false;
	igs->pattern_slot = 0;
	igs->math_printing = false;
	igs->copy_pending = false;
	cterm_igs_bitmap_cancel(igs);
	cterm_igs_palette_rotation_cancel(igs);
	cterm_igs_sweep_cancel(igs);
}

static int
text(void *context, const uint8_t *bytes, size_t size)
{
	struct cterm_igs *igs = context;
	size_t used;
	int status = cterm_input_write(igs->terminal, bytes, size, &used, CTERM_INPUT_WIRE);
	if (status == 0 && used != size)
		status = IO_ERROR_IO;
	/* A semantic hook can reset CTerm while text is being presented. Leave
	 * the parser callback before resetting its state or consuming more wire. */
	if (status == 0 && igs->reset_pending)
		status = IO_ERROR_INTERRUPTED;
	return status;
}

static int
boundary(void *context)
{
	struct cterm_igs *igs = context;
	int status = cterm_text_boundary(igs->terminal);
	if (status == 0 && igs->terminal->status_sub != NULL)
		status = cterm_text_boundary(igs->terminal->status_sub);
	if (status == 0 && igs->reset_pending)
		status = IO_ERROR_INTERRUPTED;
	return status;
}

int
cterm_igs_echo_backspace(struct cterm_igs *igs)
{
	if (!igs->destructive_backspace || igs->reset_pending)
		return 0;
	const uint8_t erase[] = {' ', 8};
	return cterm_vt52_output(igs->terminal, erase, sizeof(erase));
}

static int
scale_number(const struct cterm_igs *igs, enum st_graphics_axis axis, unsigned value, int *result)
{
	if (value > 9999)
		return IO_ERROR_OUT_OF_RANGE;
	*result = (int)value;
	if (axis == ST_GRAPHICS_UNSCALED || igs->scaling_mode == 0 || value == 0)
		return 0;
	if (igs->scaling_resolution < 0 || igs->scaling_resolution > 2)
		return IO_ERROR_INVALID_ARGUMENT;
	if (igs->scaling_mode == 2) {
		if (axis == ST_GRAPHICS_Y && igs->scaling_resolution == 2)
			*result = (int)(value * 2);
	}
	else if (igs->scaling_mode == 1) {
		unsigned maximum = axis == ST_GRAPHICS_X
		    ? (igs->scaling_resolution == 0 ? 319 : 639)
		    : (igs->scaling_resolution == 2 ? 399 : 199);
		/* IG221.TXT's intended normalized canvas. Correct the original
		 * integer quotient before float conversion; no foreign FP model
		 * or accidental word overflow is required for these bounds. */
		*result = (int)((value * maximum + 4999) / 9999);
	}
	else
		return IO_ERROR_INVALID_ARGUMENT;
	return 0;
}

int
cterm_igs_argument_value(struct cterm_igs *igs, const struct st_graphics_argument *argument, int *result)
{
	if (igs == NULL || argument == NULL || result == NULL
	    || argument->kind != ST_GRAPHICS_NUMBER
	    || argument->axis < ST_GRAPHICS_UNSCALED || argument->axis > ST_GRAPHICS_Y)
		return IO_ERROR_INVALID_ARGUMENT;
	const struct st_graphics_operand *operand = &argument->operand;
	int value;
	if (operand->operation != ST_GRAPHICS_VALUE)
		return IO_ERROR_PROTOCOL;
	switch (operand->source) {
	case ST_GRAPHICS_CONSTANT:
	case ST_GRAPHICS_ARRAY: {
		int status = scale_number(igs, argument->axis, operand->value, &value);
		if (status != 0)
			return status;
		/* Scale the decimal index before lookup, never the loaded value. */
		if (operand->source == ST_GRAPHICS_ARRAY)
			value = igs->math[value > 127 ? 127 : value];
		break;
	}
	case ST_GRAPHICS_RANDOM_R:
	case ST_GRAPHICS_RANDOM_R2: {
		unsigned which = operand->source == ST_GRAPHICS_RANDOM_R ? 0 : 1;
		int lower = igs->random_range[which][0], upper = igs->random_range[which][1];
		if (lower > upper || upper < 0 || lower > 32767 || upper > 32767)
			return IO_ERROR_OUT_OF_RANGE;
		bool found = false;
		/* Low 16/24 seed bits have full periods 2^16/2^24. Checking one
		 * period cannot falsely reject a valid byte/word interval. */
		unsigned limit = upper < 256 ? 65536u : 16777216u;
		for (unsigned tries = 0; tries < limit; ++tries) {
			unsigned raw = cterm_igs_random(igs);
			unsigned word = raw & 0xffff;
			value = upper < 256 ? (int)(raw & 255)
			    : word >= 32768 ? (int)word - 65536 : (int)word;
			if (value >= lower && value <= upper) { found = true; break; }
		}
		if (!found)
			return IO_ERROR_OUT_OF_RANGE;
		break;
	}
	default:
		return IO_ERROR_PROTOCOL;
	}
	*result = value;
	return 0;
}

int
cterm_igs_flow(struct cterm_igs *igs, bool on)
{
	if (igs->flow_mode == 0)
		return 0;
	/* The existing response transport queues output and owns readiness.
	 * No foreign BIOS polling or per-byte throughput delay belongs here. */
	for (int i = 0; i < (on ? igs->flow_on_count : igs->flow_off_count); ++i) {
		char byte = (char)(on ? igs->flow_on : igs->flow_off);
		cterm_respond(igs->terminal, &byte, 1);
		if (igs->reset_pending)
			return IO_ERROR_INTERRUPTED;
	}
	return 0;
}

static int
end_pause(struct cterm_igs *igs)
{
	igs->timed_pause = igs->pause_key = false;
	igs->wait_deadline = 0;
	/* Exit uses the current loop and flow settings, independently of entry. */
	return igs->loop_mode == 1 ? 0 : cterm_igs_flow(igs, true);
}

static int
begin_pause(struct cterm_igs *igs, int seconds)
{
	uint64_t now;
	int status = igs->now_ms(&now);
	if (status != 0)
		return status;
	uint64_t duration = (uint64_t)(uint16_t)(seconds > 30 ? 30 : seconds) * 1000;
	if (duration > UINT64_MAX - now)
		return IO_ERROR_OUT_OF_RANGE;
	igs->wait_deadline = now + duration;
	igs->timed_pause = true;
	igs->pause_key = false;
	status = igs->loop_mode == 1 ? 0 : cterm_igs_flow(igs, false);
	return status == 0 ? IO_ERROR_IN_PROGRESS : status;
}

static int
store_text(struct cterm_igs *igs, const struct st_graphics_argument *text)
{
	if (text->kind != ST_GRAPHICS_TEXT || text->size > 128)
		return IO_ERROR_PROTOCOL;
	memcpy(igs->shared_text, text->bytes, text->size);
	igs->shared_text[text->size] = 0;
	igs->shared_text_size = text->size;
	return 0;
}

static int
store_pattern_row(struct cterm_igs *igs, int slot, size_t row,
    const struct st_graphics_argument *text)
{
	if (slot < 0 || slot > 7 || row >= 16)
		return IO_ERROR_OUT_OF_RANGE;
	if (text->kind != ST_GRAPHICS_BYTES || text->size != 0) {
		int status = store_text(igs, text);
		if (status != 0)
			return status;
	}
	uint16_t bits = 0;
	/* The shared reader retains the suffix after its new NUL. X7 scans
	 * all sixteen bytes, including that retained suffix, and edits case. */
	for (unsigned col = 0; col < 16; ++col) {
		if (igs->shared_text[col] == 'x')
			igs->shared_text[col] = 'X';
		if (igs->shared_text[col] == 'X')
			bits |= (uint16_t)(0x8000u >> col);
	}
	igs->patterns[slot][row] = bits;
	igs->text_deadline = 0;
	return 0;
}

static int
field_entry(void *context, const struct st_graphics_command_view *view,
    const struct st_graphics_parameter *parameter)
{
	struct cterm_igs *igs = context;
	if (view->code == 'N') {
		if (view->count == 0) return 0;
		struct cterm_igs_music *m = igs->music;
		if (!m->command) {
			int operation;
			int status = cterm_igs_argument_value(igs, &view->arguments[0], &operation);
			if (status != 0) return status;
			cterm_igs_music_prefix(igs, operation);
		}
		if (view->count == 2 && parameter->kind == ST_GRAPHICS_BYTES) {
			int count;
			int status = cterm_igs_argument_value(igs, &view->arguments[1], &count);
			return status == 0 ? cterm_igs_music_load(igs, count) : status;
		}
		return 0;
	}
	bool pattern = false;
	if (view->code == 'X' && view->count >= 2) {
		struct cterm_igs preview = *igs;
		int selector, slot;
		int status = 0;
		if (igs->bitmap_loading)
			selector = 11;
		else if (igs->math_printing)
			selector = 13;
		else if (igs->pattern_loading)
			selector = 7;
		else
			status = cterm_igs_argument_value(&preview, &view->arguments[0], &selector);
		if (status != 0) return status;
		if (selector == 11 && view->count == 3 && parameter->kind == ST_GRAPHICS_BYTES) {
			int operation, section;
			status = cterm_igs_argument_value(&preview,&view->arguments[1],&operation);
			if (status == 0) status = cterm_igs_argument_value(&preview,&view->arguments[2],&section);
			if (status != 0) return status;
			igs->random_seed = preview.random_seed;
			return cterm_igs_bitmap_begin(igs,operation,section);
		}
		if (selector == 13 && view->count == 4 && !igs->math_printing) {
			int operation, kind, index;
			status = cterm_igs_argument_value(&preview, &view->arguments[1], &operation);
			if (status != 0) return status;
			if (operation == 7) {
				status = cterm_igs_argument_value(&preview, &view->arguments[2], &kind);
				if (status != 0) return status;
				if (kind == 1) {
					status = cterm_igs_argument_value(&preview, &view->arguments[3], &index);
					if (status != 0) return status;
					/* sprintf precedes the first coordinate reader. Keep its
					 * buffer and prefix effects even if that reader stops. */
					status = cterm_igs_math_format(igs, index);
					if (status != 0) return status;
					igs->random_seed = preview.random_seed;
					igs->math_printing = true;
				}
			}
		}
		pattern = selector == 7;
		if (pattern) {
			if (!igs->pattern_loading) {
				status = cterm_igs_argument_value(&preview, &view->arguments[1], &slot);
				if (status != 0) return status;
				igs->pattern_slot = slot;
				igs->pattern_loading = true;
				igs->random_seed = preview.random_seed;
			}
			slot = igs->pattern_slot;
			/* Entry to the next row follows acceptance of the prior row,
			 * even with no more input bytes. Publish it before its reader. */
			if (view->count > 2) {
				status = store_pattern_row(igs, slot, view->count - 3, &view->arguments[view->count - 1]);
				if (status != 0) return status;
			}
			if (igs->loop_mode == 0 && igs->loop_kind == 'Z')
				igs->loop_kind = ' ';
		}
	}
	if ((view->code != 'W' && !pattern) || parameter->kind != ST_GRAPHICS_TEXT) return 0;
	uint64_t now;
	int status = igs->now_ms(&now);
	if (status != 0) return status;
	igs->text_deadline = now > UINT64_MAX-5000 ? UINT64_MAX : now+5000;
	if (igs->loop_mode == 1 && igs->loop_kind == '@') {
		if (igs->text_counter < SIZE_MAX) ++igs->text_counter;
		if (igs->text_counter == 1) return cterm_igs_flow(igs,true);
	}
	return 0;
}

static int
repeat_console_escape(struct cterm_igs *igs, uint8_t letter, int count)
{
	const uint8_t output[] = {27, letter};
	const size_t size = letter == 0 ? 1 : sizeof(output);
	/* The first iteration precedes the signed count comparison. Each
	 * escape starts in the state left by the previous output bytes. */
	for (int i = 0; i < (count > 0 ? count : 1); ++i) {
		int status = cterm_vt52_output(igs->terminal, output, size);
		if (status != 0)
			return status;
	}
	return 0;
}

static int
clear_screen(struct cterm_igs *igs, int selector)
{
	struct cterminal *terminal = igs->terminal;
	if (selector > 5)
		selector = 0;
	if (selector == 5) {
		/* The first ESC is sent before the reset string's own ESC.
		 * In Normal this makes q a glyph, which can wrap and scroll. */
		static const uint8_t reset[] = {27, 27, 'q', 27, 'c', '0', 27, 'b', '3', 27, 'E'};
		static const uint8_t low_foreground[] = {27, 'b', '?'};
		int status = cterm_vt52_output(terminal, reset, sizeof(reset));
		if (status == 0 && igs->resolution == 0)
			status = cterm_vt52_output(terminal, low_foreground, sizeof(low_foreground));
		return status;
	}
	if (selector != 3) {
		static const uint8_t letters[] = {'E', 'd', 'J', 0, 'E'};
		const uint8_t output[] = {27, selector < 0 ? 0 : letters[selector]};
		int status = cterm_vt52_output(terminal, output, selector < 0 ? 1 : sizeof(output));
		if (status != 0)
			return status;
	}
	if (selector == 3 || selector == 4) {
		/* The VDI clear covers the drawable screen independently of clip,
		 * console colors and cursor state. Keep the reserved UI row. */
		return cterm_render_fill_blank(terminal, terminal->x, terminal->y,
		    terminal->x + terminal->width - 1, terminal->y + terminal->height - 1,
		    CTERM_IGS_PALETTE_FIRST);
	}
	return 0;
}

static int
command(void *context, const struct st_graphics_command_view *view)
{
	struct cterm_igs *igs = context;
	int values[1+2*CTERM_IGS_MAX_POINTS];
	if (igs->reset_pending)
		return IO_ERROR_INTERRUPTED;
	if (view->code == 'N') {
		struct cterm_igs_music *m = igs->music;
		if (!m->command) {
			int operation;
			int status = cterm_igs_argument_value(igs, &view->arguments[0], &operation);
			if (status != 0) return status;
			cterm_igs_music_prefix(igs, operation);
		}
		int first = 0, end = 0, status = 0;
		if (m->operation == 6 && m->local_count > 5) {
			if (view->count != 3) return IO_ERROR_PROTOCOL;
			status = cterm_igs_argument_value(igs, &view->arguments[1], &first);
			if (status == 0) status = cterm_igs_argument_value(igs, &view->arguments[2], &end);
		}
		return status == 0 ? cterm_igs_music_execute(igs, first, end) : status;
	}
	if (view->code == 'X' && igs->bitmap_loading) {
		if (view->count != 4 || view->arguments[3].kind != ST_GRAPHICS_BYTES
		    || view->arguments[3].size != igs->bitmap_received) return IO_ERROR_PROTOCOL;
		return cterm_igs_bitmap_complete(igs,igs->bitmap_operation,igs->bitmap_section);
	}
	if (view->code == 'X' && igs->math_printing) {
		if (view->count != 6) return IO_ERROR_PROTOCOL;
		int x, y;
		int status = cterm_igs_argument_value(igs, &view->arguments[4], &x);
		if (status == 0) status = cterm_igs_argument_value(igs, &view->arguments[5], &y);
		igs->math_printing = false;
		return status == 0 ? cterm_igs_graphic_text(igs, x, y,
		    igs->shared_text, igs->shared_text_size) : status;
	}
	if (view->code == 'X' && igs->pattern_loading) {
		if (view->count != 18)
			return IO_ERROR_PROTOCOL;
		int status = store_pattern_row(igs, igs->pattern_slot, 15, &view->arguments[17]);
		igs->pattern_loading = false;
		igs->pattern_slot = 0;
		return status;
	}
	if (view->code == 'X') {
		struct cterm_igs preview = *igs;
		int selector;
		int status = cterm_igs_argument_value(&preview, &view->arguments[0], &selector);
		if (status != 0) return status;
		if (selector == 2) {
			struct st_graphics_command_view tail = {.code = 2, .chained = view->chained,
			    .arguments = view->arguments + 1, .count = view->count - 1};
			bool extra;
			status = random_range_arguments(&preview, &tail, true, &extra);
			if (status == 0) {
				memcpy(igs->random_range, preview.random_range, sizeof(igs->random_range));
				igs->random_seed = preview.random_seed;
			}
			return status;
		}
	}
	if (view->count > sizeof(values) / sizeof(*values))
		return IO_ERROR_PROTOCOL;
	for (size_t i = 0; i < view->count; ++i) {
		if (view->arguments[i].kind != ST_GRAPHICS_NUMBER) { values[i] = 0; continue; }
		int status = cterm_igs_argument_value(igs, &view->arguments[i], &values[i]);
		if (status != 0)
			return status;
	}
	switch (view->code) {
	case 'b': return cterm_igs_audio_effect(igs,values,view->count);
	case 'n': return cterm_igs_audio_note_begin(igs,values);
	case 'G': return cterm_igs_grab(igs,values,view->count);
	case 'I': return cterm_igs_initialize(igs, values[0]);
	case 'R': return cterm_igs_resolution(igs, values[0], values[1]);
	case 'g': cterm_igs_set_scaling(igs, values[0]); return 0;
	case 'M':
		igs->attributes.writing_mode = values[0] >= 1 && values[0] <= 4 ? values[0] : 1;
		return 0;
	case 'C': cterm_igs_set_pen(igs, values[0], values[1]); return 0;
	case 'p': {
		/* Acquire column/row unscaled, then send BIOS low bytes through the
		 * existing console state. Addition and byte wrap precede VT52 clamps. */
		const uint8_t output[] = {27,'Y',(uint8_t)((unsigned)values[1]+32u),(uint8_t)((unsigned)values[0]+32u)};
		return cterm_vt52_output(igs->terminal,output,sizeof(output));
	}
	case 'm': {
		static const uint8_t directions[] = {'H','A','B','C','D'};
		int selector = values[0] > 4 ? 0 : values[0];
		return repeat_console_escape(igs, selector < 0 ? 0 : directions[selector], values[1]);
	}
	case 'd': return repeat_console_escape(igs, 'M', values[0]);
	case 't': return begin_pause(igs, values[0]);
	case 'q': {
		int count = values[0];
		if (count > 9994) {
			igs->double_step = 9999 - count;
			count = 1;
		}
		if (count < 1)
			count = 1;
		if (count > 180)
			count = 180;
		/* The original loop calls Vsync before its old-value decrement
		 * test, so its terminating iteration contributes one more unit. */
		return cterm_igs_wait_sync(igs, (unsigned)count + 1);
	}
	case 'i': {
		static const uint8_t letters[] = {'I', 'L'};
		int selector = values[0] > 1 ? 0 : values[0];
		return repeat_console_escape(igs, selector < 0 ? 0 : letters[selector], values[1]);
	}
	case 'r': {
		const uint8_t output[] = {27, values[0] == 1 ? 'k' : 'j'};
		return cterm_vt52_output(igs->terminal, output, values[0] < 0 ? 1 : sizeof(output));
	}
	case 'k': {
		int selector = values[0] > 3 ? 0 : values[0];
		if (selector >= 2) {
			igs->destructive_backspace = selector == 2;
			return 0;
		}
		const uint8_t output[] = {27, selector == 1 ? 'e' : 'f'};
		return cterm_vt52_output(igs->terminal, output, selector < 0 ? 1 : sizeof(output));
	}
	case 'v': {
		const uint8_t output[] = {27, values[0] == 1 ? 'p' : 'q'};
		return cterm_vt52_output(igs->terminal, output, values[0] < 0 ? 1 : sizeof(output));
	}
	case 'w': {
		const uint8_t output[] = {27, values[0] == 1 ? 'v' : 'w'};
		return cterm_vt52_output(igs->terminal, output, values[0] < 0 ? 1 : sizeof(output));
	}
	case 'l': {
		static const uint8_t letters[] = {'l', 'o', 'K'};
		int selector = values[0] > 2 ? 0 : values[0];
		const uint8_t output[] = {27, selector < 0 ? 0 : letters[selector]};
		return cterm_vt52_output(igs->terminal, output, selector < 0 ? 1 : sizeof(output));
	}
	case 's': return clear_screen(igs, values[0]);
	case 'c': {
		/* Upper selectors select background; negative selectors emit ESC only.
		 * Colors are raw console bytes, independent of VDI pen numbering. */
		const uint8_t output[] = {27, values[0] == 1 ? 'b' : 'c', (uint8_t)values[1]};
		return cterm_vt52_output(igs->terminal,output,values[0] < 0 ? 1 : sizeof(output));
	}
	case 'A': cterm_igs_set_fill(igs, values[0], values[1], values[2]); return 0;
	case 'E': cterm_igs_set_text(igs, values[0], values[1], values[2]); return 0;
	case 'W': {
		igs->text_deadline = 0;
		if (view->count == 3) {
			int status = store_text(igs, &view->arguments[2]);
			if (status != 0) return status;
		} else if (view->count != 2) return IO_ERROR_PROTOCOL;
		if (igs->loop_mode == 0 && igs->loop_kind == 'Z') igs->loop_kind = ' ';
		const uint8_t *nul = memchr(igs->shared_text,0,igs->shared_text_size);
		size_t visible = nul == NULL ? igs->shared_text_size : (size_t)(nul-igs->shared_text);
		return cterm_igs_graphic_text(igs,values[0],values[1],igs->shared_text,visible);
	}

	case 'P': return cterm_igs_marker(igs, values[0], values[1]);
	case 'L': return cterm_igs_line(igs, values[0], values[1], values[2], values[3]);
	case 'Z': return cterm_igs_rectangle(igs, values[0], values[1], values[2], values[3]);
	case 'O': return cterm_igs_circle(igs,values[0],values[1],values[2]);
	case 'Q': return cterm_igs_ellipse(igs,values[0],values[1],values[2],values[3]);
	case 'K': return cterm_igs_arc(igs,values[0],values[1],values[2],values[3],values[4]);
	case 'J': return cterm_igs_ellarc(igs,values[0],values[1],values[2],values[3],values[4],values[5]);
	case 'V': return cterm_igs_pie(igs,values[0],values[1],values[2],values[3],values[4]);
	case 'Y': return cterm_igs_ellpie(igs,values[0],values[1],values[2],values[3],values[4],values[5]);
	case 'B': return cterm_igs_box(igs, values[0], values[1], values[2], values[3], values[4] != 0);
	case 'U':
		if (values[4] < 0) return 0;
		if (values[4] > 0) return cterm_igs_box(igs,values[0],values[1],values[2],values[3],true);
		return cterm_igs_rounded_outline(igs,values[0],values[1],values[2],values[3]);
	case 'F':
	case 'f':
	case 'z': {
		int count = view->code == 'F' ? 0 : point_count(values[0], view->code == 'f' ? 1 : 2);
		if (view->count != (view->code == 'F' ? 2 : 1+(size_t)count*2))
			return IO_ERROR_PROTOCOL;
		int status = igs->loop_mode == 1 ? 0 : cterm_igs_flow(igs, false);
		if (status == 0)
			status = view->code == 'F' ? cterm_igs_flood(igs,values[0],values[1])
			    : view->code == 'f' ? cterm_igs_polygon(igs, values+1, (size_t)count)
			    : cterm_igs_polyline(igs, values+1, (size_t)count);
		if (status == 0 && igs->loop_mode != 1)
			status = cterm_igs_flow(igs, true);
		return status;
	}
	case 'D': {
		int status = cterm_igs_line(igs, igs->draw_x, igs->draw_y, values[0], values[1]);
		if (status == 0) {
			/* Native D copies the original endpoint to both remembered
			 * pairs after drawing; arrow shortening stays raster-private. */
			igs->draw_start_x = igs->draw_x;
			igs->draw_start_y = igs->draw_y;
		}
		return status;
	}
	case 'X':
		if (values[0] == 15)
			return cterm_igs_sweep_begin(igs,values+1);
		if (values[0] == 8)
			return cterm_igs_palette_rotation_begin(igs,values[1],values[2],values[3],values[4]);
		if (values[0] == 11) {
			if (values[1] == 0 && view->count == 4)
				return cterm_igs_bitmap_wipe(igs,values[2],values[3]);
			if (view->count != 3 || values[1] == 1 || values[1] == 2) return IO_ERROR_PROTOCOL;
			return cterm_igs_bitmap_complete(igs,values[1],values[2]);
		}
		if (values[0] == 14) {
			if (view->count != 2) return IO_ERROR_PROTOCOL;
			igs->memory_route = values[1] > 5 ? 0 : values[1];
			return 0;
		}
		if (values[0] == 13) {
			int operation = values[1];
			if (operation < 0 || operation > 7) return 0;
			if (operation == 0 && view->count == 4)
				return cterm_igs_math_write(igs, 0, values[2], 0, values[3]);
			if (operation >= 1 && operation <= 5 && view->count == 5)
				return cterm_igs_math_write(igs, operation, values[2], values[3], values[4]);
			if (operation == 6 && view->count == 4)
				return cterm_igs_math_clear(igs, values[2], values[3]);
			if (operation == 7 && view->count == 4)
				return values[2] == 0 ? cterm_igs_math_console(igs, values[3]) : 0;
			return IO_ERROR_PROTOCOL;
		}
		if (values[0] == 0) {
			if (view->count != 6)
				return IO_ERROR_PROTOCOL;
			return cterm_igs_scatter(igs, values[1], values[2], values[3], values[4], values[5]);
		}
		if (values[0] == 12) {
			if (view->count < 2)
				return IO_ERROR_PROTOCOL;
			int base = palette_group_base(values[1]);
			size_t count = palette_group_count(base);
			if (view->count != count + 2)
				return IO_ERROR_PROTOCOL;
			for (size_t i = 0; i < count; ++i) {
				int status = cterm_igs_palette_register(igs, base + (int)i, values[i + 2]);
				if (status != 0)
					return status;
			}
			return 0;
		}
		if (values[0] == 1) {
			if (view->count != 3)
				return IO_ERROR_PROTOCOL;
			return cterm_igs_palette_register(igs, values[1] > 15 ? 15 : values[1], values[2]);
		}
		if (values[0] == 10) {
			if (view->count != 3)
				return IO_ERROR_PROTOCOL;
			/* IG221.TXT's L/P-equivalent positioning uses X/Y scaling;
			 * correct the binary's off-by-one 0/1 reader classes. */
			igs->draw_x = values[1]; igs->draw_y = values[2];
			return 0;
		}
		if (values[0] >= 0 && values[0] <= 15) {
			cterm_log(igs->terminal, ST_CTERM_LOG_WARNING,
			    "Unsupported IGS X selector %d", values[0]);
			return IO_ERROR_NOT_SUPPORTED;
		}
		/* Out-of-range selectors have native one-argument no-op arity. */
		return 0;
	case 'T':
		if (values[0] < 1 || values[0] > 2)
			return 0;
		if (view->count != 3)
			return IO_ERROR_PROTOCOL;
		cterm_igs_set_type(igs, values[0], values[1], values[2]);
		return 0;
	case 'H': {
		if (values[0] < 0)
			return 0;
		bool hollow = values[0] != 0;
		igs->attributes.fill_interior = hollow ? 0 : 1;
		igs->attributes.writing_mode = hollow ? 2 : 1;
		igs->attributes.fill_perimeter = hollow;
		return 0;
	}
	case 'S': {
		static const int components[] = {0,142,285,428,571,714,857,1000};
		for (unsigned i = 1; i < 4; ++i)
			values[i] = values[i] >= 1 && values[i] <= 7 ? components[values[i]] : 0;
		return cterm_igs_palette_pen(igs, values[0] > 15 ? 0 : values[0], values[1], values[2], values[3]);
	}
	default: return IO_ERROR_NOT_SUPPORTED;
	}
}

static void
unknown(void *context, uint8_t code)
{
	struct cterm_igs *igs = context;
	unsigned mask = 1u << (code % 8);
	if ((igs->warned[code / 8] & mask) == 0) {
		igs->warned[code / 8] |= (uint8_t)mask;
		cterm_log(igs->terminal, ST_CTERM_LOG_WARNING,
		    "Unsupported IGS command 0x%02x", (unsigned)code);
	}
}

static int
raw(void *context, const struct st_graphics_command_view *view,
    const struct st_graphics_parameter *parameter, size_t offset,
    const uint8_t *bytes, size_t size, size_t *accepted, bool *end)
{
	struct cterm_igs *igs = context;
	if (view->code == 'N') {
		if (parameter->kind != ST_GRAPHICS_BYTES || offset != igs->music->received) {
			*accepted = 0; return IO_ERROR_PROTOCOL;
		}
		return cterm_igs_music_bytes(igs, bytes, size, accepted, end);
	}
	if (!igs->bitmap_loading) return 0;
	if (view->code != 'X' || parameter->kind != ST_GRAPHICS_BYTES
	    || offset != igs->bitmap_received) { *accepted = 0; return IO_ERROR_PROTOCOL; }
	return cterm_igs_bitmap_bytes(igs,bytes,size,accepted,end);
}

static struct st_graphics_parser_sink
sink(struct cterm_igs *igs)
{
	return (struct st_graphics_parser_sink){.context = igs, .text = text, .boundary = boundary,
	    .command = command, .unknown = unknown, .field = field_entry, .raw = raw};
}

int
cterm_igs_create(struct cterminal *terminal)
{
	uint32_t width, height, cw, ch;
	unsigned resolution;
	if ((terminal->screen.capabilities & ST_CONSOLE_CAP_PIXELS) == 0)
		return IO_ERROR_NOT_SUPPORTED;
	int status = st_console_pixel_size(terminal->screen.console, &width, &height);
	if (status == 0)
		status = st_console_cell_pixel_size(terminal->screen.console, &cw, &ch);
	if (status != 0)
		return status;
	if (width == 320 && height == 200) resolution = 0;
	else if (width == 640 && height == 200) resolution = 1;
	else if (width == 640 && height == 400) resolution = 2;
	else return IO_ERROR_INVALID_ARGUMENT;
	if (cw != 8 || ch != (resolution == 2 ? 16u : 8u)
	    || (uint32_t)terminal->width != width / cw)
		return IO_ERROR_INVALID_ARGUMENT;
	struct cterm_igs *igs = calloc(1, sizeof(*igs));
	if (igs == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	igs->terminal = terminal;
	igs->resolution = resolution;
	igs->width = width;
	igs->height = height;
	igs->now_ms = st_time_monotonic_ms;
	igs->sync_wait_hz = 60;
	igs->pending_resolution = -1;
	const struct st_graphics_parser_config config = {
		.dialect = &dialect, .max_arguments = 4096, .max_payload = 65536,
		.commands = commands, .command_count = sizeof(commands) / sizeof(*commands),
	};
	status = st_graphics_parser_create(&igs->parser, &config);
	if (status != 0) {
		free(igs);
		return status;
	}
	status = cterm_igs_state_init(igs);
	if (status != 0) {
		cterm_igs_destroy(igs);
		return status;
	}
	/* Establish the base console before any graphics operation. Otherwise
	 * the first later ordinary byte would start CTerm and clear that scene. */
	terminal->igs_engine = igs;
	cterm_vt52_reset_console(terminal);
	cterm_start(terminal);
	status = cterm_render_take_error(terminal);
	if (status != 0) {
		terminal->igs_engine = NULL;
		cterm_igs_destroy(igs);
		return status;
	}
	return 0;
}

void
cterm_igs_destroy(struct cterm_igs *igs)
{
	if (igs == NULL)
		return;
	cterm_igs_music_stop(igs);
	free(igs->music);
	cterm_igs_audio_destroy(igs);
	st_graphics_parser_destroy(igs->parser);
	st_igs_image_destroy(igs->memory[0]);
	st_igs_image_destroy(igs->memory[1]);
	free(igs);
}

void
cterm_igs_reset(struct cterm_igs *igs)
{
	if (igs == NULL)
		return;
	if (igs->feeding) {
		igs->reset_pending = true;
		return;
	}
	if (igs->timed_pause) {
		igs->feeding = true;
		int status = end_pause(igs);
		igs->feeding = false;
		if (status != IO_ERROR_INTERRUPTED)
			cterm_render_record_error(igs->terminal, status);
	}
	cancel_input(igs);
	cterm_igs_music_stop(igs);
	cterm_igs_audio_destroy(igs);
	memset(igs->warned, 0, sizeof(igs->warned));
	cterm_render_record_error(igs->terminal, cterm_igs_state_reset(igs));
}

bool
cterm_igs_paused(const struct cterm_igs *igs)
{
	return igs != NULL && (igs->wait_deadline != 0 || igs->timed_pause || igs->note.active
	    || (igs->music != NULL && igs->music->playing));
}

bool
cterm_igs_handle_key(struct cterm_igs *igs, const struct cterm_igs_key *key)
{
	if (igs != NULL && igs->music != NULL && igs->music->replaying) return false;
	if (igs != NULL && !igs->terminal->quiet)
		cterm_render_record_error(igs->terminal,cterm_igs_audio_console(igs,true));
	if (igs != NULL && igs->note.active && !igs->note.key) {
		igs->note.key = true;
		return true;
	}
	if (igs != NULL && cterm_igs_music_key(igs, key)) return true;
	if (igs != NULL && igs->palette_rotation.active && !igs->palette_rotation.key) {
		igs->palette_rotation.key = true;
		return true;
	}
	if (igs == NULL || !igs->timed_pause || igs->pause_key)
		return false;
	igs->pause_key = true;
	return true;
}

int
cterm_igs_feed(struct cterm_igs *igs, const void *bytes, size_t size, size_t *used)
{
	const uint8_t *input = bytes;
	int status = 0;
	*used = 0;
	if (igs->feeding)
		return IO_ERROR_BUSY;
	status = cterm_igs_audio_poll(igs);
	if (status != 0) return status;
	if (cterm_igs_paused(igs))
		return 0;
	if (igs->text_deadline != 0) {
		uint64_t now;
		status = igs->now_ms(&now);
		if (status != 0) return status;
		if (now >= igs->text_deadline) {
			cancel_input(igs); cterm_render_beep(igs->terminal);
			cterm_log(igs->terminal, ST_CTERM_LOG_WARNING, "IGS text input timed out");
		}
	}
	do {
		size_t consumed = 0;
		struct st_graphics_parser_sink callbacks = sink(igs);
		igs->feeding = true;
		status = st_graphics_parser_feed(igs->parser, &callbacks,
		    input == NULL ? NULL : input + *used, size - *used, &consumed);
		igs->feeding = false;
		*used += consumed;
		if (!igs->reset_pending)
			break;
		cterm_igs_reset(igs);
		if (status == IO_ERROR_INTERRUPTED)
			status = 0;
	} while (status == 0 && *used < size);
	if (status == IO_ERROR_IN_PROGRESS)
		status = 0;
	if (status != 0) {
		cterm_render_record_error(igs->terminal, cterm_igs_music_cancel(igs));
		igs->pattern_loading = false;
		igs->pattern_slot = 0;
		igs->math_printing = false;
		cterm_igs_bitmap_cancel(igs);
	}
	if (status == 0 && igs->music->loading)
		igs->input_deadline = igs->music->receive_deadline;
	else if (status == 0 && igs->bitmap_loading)
		igs->input_deadline = igs->bitmap_deadline;
	else if (status == 0 && !cterm_igs_paused(igs) && st_graphics_parser_in_command(igs->parser)) {
		uint64_t now;
		status = igs->now_ms(&now);
		if (status == 0 && (*used != 0 || igs->input_deadline == 0))
			igs->input_deadline = now > UINT64_MAX - 5000 ? UINT64_MAX : now + 5000;
	}
	else
		igs->input_deadline = igs->text_deadline = 0;
	/* Text entry already established this absolute deadline, even if the
	 * later progress-clock read failed. Keep polling it without more input. */
	if (igs->text_deadline != 0)
		igs->input_deadline = igs->text_deadline;
	return status;
}

int
cterm_igs_finish(struct cterm_igs *igs)
{
	if (igs->feeding)
		return 0;
	cterm_igs_music_stop(igs);
	cterm_igs_audio_stop(igs);
	if (cterm_igs_paused(igs)) {
		int status = 0;
		if (igs->timed_pause) {
			igs->feeding = true;
			status = end_pause(igs);
			igs->feeding = false;
		}
		bool reset = igs->reset_pending;
		cancel_input(igs);
		if (reset)
			cterm_igs_reset(igs);
		return status == IO_ERROR_INTERRUPTED ? 0 : status;
	}
	struct st_graphics_parser_sink callbacks = sink(igs);
	igs->feeding = true;
	int status = igs->bitmap_loading ? st_graphics_parser_end_bytes(igs->parser,&callbacks) : 0;
	if (status == 0) status = st_graphics_parser_finish(igs->parser, &callbacks);
	igs->feeding = false;
	if (status == IO_ERROR_PROTOCOL) {
		cterm_log(igs->terminal, ST_CTERM_LOG_WARNING, "Discarded incomplete IGS command");
		status = 0;
	}
	if (igs->reset_pending) {
		cterm_igs_reset(igs);
		if (status == IO_ERROR_INTERRUPTED)
			status = 0;
	}
	igs->input_deadline = igs->text_deadline = 0;
	igs->pattern_loading = false;
	igs->pattern_slot = 0;
	igs->math_printing = false;
	cterm_igs_bitmap_cancel(igs);
	int cancelled = cterm_igs_music_cancel(igs);
	return status == 0 ? cancelled : status;
}

int
cterm_igs_poll(struct cterm_igs *igs)
{
	int audio_status = cterm_igs_audio_poll(igs);
	if (audio_status != 0) return audio_status;
	uint64_t now;
	if (igs->note.active) {
		int status = igs->now_ms(&now);
		if (status != 0 || (!igs->note.key && now < igs->wait_deadline)) return status;
		status = cterm_igs_audio_note_end(igs);
		if (status != 0) return status;
		if (!igs->music->playing) return st_graphics_parser_resume(igs->parser);
	}
	if (igs->music->playing) {
		igs->feeding = true;
		int status = cterm_igs_music_resume(igs);
		igs->feeding = false;
		if (igs->reset_pending) { cterm_igs_reset(igs); return status == IO_ERROR_INTERRUPTED ? 0 : status; }
		if (status == IO_ERROR_IN_PROGRESS) return 0;
		if (status != 0) { cancel_input(igs); return status; }
		return st_graphics_parser_resume(igs->parser);
	}
	if (igs->sweep.active) {
		int status = igs->now_ms(&now);
		if (status != 0 || now < igs->wait_deadline) return status;
		igs->feeding = true;
		status = cterm_igs_sweep_resume(igs);
		igs->feeding = false;
		if (igs->reset_pending) { cterm_igs_reset(igs); return status == IO_ERROR_INTERRUPTED ? 0 : status; }
		if (status == IO_ERROR_IN_PROGRESS) return 0;
		if (status != 0) { cancel_input(igs); return status; }
		igs->wait_deadline = 0;
		return st_graphics_parser_resume(igs->parser);
	}
	if (igs->palette_rotation.active) {
		int status = igs->now_ms(&now);
		if (status != 0) return status;
		if (now < igs->wait_deadline && !(igs->palette_rotation.delaying && igs->palette_rotation.key)) return 0;
		igs->feeding = true;
		status = cterm_igs_palette_rotation_resume(igs);
		igs->feeding = false;
		if (igs->reset_pending) { cterm_igs_reset(igs); return status == IO_ERROR_INTERRUPTED ? 0 : status; }
		if (status == IO_ERROR_IN_PROGRESS) return 0;
		if (status != 0) { cancel_input(igs); return status; }
		igs->wait_deadline = 0;
		return st_graphics_parser_resume(igs->parser);
	}
	if (igs->timed_pause) {
		if (!igs->pause_key) {
			int status = igs->now_ms(&now);
			if (status != 0 || now < igs->wait_deadline)
				return status;
		}
		igs->feeding = true;
		int status = end_pause(igs);
		igs->feeding = false;
		if (igs->reset_pending) {
			cterm_igs_reset(igs);
			return status == IO_ERROR_INTERRUPTED ? 0 : status;
		}
		if (status != 0) {
			cancel_input(igs);
			return status;
		}
		return st_graphics_parser_resume(igs->parser);
	}
	if (cterm_igs_paused(igs)) {
		int status = igs->now_ms(&now);
		if (status != 0 || now < igs->wait_deadline)
			return status;
		if (igs->pending_resolution < 0) {
			if (igs->copy_pending) {
				igs->feeding = true;
				status = cterm_igs_grab_resume(igs);
				igs->feeding = false;
				if (igs->reset_pending) { cterm_igs_reset(igs); return status; }
				if (status != 0) { cancel_input(igs); return status; }
			}
			igs->wait_deadline = 0;
			return st_graphics_parser_resume(igs->parser);
		}
		igs->feeding = true;
		status = cterm_igs_set_resolution(igs->terminal, (unsigned)igs->pending_resolution);
		igs->feeding = false;
		if (igs->reset_pending) {
			cterm_igs_reset(igs);
			return status;
		}
		igs->pending_resolution = -1;
		igs->wait_deadline = 0;
		int palette = igs->pending_palette;
		igs->pending_palette = 0;
		if (status == 0)
			status = cterm_igs_resolution_complete(igs, palette);
		if (status != 0) {
			cancel_input(igs);
			return status;
		}
		return st_graphics_parser_resume(igs->parser);
	}
	if (igs->music->loading) {
		int status = igs->now_ms(&now);
		if (status != 0 || now < igs->music->receive_deadline) return status;
		const uint8_t space = ' '; size_t used;
		struct st_graphics_parser_sink callbacks = sink(igs);
		igs->feeding = true;
		status = st_graphics_parser_feed(igs->parser, &callbacks, &space, 1, &used);
		igs->feeding = false;
		if (igs->reset_pending) { cterm_igs_reset(igs); return status == IO_ERROR_INTERRUPTED ? 0 : status; }
		if (status == IO_ERROR_IN_PROGRESS) status = 0;
		if (status != 0) { cancel_input(igs); return status; }
		igs->input_deadline = igs->music->loading ? igs->music->receive_deadline : 0;
		return 0;
	}
	if (igs->bitmap_loading) {
		int status = igs->now_ms(&now);
		if (status != 0 || now < igs->bitmap_deadline) return status;
		/* Supply one expired receive slot. The raw consumer owns marker,
		 * console BEL and accumulated-error policy, outside this grammar. */
		const uint8_t marker = '*'; size_t used;
		struct st_graphics_parser_sink callbacks = sink(igs);
		igs->feeding = true;
		status = st_graphics_parser_feed(igs->parser,&callbacks,&marker,1,&used);
		igs->feeding = false;
		if (igs->reset_pending) { cterm_igs_reset(igs); return status == IO_ERROR_INTERRUPTED ? 0 : status; }
		if (status != 0) { cancel_input(igs); return status; }
		igs->input_deadline = igs->bitmap_loading ? igs->bitmap_deadline
		    : st_graphics_parser_in_command(igs->parser)
		    ? (now > UINT64_MAX-5000 ? UINT64_MAX : now+5000) : 0;
		return 0;
	}
	if (igs->input_deadline == 0)
		return 0;
	int status = igs->now_ms(&now);
	if (status == 0 && now >= igs->input_deadline) {
		bool text_timeout = igs->text_deadline != 0;
		cancel_input(igs);
		if (text_timeout) cterm_render_beep(igs->terminal);
		cterm_log(igs->terminal, ST_CTERM_LOG_WARNING, "IGS input timed out");
	}
	return status;
}

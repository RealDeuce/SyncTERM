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
static int zone_next(void *, const struct st_graphics_command_view *, struct st_graphics_parameter *);
static int macro_next(void *, const struct st_graphics_command_view *, struct st_graphics_parameter *);
static int flow_mode_next(void *, const struct st_graphics_command_view *, struct st_graphics_parameter *);
static int script_next(void *, const struct st_graphics_command_view *, struct st_graphics_parameter *);
static int unscaled_tail_next(void *, const struct st_graphics_command_view *, struct st_graphics_parameter *);
static const struct st_graphics_command extended_commands[] = {
	{0, point_parameters, 4, unscaled_tail_next},
	{1, unscaled_parameters, 2, NULL},
	{2, unscaled_parameters, 1, random_range_next},
	{3, unscaled_parameters, 1, macro_next},
	{4, unscaled_parameters, 1, zone_next},
	{5, unscaled_parameters, 1, flow_mode_next},
	{6, unscaled_parameters, 1, NULL},
	{7, unscaled_parameters, 1, pattern_load_next},
	{8, unscaled_parameters, 4, NULL},
	{9, unscaled_parameters, 1, script_next},
	{10, point_parameters, 2, NULL},
	{11, unscaled_parameters, 2, bitmap_next},
	{12, unscaled_parameters, 1, palette_group_next},
	{13, unscaled_parameters, 1, math_next},
	{14, unscaled_parameters, 1, NULL},
	{15, unscaled_parameters, 9, NULL},
};
_Static_assert(sizeof(extended_commands)/sizeof(*extended_commands)==16,
    "IGS X dispatch requires all sixteen selectors");

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

static int
script_next(void *context, const struct st_graphics_command_view *view,
    struct st_graphics_parameter *result)
{
	struct cterm_igs *igs=context;
	int operation=igs->script.operation;
	int status=igs->script.operand_ready?0:preview_first(context,view,&operation);
	*result=operation==0 && !igs->script.active && view->count==1
	    ? (struct st_graphics_parameter){.kind=ST_GRAPHICS_BYTES,.size=10001}
	    : (struct st_graphics_parameter){.kind=ST_GRAPHICS_END};
	return status;
}

static int
query_next(void *context, const struct st_graphics_command_view *view,
    struct st_graphics_parameter *result)
{
	struct cterm_igs *igs=context;
	int selector=igs->query.selector;
	int status=igs->query.configuring?0:preview_first(context,view,&selector);
	*result=(selector==1 || selector==2) && view->count==1 ? unscaled_parameters[0]
	    : (struct st_graphics_parameter){.kind=ST_GRAPHICS_END};
	return status;
}

static int
zone_next(void *context, const struct st_graphics_command_view *view,
    struct st_graphics_parameter *result)
{
	struct cterm_igs *igs = context;
	int zone = igs->input.zone;
	int status = igs->input.zone_configuring ? 0 : preview_first(context, view, &zone);
	*result = (struct st_graphics_parameter){.kind = ST_GRAPHICS_END};
	if (status != 0 || zone == 9997 || zone == 9998 || zone == 9999) return status;
	if (view->count < 5) *result = point_parameters[view->count-1];
	else if (view->count == 5) *result = unscaled_parameters[0];
	else if (view->count == 6) {
		struct cterm_igs preview = *igs;
		int length;
		status = cterm_igs_argument_value(&preview, &view->arguments[5], &length);
		if (status == 0) *result = (struct st_graphics_parameter){.kind = ST_GRAPHICS_BYTES,
		    .size = (size_t)cterm_igs_zone_length(length)+1};
	}
	return status;
}

static int
macro_next(void *context, const struct st_graphics_command_view *view,
    struct st_graphics_parameter *result)
{
	struct cterm_igs *igs = context;
	int operation = igs->input.operation;
	int status = igs->input.configuring ? 0 : preview_first(context, view, &operation);
	*result = (struct st_graphics_parameter){.kind = ST_GRAPHICS_END};
	if (status != 0) return status;
	if (operation == 1 && view->count < 2) *result = unscaled_parameters[0];
	if (operation == 2) {
		if (view->count < 4) *result = unscaled_parameters[0];
		else if (view->count == 4) {
			struct cterm_igs preview = *igs;
			int length;
			status = cterm_igs_argument_value(&preview, &view->arguments[3], &length);
			if (status == 0) *result = (struct st_graphics_parameter){.kind = ST_GRAPHICS_BYTES,
			    .size = (size_t)cterm_igs_macro_length(length)+1};
		}
	}
	return status;
}

/* X5's mode decision is frozen once its extra readers have begun. */
static int
flow_mode_next(void *context, const struct st_graphics_command_view *view,
    struct st_graphics_parameter *result)
{
	struct cterm_igs *igs = context;
	int mode = igs->flow_configuring;
	int status = mode != 0 ? 0 : preview_first(context, view, &mode);
	if (status != 0) return status;
	if (igs->flow_configuring == 0 && igs->loop_mode != 0) mode = 1;
	*result = (mode == 2 || mode == 3) && view->count < 3 ? unscaled_parameters[0]
	    : (struct st_graphics_parameter){.kind = ST_GRAPHICS_END};
	return 0;
}

static void
set_flow_mode(struct cterm_igs *igs, int mode)
{
	igs->flow_mode = igs->loop_mode != 0 ? 1 : mode;
	if (igs->flow_mode > 3) {
		igs->flow_on = 17; igs->flow_off = 19;
		igs->flow_on_count = igs->flow_off_count = 1;
	}
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
	if (preview.script.configuring)
		selector = 9;
	else if (preview.input.left_configuring)
		selector = 6;
	else if (preview.input.zone_configuring)
		selector = 4;
	else if (preview.input.configuring)
		selector = 3;
	else if (preview.flow_configuring != 0)
		selector = 5;
	else if (preview.bitmap_loading)
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
	{'&', unscaled_parameters, 4, cterm_igs_loop_next},
	{'<', unscaled_parameters, 3, NULL},
	{'?', unscaled_parameters, 1, query_next},
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
	cterm_render_record_error(igs->terminal,cterm_igs_loop_cancel(igs));
	cterm_render_record_error(igs->terminal,cterm_igs_script_cancel(igs));
	cterm_render_record_error(igs->terminal,cterm_igs_collect_cancel(igs));
	cterm_render_record_error(igs->terminal,cterm_igs_query_cancel(igs));
	cterm_render_record_error(igs->terminal, cterm_igs_music_cancel(igs));
	cterm_igs_input_cancel(igs);
	st_graphics_parser_cancel(igs->parser);
	igs->input_deadline = igs->text_deadline = igs->wait_deadline = 0;
	igs->pending_resolution = -1;
	igs->pending_palette = 0;
	igs->timed_pause = igs->pause_key = false;
	igs->note = (struct cterm_igs_note){0};
	igs->flow_configuring = 0;
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
cterm_igs_random_tail(struct cterm_igs *igs, int *result)
{
	int value = *result;
	/* The reader tests r, then R, once each after all other evaluation.
	 * A generated R sentinel reaches the second test; r is never revisited.
	 * Stored-operand acquisition retains both encodings without drawing. */
	for (unsigned which = 0; which < 2 && !igs->loop.loading; ++which) {
		if (value != -32765 + (int)which)
			continue;
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
	}
	*result = value;
	return 0;
}

int
cterm_igs_argument_value(struct cterm_igs *igs, const struct st_graphics_argument *argument, int *result)
{
	if (igs == NULL || argument == NULL || result == NULL
	    || argument->kind != ST_GRAPHICS_NUMBER
	    || argument->axis < ST_GRAPHICS_UNSCALED || argument->axis > ST_GRAPHICS_Y)
		return IO_ERROR_INVALID_ARGUMENT;
	if (argument->has_value) {
		if (argument->value<INT16_MIN || argument->value>INT16_MAX) return IO_ERROR_OUT_OF_RANGE;
		*result=argument->value;return 0;
	}
	const struct st_graphics_operand *operand = &argument->operand;
	int value;
	if (operand->operation != ST_GRAPHICS_VALUE && !igs->loop.initializing)
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
	case ST_GRAPHICS_LOOP_X:
	case ST_GRAPHICS_LOOP_Y:
		if (!igs->loop.initializing) return IO_ERROR_PROTOCOL;
		value=operand->source==ST_GRAPHICS_LOOP_X?-32767:-32766;break;
	case ST_GRAPHICS_RANDOM_R:
	case ST_GRAPHICS_RANDOM_R2:
		value = operand->source == ST_GRAPHICS_RANDOM_R ? -32765 : -32764;
		break;
	default:
		return IO_ERROR_PROTOCOL;
	}
	if (operand->operation!=ST_GRAPHICS_VALUE) {
		if (operand->operation==ST_GRAPHICS_STEP_PLUS) value=-value;
		else if (operand->operation==ST_GRAPHICS_STEP_MINUS) value=-10000-value;
		else if (operand->operation==ST_GRAPHICS_MINUS_STEP) value=-20000-value;
		else return IO_ERROR_PROTOCOL;
		if (value<INT16_MIN || value>INT16_MAX) return IO_ERROR_OUT_OF_RANGE;
	}
	int status = cterm_igs_random_tail(igs, &value);
	if (status != 0)
		return status;
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
	return igs->script.active || igs->loop_mode == 1 ? 0 : cterm_igs_flow(igs, true);
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
	status = igs->script.active || igs->loop_mode == 1 ? 0 : cterm_igs_flow(igs, false);
	return status == 0 ? IO_ERROR_IN_PROGRESS : status;
}

static int
store_text(struct cterm_igs *igs, const struct st_graphics_argument *text)
{
	if (text->kind != ST_GRAPHICS_TEXT || text->size > 128)
		return IO_ERROR_PROTOCOL;
	memcpy(igs->shared_text, text->bytes, text->size);
	if (!text->ended_early) {
		igs->shared_text[text->size] = 0; igs->shared_text_size = text->size;
	} else {
		igs->shared_text_size = 0;
		while (igs->shared_text_size < 128 && igs->shared_text[igs->shared_text_size] != 0)
			++igs->shared_text_size;
	}
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
	if (igs->script.active) igs->script.text=parameter->kind==ST_GRAPHICS_TEXT;
	int loop_status=cterm_igs_loop_field(igs,view,parameter);
	if (loop_status!=0 || view->code=='&') return loop_status;
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
	if (view->code=='<' && view->count==1) {
		int cr;
		int status=cterm_igs_argument_value(igs,&view->arguments[0],&cr);
		if (status!=0) return status;
		igs->collect.append_cr=(uint8_t)cr; igs->collect.configuring=true;
	}
	if (view->code=='<' && view->count==2) {
		int status=cterm_igs_argument_value(igs,&view->arguments[1],&igs->collect.type);
		if (status!=0) return status;
	}
	if (view->code=='?' && view->count==1) {
		int selector;
		int status=cterm_igs_argument_value(igs,&view->arguments[0],&selector);
		if (status!=0) return status;
		igs->query.configuring=true; igs->query.selector=selector;
	}
	if (view->code == 'X' && view->count == 1) {
		struct cterm_igs preview = *igs;
		int selector;
		int status = cterm_igs_argument_value(&preview, &view->arguments[0], &selector);
		if (status != 0) return status;
		if (selector == 9) {
			igs->random_seed=preview.random_seed;
			igs->script.configuring=true;
			igs->script.operand_ready=false;
		}
		if (selector == 6) {
			igs->random_seed = preview.random_seed;
			igs->input.left_configuring = true;
		}
	}
	if (view->code=='X' && igs->script.configuring && view->count==2) {
		int status=cterm_igs_argument_value(igs,&view->arguments[1],&igs->script.operation);
		if (status!=0) return status;
		igs->script.operand_ready=true;
		if (igs->script.operation==0 && !igs->script.active && parameter->kind==ST_GRAPHICS_BYTES)
			return cterm_igs_script_load(igs);
		return 0;
	}
	bool pattern = false;
	if (view->code == 'X' && view->count >= 2) {
		struct cterm_igs preview = *igs;
		int selector, slot;
		int status = 0;
		if (igs->input.zone_configuring)
			selector = 4;
		else if (igs->input.configuring)
			selector = 3;
		else if (igs->flow_configuring != 0)
			selector = 5;
		else if (igs->bitmap_loading)
			selector = 11;
		else if (igs->math_printing)
			selector = 13;
		else if (igs->pattern_loading)
			selector = 7;
		else
			status = cterm_igs_argument_value(&preview, &view->arguments[0], &selector);
		if (status != 0) return status;
		if (selector == 4) {
			struct cterm_igs_input *input = &igs->input;
			if (view->count == 2) {
				int zone;
				status = cterm_igs_argument_value(&preview, &view->arguments[1], &zone);
				if (status != 0) return status;
				igs->random_seed = preview.random_seed;
				/* Negative native offsets escape both allocated zone tables. */
				if (zone < 0) return IO_ERROR_OUT_OF_RANGE;
				input->zone = zone > 47 ? 47 : zone;
				input->zone_configuring = true;
			} else {
				int value;
				status = cterm_igs_argument_value(igs, &view->arguments[view->count-1], &value);
				if (status != 0) return status;
				if (view->count <= 6) input->zones[input->zone].rectangle[view->count-3] = (int16_t)value;
				else if (view->count == 7 && parameter->kind == ST_GRAPHICS_BYTES)
					return cterm_igs_zone_load(igs, value);
			}
			return 0;
		}
		if (selector == 3) {
			struct cterm_igs_input *input = &igs->input;
			if (view->count == 2) {
				status = cterm_igs_argument_value(&preview, &view->arguments[1], &input->operation);
				if (status != 0) return status;
				igs->random_seed = preview.random_seed;
				input->configuring = true;
				if (input->operation == 1) input->enabled = 1;
			}
			if (input->operation == 2 && view->count >= 3) {
				int value;
				status = cterm_igs_argument_value(igs, &view->arguments[view->count-1], &value);
				if (status != 0) return status;
				if (view->count == 3) input->enabled = value != 0;
				else if (view->count == 4) input->cr = (uint8_t)value;
				else if (view->count == 5 && parameter->kind == ST_GRAPHICS_BYTES)
					return cterm_igs_macro_load(igs, value);
			}
			return 0;
		}
		if (selector == 5 && view->count == 2) {
			int mode;
			status = cterm_igs_argument_value(&preview, &view->arguments[1], &mode);
			if (status != 0) return status;
			set_flow_mode(igs, mode);
			igs->flow_configuring = igs->flow_mode;
			igs->random_seed = preview.random_seed;
		}
		if (selector == 5 && view->count == 3 && igs->flow_configuring != 0) {
			int byte;
			status = cterm_igs_argument_value(igs, &view->arguments[2], &byte);
			if (status != 0) return status;
			if (igs->flow_configuring == 2) igs->flow_on = (uint8_t)byte;
			else igs->flow_off = (uint8_t)byte;
		}
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
	if ((view->code != 'W' && !pattern) || parameter->kind != ST_GRAPHICS_TEXT || igs->script.active) return 0;
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
execute_command(void *context, const struct st_graphics_command_view *view)
{
	struct cterm_igs *igs = context;
	int values[1+2*CTERM_IGS_MAX_POINTS];
	if (igs->reset_pending)
		return IO_ERROR_INTERRUPTED;
	if (view->code=='X' && igs->script.configuring) {
		struct cterm_igs_script *s=&igs->script;
		int status=0;
		if (view->count==2) status=cterm_igs_argument_value(igs,&view->arguments[1],&s->operation);
		else if (view->count!=3 || s->operation!=0 || s->loading) status=IO_ERROR_PROTOCOL;
		s->configuring=s->operand_ready=false;s->disabled=false;
		if (status!=0 || s->active) return status;
		if (s->operation==2) cterm_igs_script_clear(igs);
		if (s->operation==1 && igs->music->script_length!=0) {
			s->starting=true;
			return IO_ERROR_IN_PROGRESS;
		}
		return 0;
	}
	if (view->code=='<' && igs->collect.configuring) {
		int options;
		igs->collect.configuring=false;
		if (view->count!=3) return IO_ERROR_PROTOCOL;
		int status=cterm_igs_argument_value(igs,&view->arguments[2],&options);
		return status==0?cterm_igs_collect_begin(igs,igs->collect.type,options):status;
	}
	if (view->code=='?' && igs->query.configuring) {
		int style;
		igs->query.configuring=false;
		if (view->count!=2) return IO_ERROR_PROTOCOL;
		int status=cterm_igs_argument_value(igs,&view->arguments[1],&style);
		return status==0?cterm_igs_query_begin(igs,igs->query.selector,style):status;
	}
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
	if (view->code == 'X' && igs->input.left_configuring) {
		int mode;
		igs->input.left_configuring = false;
		if (view->count != 2) return IO_ERROR_PROTOCOL;
		int status = cterm_igs_argument_value(igs, &view->arguments[1], &mode);
		if (status == 0) igs->input.left_mode = (uint8_t)mode;
		return status;
	}
	if (view->code == 'X' && igs->input.zone_configuring) {
		struct cterm_igs_input *input = &igs->input;
		bool complete = view->count == 8 && input->zone_received == (size_t)input->zones[input->zone].length+1;
		input->zone_configuring = input->zone_loading = false; input->zone_deadline = 0;
		return complete ? 0 : IO_ERROR_PROTOCOL;
	}
	if (view->code == 'X' && igs->input.configuring) {
		struct cterm_igs_input *input = &igs->input;
		int status = 0;
		if (input->operation == 1) {
			int cr;
			status = cterm_igs_argument_value(igs, &view->arguments[2], &cr);
			if (status == 0) input->cr = (uint8_t)cr;
		} else if (input->operation != 2 || view->count != 6 || input->received != (size_t)input->length+1)
			status = IO_ERROR_PROTOCOL;
		input->configuring = input->loading = false; input->deadline = 0;
		return status;
	}
	if (view->code == 'X' && igs->flow_configuring != 0) {
		if (view->count != 4) return IO_ERROR_PROTOCOL;
		int count, mode = igs->flow_configuring;
		igs->flow_configuring = 0;
		int status = cterm_igs_argument_value(igs, &view->arguments[3], &count);
		if (status != 0) return status;
		if (mode == 2) igs->flow_on_count = count;
		else igs->flow_off_count = count;
		return 0;
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
	case '?': return cterm_igs_query_begin(igs,values[0],0);
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
		int status = igs->script.active || igs->loop_mode == 1 ? 0 : cterm_igs_flow(igs, false);
		if (status == 0)
			status = view->code == 'F' ? cterm_igs_flood(igs,values[0],values[1])
			    : view->code == 'f' ? cterm_igs_polygon(igs, values+1, (size_t)count)
			    : cterm_igs_polyline(igs, values+1, (size_t)count);
		if (!igs->reset_pending && !igs->script.active && igs->loop_mode != 1) {
			int flow_status = cterm_igs_flow(igs, true);
			if (status == 0) status = flow_status;
		}
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
		if (values[0] == 5) { set_flow_mode(igs, values[1]); return 0; }
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
		if (values[0] == 4) return cterm_igs_zone_control(igs, values[1]);
		if (values[0] == 3) {
			/* The extra-reader cases have already published their stores. */
			if (values[1] == 0 || values[1] > 2) igs->input.enabled = 0;
			return 0;
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
		/* All sixteen selectors are handled above. Out-of-range words
		 * have native one-argument no-op arity. */
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

/* Only the outer attention loop samples macro buttons and sends < data. */
static int
end_entry(struct cterm_igs *igs, bool chained)
{
	if (chained) return 0;
	if (igs->script.active) {
		igs->loop_mode=0; igs->loop_kind=' '; igs->loop.ganged=false;
		return cterm_igs_collect_send(igs);
	}
	int status=cterm_igs_collect_send(igs);
	if (status==0) status=cterm_igs_input_activate(igs);
	if (status==0 && (igs->input.held || igs->input.left_held)) {
		igs->input.suspended=true;status=IO_ERROR_IN_PROGRESS;
	} else if (status==0) status=cterm_igs_forward_keys(igs);
	return status;
}

static int
command(void *context, const struct st_graphics_command_view *view)
{
	struct cterm_igs *igs=context;
	if (view->code=='&' && igs->loop.initializing) return cterm_igs_loop_begin(igs,view);
	int status=execute_command(context,view);
	/* Invalid T kind jumps to chain postprocessing without testing the
	 * repeated-body predicate, even when its reader left loop mode set. */
	if (status==0 && view->code=='T' && view->count==1) igs->loop.bad=false;
	if (igs->script.starting) return status;
	if (igs->loop.run) {
		if (status==0) {igs->loop.next=true;return IO_ERROR_IN_PROGRESS;}
		return status;
	}
	if (igs->script.active) {
		if (status==0) {igs->script.ready=true; return IO_ERROR_IN_PROGRESS;}
		return status;
	}
	return status==0?end_entry(igs,view->chained):status;
}

static int
resume_parser(struct cterm_igs *igs)
{
	if (igs->loop.run) {igs->loop.next=true;return 0;}
	if (igs->script.active) {igs->script.ready=true;return 0;}
	int status = st_graphics_parser_resume(igs->parser);
	if (status == 0 && !st_graphics_parser_in_command(igs->parser)) {
		bool feeding=igs->feeding; igs->feeding=true;
		status=cterm_igs_collect_send(igs);
		igs->feeding=feeding;
		if (status==0 && !igs->reset_pending) status = cterm_igs_input_activate(igs);
	}
	return status;
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
	if (igs->script.loading) {
		if (view->code!='X' || parameter->kind!=ST_GRAPHICS_BYTES || offset!=igs->script.received) {
			*accepted=0; return IO_ERROR_PROTOCOL;
		}
		return cterm_igs_script_bytes(igs,bytes,size,accepted,end);
	}
	if (view->code == 'N') {
		if (parameter->kind != ST_GRAPHICS_BYTES || offset != igs->music->received) {
			*accepted = 0; return IO_ERROR_PROTOCOL;
		}
		return cterm_igs_music_bytes(igs, bytes, size, accepted, end);
	}
	if (igs->input.zone_loading) {
		if (view->code != 'X' || parameter->kind != ST_GRAPHICS_BYTES || offset != igs->input.zone_received) {
			*accepted = 0; return IO_ERROR_PROTOCOL;
		}
		return cterm_igs_zone_bytes(igs, bytes, size, accepted);
	}
	if (igs->input.loading) {
		if (view->code != 'X' || parameter->kind != ST_GRAPHICS_BYTES || offset != igs->input.received) {
			*accepted = 0; return IO_ERROR_PROTOCOL;
		}
		return cterm_igs_macro_bytes(igs, bytes, size, accepted);
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
	    .command = command, .unknown = unknown, .field = field_entry, .raw = raw, .number = cterm_igs_loop_number};
}

/* Serialized owner pump. A budget yields locally; it never limits the total
 * number of valid protocol iterations or creates another clock/parser. */
static int
drive_parser(struct cterm_igs *igs,const uint8_t *bytes,size_t size,size_t *used)
{
	struct cterm_igs_loop *l=&igs->loop;
	struct st_graphics_parser_sink callbacks=sink(igs);
	*used=0;l->work=0;l->cooperative=false;
	unsigned iterations=0;
	for (;;) {
		if (l->run && l->next) {
			if (!l->initial && (igs->loop_mode!=1 || !l->bad)) {
				bool chained=l->chained;
				int status=cterm_igs_loop_end(igs);
				if (status==0 && igs->script.active) {igs->script.ready=true; return IO_ERROR_IN_PROGRESS;}
				if (status==0) status=end_entry(igs,chained);
				if (status==0) status=st_graphics_parser_resume(igs->parser);
				if (status!=0) return status;
			} else {l->initial=false;l->next=false;l->selecting=true;l->bad=false;}
		}
		if (l->run && l->selecting) {
			if (iterations++>=64) {l->cooperative=true;return IO_ERROR_IN_PROGRESS;}
			int code=l->target;
			if (l->ganged) {
				int selector;int status=cterm_igs_loop_value(igs,&selector);
				if (status!=0) return status;
				code=l->gang[selector<0 || selector>127?0:(size_t)selector];
			}
			l->command=(uint8_t)code;
			int status=code=='&'?IO_ERROR_NOT_SUPPORTED:st_graphics_parser_start_command(igs->parser,&callbacks,(uint8_t)code,l->chained);
			l->selecting=false;
			if (status==IO_ERROR_NOT_SUPPORTED) {unknown(igs,(uint8_t)code);l->next=true;continue;}
			if (status!=0) return status;
		}
		size_t consumed=0;
		int status=st_graphics_parser_feed(igs->parser,&callbacks,bytes==NULL?NULL:bytes+*used,size-*used,&consumed);
		*used+=consumed;
		if (status==IO_ERROR_IN_PROGRESS && l->run && l->next) continue;
		return status;
	}
}

/* The file attention owner acquires command/chain words; field grammar and
 * repeated bodies still use the one shared parser above. Cursor denotes the
 * next unread byte, equivalent to the native helper's last-byte cursor plus
 * one. Every byte is fetched from the live music allocation at use time. */
static int
drive_script(struct cterm_igs *igs)
{
	struct cterm_igs_script *s=&igs->script;
	struct st_graphics_parser_sink callbacks=sink(igs);
	s->cooperative=false;
	for (unsigned work=0;work<1024;++work) {
		if (igs->reset_pending) return IO_ERROR_INTERRUPTED;
		if (s->post || s->paused) {
			int key=cterm_igs_take_key(igs);
			if (s->paused) {
				if (key!=17) {
					if (igs->input.key_count!=0) continue;
					return IO_ERROR_IN_PROGRESS;
				}
				s->paused=false;
			} else if (key==19) {s->paused=true;continue;}
			else if (key==3) {
				size_t total=igs->music->script_length;
				s->parsed=total==SIZE_MAX?SIZE_MAX:total+1;
			}
			s->post=false;
		}
		if (s->ready) {
			int status=end_entry(igs,s->chained);
			if (status!=0) return status;
			s->ready=false;
			st_graphics_parser_cancel(igs->parser);
			if (s->chained) s->header=true;
			else s->post=true;
			continue;
		}
		if (s->header) {
			/* Both acquisitions occur even when the first byte is NUL. An
			 * incomplete pair is invalid source input, never fabricated RAM. */
			if (s->cursor>=igs->music->script_length || s->cursor>=CTERM_IGS_MUSIC_BYTES) return IO_ERROR_PROTOCOL;
			uint8_t code=igs->music->bytes[s->cursor++];++s->parsed;
			if (s->cursor>=igs->music->script_length || s->cursor>=CTERM_IGS_MUSIC_BYTES) return IO_ERROR_PROTOCOL;
			uint8_t chain=igs->music->bytes[s->cursor++];++s->parsed;
			s->chained=chain=='>';s->header=false;s->text=false;
			if (code==0 || chain==0) {s->ready=true;continue;}
			int status=st_graphics_parser_start_command(igs->parser,&callbacks,code,s->chained);
			if (status==IO_ERROR_NOT_SUPPORTED) {unknown(igs,code);s->ready=true;continue;}
			if (status!=0) return status;
		}
		if (st_graphics_parser_in_command(igs->parser)) {
			size_t used;
			/* Establish field effects before fetching the next live byte. */
			int status=drive_parser(igs,NULL,0,&used);
			if (status==IO_ERROR_IN_PROGRESS) {
				if (s->ready) continue;
				return status;
			}
			if (status!=0) return status;
			if (s->cursor>=CTERM_IGS_MUSIC_BYTES && s->cursor<igs->music->script_length) return IO_ERROR_OUT_OF_RANGE;
			if (s->cursor>=igs->music->script_length) {
				if (!s->text) return IO_ERROR_PROTOCOL;
				++s->cursor;++s->parsed;
				status=st_graphics_parser_end_text(igs->parser,&callbacks);
			} else {
				uint8_t byte=igs->music->bytes[s->cursor];
				if (s->text && byte==0) {
					++s->cursor;++s->parsed;
					status=st_graphics_parser_end_text(igs->parser,&callbacks);
				} else {
					status=drive_parser(igs,&byte,1,&used);
					s->cursor+=used;s->parsed+=used;
				}
			}
			if (status==IO_ERROR_IN_PROGRESS) {
				if (s->ready) continue;
				return status;
			}
			if (status!=0) return status;
			continue;
		}
		if (s->parsed>=igs->music->script_length || s->cursor>=igs->music->script_length) return 0;
		if (s->cursor>=CTERM_IGS_MUSIC_BYTES) return IO_ERROR_OUT_OF_RANGE;
		uint8_t byte=igs->music->bytes[s->cursor++];++s->parsed;
		int status=0;
		if (s->pending_g && (byte!='#' || s->disabled)) {
			const uint8_t g='G';status=cterm_vt52_output(igs->terminal,&g,1);
			if (status==0 && byte!='G') status=cterm_vt52_output(igs->terminal,&byte,1);
		} else if (!s->pending_g && byte!='G') status=cterm_vt52_output(igs->terminal,&byte,1);
		bool dispatch=s->pending_g && byte=='#' && !s->disabled;
		s->pending_g=byte=='G';
		if (status!=0) return status;
		if (dispatch) s->header=true;
		else s->post=true;
	}
	s->cooperative=true;
	return IO_ERROR_IN_PROGRESS;
}

static int
return_script(struct cterm_igs *igs,bool failed)
{
	struct cterm_igs_script *s=&igs->script;
	struct st_graphics_continuation continuation={.chained=s->return_chained};
	bool loop=s->return_loop,flow=s->return_flow;
	if (failed) {
		/* Retain accepted stores/output, unwind readers, then return to the
		 * caller's real boundary. No script bytes leak into the wire parser. */
		s->return_flow=false;
		cancel_input(igs);
		igs->loop_mode=0;
		if (igs->reset_pending) return IO_ERROR_INTERRUPTED;
	} else {
		st_graphics_parser_cancel(igs->parser);
		s->active=s->starting=s->ready=s->paused=s->cooperative=s->return_flow=false;
		s->disabled=false;
	}
	int status=st_graphics_parser_restore_continuation(igs->parser,&continuation);
	if (status!=0) return status;
	if (loop) {
		igs->loop.run=igs->loop.next=true;igs->loop.initial=false;
		igs->loop.did_flow=flow;igs->loop.chained=continuation.chained;
		return 0;
	}
	status=end_entry(igs,continuation.chained);
	return status==0?st_graphics_parser_resume(igs->parser):status;
}

static int
drive_input(struct cterm_igs *igs,const uint8_t *bytes,size_t size,size_t *used)
{
	struct cterm_igs_script *s=&igs->script;
	*used=0;
	s->cooperative=false;
	for (unsigned work=0;;++work) {
		/* Short sources can return into an enclosing loop or another X9
		 * in this same wire batch. Their combined owner work also yields. */
		if (work>=64) {s->cooperative=true;return IO_ERROR_IN_PROGRESS;}
		if (s->starting) {
			struct st_graphics_continuation continuation;
			int status=st_graphics_parser_take_continuation(igs->parser,&continuation);
			if (status!=0) return status;
			s->return_chained=continuation.chained;
			s->return_loop=igs->loop.run;s->return_flow=igs->loop.did_flow;
			igs->loop.run=igs->loop.next=igs->loop.selecting=igs->loop.waiting=igs->loop.cooperative=igs->loop.did_flow=false;
			s->starting=false;s->active=true;s->cursor=s->parsed=0;
			s->ready=s->header=s->pending_g=s->paused=s->post=s->text=false;
			igs->input_deadline=igs->text_deadline=0;
		}
		if (s->active) {
			int status=drive_script(igs);
			if (igs->reset_pending) return IO_ERROR_INTERRUPTED;
			if (status==IO_ERROR_IN_PROGRESS) return status;
			if (status!=0) cterm_log(igs->terminal,ST_CTERM_LOG_WARNING,"IGS script rejected (%d); returning to caller",status);
			status=return_script(igs,status!=0);
			if (status!=0) return status;
		}
		size_t consumed;
		int status=drive_parser(igs,bytes==NULL?NULL:bytes+*used,size-*used,&consumed);
		*used+=consumed;
		if (s->starting) continue;
		return status;
	}
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
	(void)cterm_igs_collect_cancel(igs);
	(void)cterm_igs_query_cancel(igs);
	cterm_igs_music_stop(igs);
	free(igs->music);
	free(igs->input.zones);
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
	    || (igs->music != NULL && igs->music->playing) || igs->script.active || igs->script.starting || igs->script.cooperative || igs->input.held || igs->input.left_held || igs->input.suspended || igs->query.active || igs->collect.active || igs->loop.next || igs->loop.selecting || igs->loop.waiting || igs->loop.cooperative);
}

bool
cterm_igs_handle_key(struct cterm_igs *igs, const struct cterm_key *key)
{
	if (igs != NULL && igs->input.replaying) return false;
	if (igs != NULL && !igs->terminal->quiet)
		cterm_render_record_error(igs->terminal,cterm_igs_audio_console(igs,true));
	if (igs != NULL && igs->note.active && !igs->note.key) {
		igs->note.key = true;
		return true;
	}
	/* An async body owns its first key before the enclosing loop's next
	 * numeric poll. Later keys remain available to that outer poll. */
	if (igs != NULL && (igs->loop.run || igs->script.active)) {
		if (igs->timed_pause && !igs->pause_key) {igs->pause_key=true;return true;}
		if (igs->palette_rotation.active && !igs->palette_rotation.key) {igs->palette_rotation.key=true;return true;}
	}
	if (igs != NULL && cterm_igs_buffer_key(igs, key)) {
		if (igs->collect.active && !igs->feeding)
			cterm_render_record_error(igs->terminal,cterm_igs_poll(igs));
		return true;
	}
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
		igs->feeding = true;
		status = drive_input(igs,input==NULL?NULL:input+*used,size-*used,&consumed);
		igs->feeding = false;
		*used += consumed;
		bool reset = igs->reset_pending;
		if (reset) {
			cterm_igs_reset(igs);
			if (status == IO_ERROR_INTERRUPTED) status = 0;
		}
		if (status != 0 && status != IO_ERROR_IN_PROGRESS) {
			/* The grammar has cancelled the rejected command. Its unread
			 * suffix still belongs to the input stream: run it through this
			 * stage again so ordinary bytes reach the base terminal parser. */
			cterm_log(igs->terminal, ST_CTERM_LOG_WARNING,
			    "IGS input rejected (%d); continuing stream", status);
			cterm_render_record_error(igs->terminal,cterm_igs_loop_cancel(igs));
			cterm_render_record_error(igs->terminal,cterm_igs_script_cancel(igs));
			st_graphics_parser_cancel(igs->parser);
			cterm_render_record_error(igs->terminal, cterm_igs_music_cancel(igs));
			cterm_igs_input_cancel_reader(igs);
			igs->collect.configuring=false;
			igs->query.configuring=false;
			igs->flow_configuring = 0;
			igs->pattern_loading = false;
			igs->pattern_slot = 0;
			igs->math_printing = false;
			cterm_igs_bitmap_cancel(igs);
			status = 0;
		}
		else if (!reset) break;
	} while (status == 0 && *used < size);
	if (status == IO_ERROR_IN_PROGRESS)
		status = 0;
	if (status != 0) {
		cterm_render_record_error(igs->terminal, cterm_igs_music_cancel(igs));
		cterm_igs_input_cancel_reader(igs);
		igs->collect.configuring=false;
		igs->query.configuring=false;
		igs->flow_configuring = 0;
		igs->pattern_loading = false;
		igs->pattern_slot = 0;
		igs->math_printing = false;
		cterm_igs_bitmap_cancel(igs);
	}
	if (status == 0 && !igs->feeding && !igs->script.active && !st_graphics_parser_in_command(igs->parser)) {
		igs->feeding=true; status=cterm_igs_collect_send(igs); igs->feeding=false;
		if (igs->reset_pending) { cterm_igs_reset(igs); if (status==IO_ERROR_INTERRUPTED) status=0; }
		if (status==0) status = cterm_igs_input_activate(igs);
	}
	if (status == 0 && igs->script.loading)
		igs->input_deadline = igs->script.deadline;
	else if (status == 0 && igs->input.zone_loading)
		igs->input_deadline = igs->input.zone_deadline;
	else if (status == 0 && igs->input.loading)
		igs->input_deadline = igs->input.deadline;
	else if (status == 0 && igs->music->loading)
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
	else if (status != 0 && status != IO_ERROR_INTERRUPTED) {
		cterm_log(igs->terminal, ST_CTERM_LOG_WARNING,
		    "IGS input rejected at end of stream (%d)", status);
		status = 0;
	}
	if (igs->reset_pending) {
		cterm_igs_reset(igs);
		if (status == IO_ERROR_INTERRUPTED)
			status = 0;
	}
	cterm_igs_input_cancel(igs);
	(void)cterm_igs_loop_cancel(igs);
	(void)cterm_igs_script_cancel(igs);
	(void)cterm_igs_collect_cancel(igs);
	igs->query.configuring=false;
	igs->input_deadline = igs->text_deadline = 0;
	igs->flow_configuring = 0;
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
	if (igs->input.sending) return 0;
	if ((igs->loop.run || igs->script.active) && !igs->feeding && igs->input.key_count!=0) {
		/* A key queued before the body started is still available to its
		 * own poll. N retains its distinct inner/outer FIFO policy. */
		bool *key=igs->note.active && !igs->music->playing?&igs->note.key
		    : igs->timed_pause?&igs->pause_key
		    : igs->palette_rotation.active?&igs->palette_rotation.key:NULL;
		if (key!=NULL && !*key) {(void)cterm_igs_take_key(igs);*key=true;}
	}
	if (igs->collect.active) {
		if (igs->reset_pending) { cterm_igs_reset(igs); return 0; }
		igs->feeding=true;
		int status=cterm_igs_collect_resume(igs);
		igs->feeding=false;
		if (igs->reset_pending) { cterm_igs_reset(igs); return status==IO_ERROR_INTERRUPTED?0:status; }
		if (status!=0) { cancel_input(igs); return status; }
		if (igs->collect.active) return 0;
		status=resume_parser(igs);
		if (igs->reset_pending) { cterm_igs_reset(igs); return status==IO_ERROR_INTERRUPTED?0:status; }
		if (status!=0) return status;
	}
	if (igs->query.active) {
		if (igs->reset_pending) { cterm_igs_reset(igs); return 0; }
		if (!igs->query.ready) return 0;
		igs->feeding=true;
		int status=cterm_igs_query_finish(igs);
		igs->feeding=false;
		if (igs->reset_pending) { cterm_igs_reset(igs); return status==IO_ERROR_INTERRUPTED?0:status; }
		if (status!=0) { cancel_input(igs); return status; }
		status=resume_parser(igs);
		if (igs->reset_pending) { cterm_igs_reset(igs); return status==IO_ERROR_INTERRUPTED?0:status; }
		if (status!=0) return status;
	}
	bool left_finished = false;
	if (igs->input.left_held && igs->input.buttons != 1) {
		int status = cterm_igs_left_finish(igs);
		if (igs->reset_pending) { cterm_igs_reset(igs); return status == IO_ERROR_INTERRUPTED ? 0 : status; }
		if (status != 0) return status;
		left_finished = true;
	}
	if (igs->input.held || igs->input.left_held) return 0;
	if (igs->input.suspended) {
		igs->input.suspended = false;
		int status = st_graphics_parser_resume(igs->parser);
		if (status != 0) return status;
	}
	if (left_finished && igs->input.buttons == 2) {
		int status = cterm_igs_input_activate(igs);
		if (status != 0 || igs->input.held) return status;
	}
	if (igs->input.key_count != 0 && !igs->script.active && !st_graphics_parser_in_command(igs->parser) && !igs->input.loading && !igs->input.zone_loading && !igs->music->loading && !igs->music->playing) {
		igs->feeding = true;
		int status = cterm_igs_forward_keys(igs);
		igs->feeding = false;
		if (igs->reset_pending) { cterm_igs_reset(igs); return status == IO_ERROR_INTERRUPTED ? 0 : status; }
		if (status != 0) return status;
	}
	uint64_t now;
	if (igs->note.active) {
		int status = igs->now_ms(&now);
		if (status != 0 || (!igs->note.key && now < igs->wait_deadline)) return status;
		status = cterm_igs_audio_note_end(igs);
		if (status != 0) return status;
		if (!igs->music->playing) return resume_parser(igs);
	}
	if (igs->music->playing) {
		igs->feeding = true;
		int status = cterm_igs_music_resume(igs);
		igs->feeding = false;
		if (igs->reset_pending) { cterm_igs_reset(igs); return status == IO_ERROR_INTERRUPTED ? 0 : status; }
		if (status == IO_ERROR_IN_PROGRESS) return 0;
		if (status != 0) { cancel_input(igs); return status; }
		return resume_parser(igs);
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
		return resume_parser(igs);
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
		return resume_parser(igs);
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
		return resume_parser(igs);
	}
	if ((igs->script.active || igs->script.starting || igs->script.cooperative) && igs->wait_deadline==0) {
		size_t used;igs->feeding=true;
		int status=drive_input(igs,NULL,0,&used);igs->feeding=false;
		if (igs->reset_pending) {cterm_igs_reset(igs);return status==IO_ERROR_INTERRUPTED?0:status;}
		if (status==IO_ERROR_IN_PROGRESS) status=0;
		if (status!=0) {cancel_input(igs);return status;}
		return 0;
	}
	if (igs->loop.run && (igs->loop.next || igs->loop.selecting || igs->loop.waiting || igs->loop.cooperative)) {
		size_t used;igs->feeding=true;
		int status=drive_input(igs,NULL,0,&used);igs->feeding=false;
		if (igs->reset_pending) {cterm_igs_reset(igs);return status==IO_ERROR_INTERRUPTED?0:status;}
		if (status==IO_ERROR_IN_PROGRESS) status=0;
		if (status!=0) {cancel_input(igs);return status;}
		return 0;
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
			return resume_parser(igs);
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
		return resume_parser(igs);
	}
	if (igs->script.loading) {
		int status=igs->now_ms(&now);
		if (status!=0 || now<igs->script.deadline) return status;
		const uint8_t space=' ';size_t used;
		igs->feeding=true;
		status=drive_input(igs,&space,1,&used);igs->feeding=false;
		if (igs->reset_pending) {cterm_igs_reset(igs);return status==IO_ERROR_INTERRUPTED?0:status;}
		if (status==IO_ERROR_IN_PROGRESS) status=0;
		if (status!=0) {cancel_input(igs);return status;}
		igs->input_deadline=igs->script.loading?igs->script.deadline:0;
		return 0;
	}
	if (igs->input.loading || igs->input.zone_loading) {
		int status = igs->now_ms(&now);
		if (status != 0 || now < (igs->input.zone_loading ? igs->input.zone_deadline : igs->input.deadline)) return status;
		const uint8_t space = ' '; size_t used;
		struct st_graphics_parser_sink callbacks = sink(igs);
		igs->feeding = true;
		status = st_graphics_parser_feed(igs->parser, &callbacks, &space, 1, &used);
		igs->feeding = false;
		if (igs->reset_pending) { cterm_igs_reset(igs); return status == IO_ERROR_INTERRUPTED ? 0 : status; }
		if (status == IO_ERROR_IN_PROGRESS) status = 0;
		if (status != 0) { cancel_input(igs); return status; }
		igs->input_deadline = igs->input.zone_loading ? igs->input.zone_deadline : igs->input.loading ? igs->input.deadline : 0;
		return 0;
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
		if (status == IO_ERROR_IN_PROGRESS) status = 0;
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

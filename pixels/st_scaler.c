/* Persistent implementation of the SyncTERM internal scaling pipeline. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_pixels.h"

#include "pixels_internal.h"

#include "io_error.h"
#include "st_scaler_internal.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct st_pixel_scaler {
	uint32_t *work[2];
	size_t capacity[2];
	struct st_pixel_buffer output;
	uint32_t source_width;
	uint32_t source_height;
	uint32_t width;
	uint32_t height;
	uint32_t exact_x_repeat;
	uint32_t exact_y_repeat;
	struct scaler_stage *stages;
	size_t stage_count;
	size_t stage_capacity;
	enum st_pixel_scaling_policy policy;
	bool valid;
	struct axis_sample *x_samples;
	struct axis_sample *y_samples;
	struct axis_damage *x_damage;
	struct axis_damage *y_damage;
	uint32_t *vertical_line;
	size_t x_sample_capacity;
	size_t y_sample_capacity;
	size_t x_damage_capacity;
	size_t y_damage_capacity;
	size_t vertical_line_capacity;
};

struct axis_sample {
	uint32_t first;
	uint32_t second;
	uint32_t weight;
};

struct axis_damage {
	uint32_t first;
	uint32_t end;
};

enum scaler_stage_kind {
	SCALER_STAGE_MULTIPLY,
	SCALER_STAGE_POINTY_ODD,
	SCALER_STAGE_POINTY5,
	SCALER_STAGE_POINTY3,
	SCALER_STAGE_XBR4,
	SCALER_STAGE_XBR2,
	SCALER_STAGE_HEIGHT,
	SCALER_STAGE_WIDTH,
};

struct scaler_stage {
	enum scaler_stage_kind kind;
	uint32_t input_width;
	uint32_t input_height;
	uint32_t output_width;
	uint32_t output_height;
	uint32_t x_multiplier;
	uint32_t y_multiplier;
	uint32_t repeated_rows;
	uint32_t *pixels;
	size_t pixel_capacity;
	struct axis_sample *samples;
	struct axis_damage *damage;
	size_t sample_capacity;
	size_t damage_capacity;
};

#define pixel_count st_pixels_count_u32
#define blend_pixel st_pixels_blend

static int
ensure_capacity(st_pixel_scaler_t *scaler, unsigned int index, size_t count)
{
	uint32_t *replacement;

	if (scaler->capacity[index] >= count)
		return 0;
	replacement = realloc(scaler->work[index], count * sizeof(*replacement));
	if (replacement == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	scaler->work[index] = replacement;
	scaler->capacity[index] = count;
	return 0;
}

int
st_pixel_scaler_create(st_pixel_scaler_t **result)
{
	st_pixel_scaler_t *scaler;

	if (result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	scaler = calloc(1, sizeof(*scaler));
	if (scaler == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	*result = scaler;
	return 0;
}

void
st_pixel_scaler_destroy(st_pixel_scaler_t *scaler)
{
	if (scaler == NULL)
		return;
	for (size_t i = 0; i < scaler->stage_capacity; ++i) {
		free(scaler->stages[i].damage);
		free(scaler->stages[i].samples);
		free(scaler->stages[i].pixels);
	}
	free(scaler->stages);
	free(scaler->work[1]);
	free(scaler->work[0]);
	free(scaler->y_damage);
	free(scaler->x_damage);
	free(scaler->y_samples);
	free(scaler->x_samples);
	free(scaler->vertical_line);
	free(scaler);
}

static int
ensure_array(void **array, size_t *capacity, size_t count,
	size_t element_size)
{
	void *replacement;

	if (*capacity >= count)
		return 0;
	if (count > SIZE_MAX / element_size)
		return IO_ERROR_OUT_OF_RANGE;
	replacement = realloc(*array, count * element_size);
	if (replacement == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	*array = replacement;
	*capacity = count;
	return 0;
}

static void
make_axis_map(struct axis_sample *samples, struct axis_damage *damage,
	uint32_t output_size, uint32_t source_size)
{
	uint32_t repeat = output_size >= source_size
	    ? output_size / source_size : 1;
	uint32_t expanded = source_size * repeat;
	uint64_t increment = ((uint64_t)expanded << 16) / output_size;
	uint64_t position = 0;

	for (uint32_t source = 0; source < source_size; ++source)
		damage[source] = (struct axis_damage){output_size, 0};
	for (uint32_t output = 0; output < output_size; ++output) {
		uint32_t expanded_first = (uint32_t)(position >> 16);
		uint32_t expanded_second = expanded_first + 1 < expanded
		    ? expanded_first + 1 : expanded_first;
		struct axis_sample sample = {
			.first = expanded_first / repeat,
			.second = expanded_second / repeat,
			.weight = (uint32_t)position & UINT32_C(0xffff),
		};

		samples[output] = sample;
		if (damage[sample.first].first == output_size)
			damage[sample.first].first = output;
		damage[sample.first].end = output + 1;
		if (sample.second != sample.first) {
			if (damage[sample.second].first == output_size)
				damage[sample.second].first = output;
			damage[sample.second].end = output + 1;
		}
		position += increment;
	}
}

static int
append_stage(st_pixel_scaler_t *scaler, enum scaler_stage_kind kind,
	uint32_t input_width, uint32_t input_height, uint32_t output_width,
	uint32_t output_height, uint32_t x_multiplier, uint32_t y_multiplier,
	uint32_t repeated_rows)
{
	struct scaler_stage *stage;
	size_t count;
	int status;

	if (!pixel_count(output_width, output_height, &count))
		return IO_ERROR_OUT_OF_RANGE;
	if (scaler->stage_count == scaler->stage_capacity) {
		size_t old_capacity = scaler->stage_capacity;
		size_t capacity = old_capacity == 0 ? 8 : old_capacity * 2;
		struct scaler_stage *replacement;

		if (capacity < old_capacity
		    || capacity > SIZE_MAX / sizeof(*replacement))
			return IO_ERROR_OUT_OF_RANGE;
		replacement = realloc(scaler->stages,
		    capacity * sizeof(*replacement));
		if (replacement == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		memset(&replacement[old_capacity], 0,
		    (capacity - old_capacity) * sizeof(*replacement));
		scaler->stages = replacement;
		scaler->stage_capacity = capacity;
	}
	stage = &scaler->stages[scaler->stage_count];
	stage->kind = kind;
	stage->input_width = input_width;
	stage->input_height = input_height;
	stage->output_width = output_width;
	stage->output_height = output_height;
	stage->x_multiplier = x_multiplier;
	stage->y_multiplier = y_multiplier;
	stage->repeated_rows = repeated_rows;
	status = ensure_array((void **)&stage->pixels, &stage->pixel_capacity,
	    count, sizeof(stage->pixels[0]));
	if (status != 0)
		return status;
	if (kind == SCALER_STAGE_HEIGHT) {
		status = ensure_array((void **)&stage->samples,
		    &stage->sample_capacity, output_height,
		    sizeof(stage->samples[0]));
		if (status == 0)
			status = ensure_array((void **)&stage->damage,
			    &stage->damage_capacity, input_height,
			    sizeof(stage->damage[0]));
		if (status == 0)
			make_axis_map(stage->samples, stage->damage, output_height,
			    input_height);
	}
	else if (kind == SCALER_STAGE_WIDTH) {
		status = ensure_array((void **)&stage->samples,
		    &stage->sample_capacity, output_width,
		    sizeof(stage->samples[0]));
		if (status == 0)
			status = ensure_array((void **)&stage->damage,
			    &stage->damage_capacity, input_width,
			    sizeof(stage->damage[0]));
		if (status == 0)
			make_axis_map(stage->samples, stage->damage, output_width,
			    input_width);
	}
	if (status != 0)
		return status;
	++scaler->stage_count;
	return 0;
}

static int
prepare_blocky_maps(st_pixel_scaler_t *scaler)
{
	int status;

	status = ensure_array((void **)&scaler->x_samples,
	    &scaler->x_sample_capacity, scaler->width,
	    sizeof(scaler->x_samples[0]));
	if (status == 0)
		status = ensure_array((void **)&scaler->y_samples,
		    &scaler->y_sample_capacity, scaler->height,
		    sizeof(scaler->y_samples[0]));
	if (status == 0)
		status = ensure_array((void **)&scaler->x_damage,
		    &scaler->x_damage_capacity, scaler->source_width,
		    sizeof(scaler->x_damage[0]));
	if (status == 0)
		status = ensure_array((void **)&scaler->y_damage,
		    &scaler->y_damage_capacity, scaler->source_height,
		    sizeof(scaler->y_damage[0]));
	if (status == 0)
		status = ensure_array((void **)&scaler->vertical_line,
		    &scaler->vertical_line_capacity, scaler->source_width,
		    sizeof(scaler->vertical_line[0]));
	if (status != 0)
		return status;
	make_axis_map(scaler->x_samples, scaler->x_damage, scaler->width,
	    scaler->source_width);
	make_axis_map(scaler->y_samples, scaler->y_damage, scaler->height,
	    scaler->source_height);
	scaler->exact_x_repeat = scaler->width % scaler->source_width == 0
	    ? scaler->width / scaler->source_width : 0;
	scaler->exact_y_repeat = scaler->height % scaler->source_height == 0
	    ? scaler->height / scaler->source_height : 0;
	return 0;
}

static void
multiply_scale(const uint32_t *source, uint32_t *destination,
	uint32_t width, uint32_t height, uint32_t x_multiplier,
	uint32_t y_multiplier)
{
	uint32_t destination_width = width * x_multiplier;

	for (uint32_t y = 0; y < height; ++y) {
		uint32_t *row = &destination[(size_t)y * y_multiplier
		    * destination_width];

		for (uint32_t x = 0; x < width; ++x)
			for (uint32_t repeat = 0; repeat < x_multiplier; ++repeat)
				row[(size_t)x * x_multiplier + repeat]
				    = source[(size_t)y * width + x];
		for (uint32_t repeat = 1; repeat < y_multiplier; ++repeat)
			memcpy(&row[(size_t)repeat * destination_width], row,
			    (size_t)destination_width * sizeof(row[0]));
	}
}

static void
interpolate_height(const uint32_t *source, uint32_t *destination,
	uint32_t width, uint32_t height, uint32_t new_height,
	uint32_t repeated_rows)
{
	uint64_t position = 0;
	uint64_t increment = ((uint64_t)height << 16) / new_height;

	for (uint32_t y = 0; y < new_height; ++y) {
		uint32_t first = (uint32_t)(position >> 16);
		uint32_t weight = (uint32_t)position & UINT32_C(0xffff);
		uint32_t second = first + 1 < height ? first + 1 : first;
		bool repeated = repeated_rows > 1
		    && first / repeated_rows == second / repeated_rows;

		if (weight == 0 || first == second || repeated)
			memcpy(&destination[(size_t)y * width],
			    &source[(size_t)first * width],
			    (size_t)width * sizeof(source[0]));
		else
			for (uint32_t x = 0; x < width; ++x)
				destination[(size_t)y * width + x] = blend_pixel(
				    source[(size_t)first * width + x],
				    source[(size_t)second * width + x], weight);
		position += increment;
	}
}

static void
interpolate_width(const uint32_t *source, uint32_t *destination,
	uint32_t width, uint32_t height, uint32_t new_width)
{
	uint64_t increment = ((uint64_t)width << 16) / new_width;

	for (uint32_t y = 0; y < height; ++y) {
		uint64_t position = 0;

		for (uint32_t x = 0; x < new_width; ++x) {
			uint32_t first = (uint32_t)(position >> 16);
			uint32_t second = first + 1 < width ? first + 1 : first;
			uint32_t weight = (uint32_t)position & UINT32_C(0xffff);

			destination[(size_t)y * new_width + x] = blend_pixel(
			    source[(size_t)y * width + first],
			    source[(size_t)y * width + second], weight);
			position += increment;
		}
	}
}

static void
execute_stage_full(const struct scaler_stage *stage, const uint32_t *source)
{
	switch (stage->kind) {
	case SCALER_STAGE_MULTIPLY:
		multiply_scale(source, stage->pixels, stage->input_width,
		    stage->input_height, stage->x_multiplier,
		    stage->y_multiplier);
		break;
	case SCALER_STAGE_POINTY_ODD:
		st_pointy_scale_odd(source, stage->pixels,
		    (int)stage->input_width, (int)stage->input_height,
		    (int)stage->x_multiplier);
		break;
	case SCALER_STAGE_POINTY5:
		st_pointy_scale5(source, stage->pixels,
		    (int)stage->input_width, (int)stage->input_height);
		break;
	case SCALER_STAGE_POINTY3:
		st_pointy_scale3(source, stage->pixels,
		    (int)stage->input_width, (int)stage->input_height);
		break;
	case SCALER_STAGE_XBR4:
		st_xbr_filter(source, stage->pixels, (int)stage->input_width,
		    (int)stage->input_height, 4);
		break;
	case SCALER_STAGE_XBR2:
		st_xbr_filter(source, stage->pixels, (int)stage->input_width,
		    (int)stage->input_height, 2);
		break;
	case SCALER_STAGE_HEIGHT:
		interpolate_height(source, stage->pixels, stage->input_width,
		    stage->input_height, stage->output_height,
		    stage->repeated_rows);
		break;
	case SCALER_STAGE_WIDTH:
		interpolate_width(source, stage->pixels, stage->input_width,
		    stage->input_height, stage->output_width);
		break;
	}
}

static void update_blocky_region(st_pixel_scaler_t *scaler,
	const uint32_t *source, const struct st_pixel_rect *rectangle);

static void
update_exact_blocky_damage(st_pixel_scaler_t *scaler,
	const uint32_t *source, const struct st_pixel_rect *damage)
{
	uint32_t source_end_x = damage->x + damage->width;
	uint32_t source_end_y = damage->y + damage->height;
	uint32_t output_x = damage->x * scaler->exact_x_repeat;
	uint32_t output_width = damage->width * scaler->exact_x_repeat;

	for (uint32_t y = damage->y; y < source_end_y; ++y) {
		uint32_t *first = &scaler->output.primary[(size_t)y
		    * scaler->exact_y_repeat * scaler->width + output_x];
		uint32_t *destination = first;

		for (uint32_t x = damage->x; x < source_end_x; ++x)
			for (uint32_t repeat = 0;
			    repeat < scaler->exact_x_repeat; ++repeat)
				*destination++ = source[(size_t)y
				    * scaler->source_width + x];
		for (uint32_t repeat = 1; repeat < scaler->exact_y_repeat;
		    ++repeat)
			memcpy(first + (size_t)repeat * scaler->width, first,
			    (size_t)output_width * sizeof(first[0]));
	}
}

int
st_pixel_scaler_scale(st_pixel_scaler_t *scaler,
	const struct st_pixel_buffer **result, const uint32_t *source,
	uint32_t source_width, uint32_t source_height, uint32_t width,
	uint32_t height, enum st_pixel_scaling_policy policy)
{
	uint32_t x_integer;
	uint32_t y_integer;
	uint32_t common;
	uint32_t pointy_odd = 1;
	unsigned int pointy_fives = 0;
	unsigned int pointy_threes = 0;
	unsigned int xbr_fours = 0;
	unsigned int xbr_twos = 0;
	uint32_t current_width = source_width;
	uint32_t current_height = source_height;
	uint32_t repeated_rows = 1;
	size_t ignored_count;
	size_t target_count;
	int status;

	if (scaler == NULL || result == NULL || source == NULL
	    || (policy != ST_PIXEL_SCALING_BLOCKY
	    && policy != ST_PIXEL_SCALING_POINTY)
	    || !pixel_count(source_width, source_height, &ignored_count)
	    || !pixel_count(width, height, &target_count))
		return IO_ERROR_INVALID_ARGUMENT;
	scaler->valid = false;
	if (policy == ST_PIXEL_SCALING_BLOCKY) {
		struct st_pixel_rect rectangle = {0, 0, width, height};

		status = ensure_capacity(scaler, 0, target_count);
		scaler->source_width = source_width;
		scaler->source_height = source_height;
		scaler->width = width;
		scaler->height = height;
		scaler->policy = policy;
		scaler->output = (struct st_pixel_buffer){
			.primary = scaler->work[0],
			.width = width,
			.height = height,
		};
		if (status == 0)
			status = prepare_blocky_maps(scaler);
		if (status != 0)
			return status;
		if (scaler->exact_x_repeat != 0
		    && scaler->exact_y_repeat != 0)
			multiply_scale(source, scaler->output.primary, source_width,
			    source_height, scaler->exact_x_repeat,
			    scaler->exact_y_repeat);
		else
			update_blocky_region(scaler, source, &rectangle);
		scaler->valid = true;
		*result = &scaler->output;
		return 0;
	}
	x_integer = width / source_width;
	y_integer = height / source_height;
	if (x_integer == 0)
		x_integer = 1;
	if (y_integer == 0)
		y_integer = 1;
	common = x_integer < y_integer ? x_integer : y_integer;
	if ((common & 1U) != 0 && common > 5) {
		pointy_odd = common;
		x_integer /= common;
		y_integer /= common;
		common = 1;
	}
	while (common > 1 && common % 5 == 0) {
		++pointy_fives;
		x_integer /= 5;
		y_integer /= 5;
		common /= 5;
	}
	while (common > 1 && common % 3 == 0) {
		++pointy_threes;
		x_integer /= 3;
		y_integer /= 3;
		common /= 3;
	}
	while (common > 1 && common % 4 == 0) {
		++xbr_fours;
		x_integer /= 4;
		y_integer /= 4;
		common /= 4;
	}
	while (common > 1 && common % 2 == 0) {
		++xbr_twos;
		x_integer /= 2;
		y_integer /= 2;
		common /= 2;
	}
	scaler->stage_count = 0;
	if (x_integer != 1 || y_integer != 1) {
		status = append_stage(scaler, SCALER_STAGE_MULTIPLY,
		    current_width, current_height, current_width * x_integer,
		    current_height * y_integer, x_integer, y_integer, 1);
		if (status != 0)
			return status;
		current_width *= x_integer;
		current_height *= y_integer;
		repeated_rows = y_integer;
	}
	#define ADD_SQUARE(kind, multiplier) do { \
	status = append_stage(scaler, (kind), current_width, current_height, \
	    current_width * (uint32_t)(multiplier), \
	    current_height * (uint32_t)(multiplier), (multiplier), \
	    (multiplier), 1); \
	if (status != 0) \
		return status; \
	current_width *= (uint32_t)(multiplier); \
	current_height *= (uint32_t)(multiplier); \
	repeated_rows = 1; \
} while (false)
	if ((pointy_odd > 1 || pointy_fives != 0 || pointy_threes != 0
	    || xbr_fours != 0 || xbr_twos != 0)
	    && (width > INT_MAX || height > INT_MAX || pointy_odd > INT_MAX))
		return IO_ERROR_OUT_OF_RANGE;
	if (pointy_odd > 1)
		ADD_SQUARE(SCALER_STAGE_POINTY_ODD, pointy_odd);
	while (pointy_fives-- > 0)
		ADD_SQUARE(SCALER_STAGE_POINTY5, 5);
	while (pointy_threes-- > 0)
		ADD_SQUARE(SCALER_STAGE_POINTY3, 3);
	while (xbr_fours-- > 0)
		ADD_SQUARE(SCALER_STAGE_XBR4, 4);
	while (xbr_twos-- > 0)
		ADD_SQUARE(SCALER_STAGE_XBR2, 2);
	#undef ADD_SQUARE
	if (current_height != height) {
		status = append_stage(scaler, SCALER_STAGE_HEIGHT, current_width,
		    current_height, current_width, height, 1, 1, repeated_rows);
		if (status != 0)
			return status;
		current_height = height;
	}
	if (current_width != width) {
		status = append_stage(scaler, SCALER_STAGE_WIDTH, current_width,
		    current_height, width, current_height, 1, 1, 1);
		if (status != 0)
			return status;
		current_width = width;
	}
	if (scaler->stage_count == 0) {
		status = append_stage(scaler, SCALER_STAGE_MULTIPLY,
		    current_width, current_height, current_width, current_height,
		    1, 1, 1);
		if (status != 0)
			return status;
	}
	for (size_t i = 0; i < scaler->stage_count; ++i) {
		const uint32_t *stage_source = i == 0 ? source
		    : scaler->stages[i - 1].pixels;

		execute_stage_full(&scaler->stages[i], stage_source);
	}
	scaler->output = (struct st_pixel_buffer){
		.primary = scaler->stages[scaler->stage_count - 1].pixels,
		.width = current_width,
		.height = current_height,
	};
	scaler->source_width = source_width;
	scaler->source_height = source_height;
	scaler->width = width;
	scaler->height = height;
	scaler->policy = policy;
	scaler->valid = true;
	*result = &scaler->output;
	return 0;
}

static void
damage_bounds(const struct axis_damage *map, uint32_t first_source,
	uint32_t source_end, uint32_t output_size, uint32_t *first,
	uint32_t *end)
{
	*first = output_size;
	*end = 0;
	for (uint32_t source = first_source; source < source_end; ++source) {
		if (map[source].first < *first)
			*first = map[source].first;
		if (map[source].end > *end)
			*end = map[source].end;
	}
}

static struct st_pixel_rect
expand_damage(const struct st_pixel_rect *damage, uint32_t width,
	uint32_t height, uint32_t radius)
{
	uint32_t end_x = damage->x + damage->width;
	uint32_t end_y = damage->y + damage->height;
	struct st_pixel_rect result;

	result.x = damage->x > radius ? damage->x - radius : 0;
	result.y = damage->y > radius ? damage->y - radius : 0;
	result.width = width - end_x >= radius ? end_x + radius : width;
	result.height = height - end_y >= radius ? end_y + radius : height;
	result.width -= result.x;
	result.height -= result.y;
	return result;
}

static void
multiply_region(const struct scaler_stage *stage, const uint32_t *source,
	const struct st_pixel_rect *damage, struct st_pixel_rect *output_damage)
{
	uint32_t source_end_x = damage->x + damage->width;
	uint32_t source_end_y = damage->y + damage->height;

	*output_damage = (struct st_pixel_rect){
		.x = damage->x * stage->x_multiplier,
		.y = damage->y * stage->y_multiplier,
		.width = damage->width * stage->x_multiplier,
		.height = damage->height * stage->y_multiplier,
	};
	for (uint32_t y = damage->y; y < source_end_y; ++y) {
		uint32_t *first_row = &stage->pixels[(size_t)y
		    * stage->y_multiplier * stage->output_width
		    + output_damage->x];
		uint32_t *destination = first_row;

		for (uint32_t x = damage->x; x < source_end_x; ++x)
			for (uint32_t repeat = 0; repeat < stage->x_multiplier;
			    ++repeat)
				*destination++ = source[(size_t)y
				    * stage->input_width + x];
		for (uint32_t repeat = 1; repeat < stage->y_multiplier; ++repeat)
			memcpy(first_row + (size_t)repeat * stage->output_width,
			    first_row, (size_t)output_damage->width
			    * sizeof(first_row[0]));
	}
}

static void
interpolate_height_region(const struct scaler_stage *stage,
	const uint32_t *source, const struct st_pixel_rect *damage,
	struct st_pixel_rect *output_damage)
{
	uint32_t end_y;

	damage_bounds(stage->damage, damage->y, damage->y + damage->height,
	    stage->output_height, &output_damage->y, &end_y);
	output_damage->x = damage->x;
	output_damage->width = damage->width;
	output_damage->height = end_y - output_damage->y;
	for (uint32_t y = output_damage->y; y < end_y; ++y) {
		const struct axis_sample *sample = &stage->samples[y];
		const uint32_t *first = &source[(size_t)sample->first
		    * stage->input_width];
		const uint32_t *second = &source[(size_t)sample->second
		    * stage->input_width];
		uint32_t *destination = &stage->pixels[(size_t)y
		    * stage->output_width];
		bool repeated = stage->repeated_rows > 1
		    && sample->first / stage->repeated_rows
		    == sample->second / stage->repeated_rows;

		if (sample->first == sample->second || sample->weight == 0
		    || repeated)
			memcpy(&destination[damage->x], &first[damage->x],
			    (size_t)damage->width * sizeof(destination[0]));
		else
			for (uint32_t x = damage->x;
			    x < damage->x + damage->width; ++x)
				destination[x] = blend_pixel(first[x], second[x],
				    sample->weight);
	}
}

static void
interpolate_width_region(const struct scaler_stage *stage,
	const uint32_t *source, const struct st_pixel_rect *damage,
	struct st_pixel_rect *output_damage)
{
	uint32_t end_x;

	damage_bounds(stage->damage, damage->x, damage->x + damage->width,
	    stage->output_width, &output_damage->x, &end_x);
	output_damage->y = damage->y;
	output_damage->width = end_x - output_damage->x;
	output_damage->height = damage->height;
	for (uint32_t y = damage->y; y < damage->y + damage->height; ++y) {
		const uint32_t *source_row = &source[(size_t)y
		    * stage->input_width];
		uint32_t *destination = &stage->pixels[(size_t)y
		    * stage->output_width];

		for (uint32_t x = output_damage->x; x < end_x; ++x) {
			const struct axis_sample *sample = &stage->samples[x];

			destination[x] = blend_pixel(source_row[sample->first],
			    source_row[sample->second], sample->weight);
		}
	}
}

static void
execute_stage_region(const struct scaler_stage *stage,
	const uint32_t *source, const struct st_pixel_rect *damage,
	struct st_pixel_rect *output_damage)
{
	struct st_pixel_rect centers;
	uint32_t multiplier;

	switch (stage->kind) {
	case SCALER_STAGE_MULTIPLY:
		multiply_region(stage, source, damage, output_damage);
		return;
	case SCALER_STAGE_HEIGHT:
		interpolate_height_region(stage, source, damage, output_damage);
		return;
	case SCALER_STAGE_WIDTH:
		interpolate_width_region(stage, source, damage, output_damage);
		return;
	case SCALER_STAGE_XBR4:
	case SCALER_STAGE_XBR2:
		centers = expand_damage(damage, stage->input_width,
		    stage->input_height, 2);
		multiplier = stage->kind == SCALER_STAGE_XBR4 ? 4 : 2;
		st_xbr_filter_region(source, stage->pixels,
		    (int)stage->input_width, (int)stage->input_height,
		    (int)multiplier, centers.x, centers.y, centers.width,
		    centers.height);
		break;
	case SCALER_STAGE_POINTY_ODD:
	case SCALER_STAGE_POINTY5:
	case SCALER_STAGE_POINTY3:
		centers = expand_damage(damage, stage->input_width,
		    stage->input_height, 1);
		multiplier = stage->x_multiplier;
		if (stage->kind == SCALER_STAGE_POINTY_ODD)
			st_pointy_scale_odd_region(source, stage->pixels,
			    (int)stage->input_width, (int)stage->input_height,
			    (int)multiplier, centers.x, centers.y, centers.width,
			    centers.height);
		else if (stage->kind == SCALER_STAGE_POINTY5)
			st_pointy_scale5_region(source, stage->pixels,
			    (int)stage->input_width, (int)stage->input_height,
			    centers.x, centers.y, centers.width, centers.height);
		else
			st_pointy_scale3_region(source, stage->pixels,
			    (int)stage->input_width, (int)stage->input_height,
			    centers.x, centers.y, centers.width, centers.height);
		break;
	}
	*output_damage = (struct st_pixel_rect){
		.x = centers.x * multiplier,
		.y = centers.y * multiplier,
		.width = centers.width * multiplier,
		.height = centers.height * multiplier,
	};
}

static void
update_blocky_region(st_pixel_scaler_t *scaler, const uint32_t *source,
	const struct st_pixel_rect *rectangle)
{
	uint32_t right = rectangle->x + rectangle->width;
	uint32_t bottom = rectangle->y + rectangle->height;
	uint32_t source_first = scaler->x_samples[rectangle->x].first;
	uint32_t source_end = scaler->x_samples[right - 1].second + 1;

	for (uint32_t y = rectangle->y; y < bottom; ++y) {
		const struct axis_sample *vertical = &scaler->y_samples[y];
		const uint32_t *top = &source[(size_t)vertical->first
		    * scaler->source_width];
		const uint32_t *bottom_row = &source[(size_t)vertical->second
		    * scaler->source_width];
		uint32_t *destination = &scaler->output.primary[(size_t)y
		    * scaler->width];

		if (vertical->first == vertical->second
		    || vertical->weight == 0)
			memcpy(&scaler->vertical_line[source_first],
			    &top[source_first],
			    (size_t)(source_end - source_first)
			    * sizeof(scaler->vertical_line[0]));
		else
			for (uint32_t x = source_first; x < source_end; ++x)
				scaler->vertical_line[x] = blend_pixel(top[x],
				    bottom_row[x], vertical->weight);
		if (scaler->exact_x_repeat != 0) {
			uint32_t output = source_first * scaler->exact_x_repeat;

			for (uint32_t x = source_first; x < source_end; ++x)
				for (uint32_t repeat = 0;
				    repeat < scaler->exact_x_repeat; ++repeat)
					destination[output++]
					    = scaler->vertical_line[x];
		}
		else
			for (uint32_t x = rectangle->x; x < right; ++x) {
				const struct axis_sample *horizontal
				    = &scaler->x_samples[x];

				destination[x] = blend_pixel(
				    scaler->vertical_line[horizontal->first],
				    scaler->vertical_line[horizontal->second],
				    horizontal->weight);
			}
	}
}

int
st_pixel_scaler_scale_region(st_pixel_scaler_t *scaler,
	const struct st_pixel_buffer **result, const uint32_t *source,
	uint32_t source_width, uint32_t source_height,
	const struct st_pixel_rect *damage, struct st_pixel_rect *output_damage)
{
	uint32_t right;
	uint32_t bottom;
	struct st_pixel_rect changed;

	if (scaler == NULL || result == NULL || source == NULL || damage == NULL
	    || output_damage == NULL || !scaler->valid
	    || source_width != scaler->source_width
	    || source_height != scaler->source_height || damage->width == 0
	    || damage->height == 0 || damage->x >= source_width
	    || damage->y >= source_height
	    || damage->width > source_width - damage->x
	    || damage->height > source_height - damage->y)
		return IO_ERROR_INVALID_ARGUMENT;
	if (scaler->policy != ST_PIXEL_SCALING_BLOCKY) {
		changed = *damage;
		for (size_t i = 0; i < scaler->stage_count; ++i) {
			const uint32_t *stage_source = i == 0 ? source
			    : scaler->stages[i - 1].pixels;
			struct st_pixel_rect stage_damage;

			execute_stage_region(&scaler->stages[i], stage_source,
			    &changed, &stage_damage);
			changed = stage_damage;
		}
		*result = &scaler->output;
		*output_damage = changed;
		return 0;
	}
	right = damage->x + damage->width;
	bottom = damage->y + damage->height;
	if (scaler->exact_x_repeat != 0 && scaler->exact_y_repeat != 0) {
		changed = (struct st_pixel_rect){
			.x = damage->x * scaler->exact_x_repeat,
			.y = damage->y * scaler->exact_y_repeat,
			.width = damage->width * scaler->exact_x_repeat,
			.height = damage->height * scaler->exact_y_repeat,
		};
		update_exact_blocky_damage(scaler, source, damage);
		*result = &scaler->output;
		*output_damage = changed;
		return 0;
	}
	damage_bounds(scaler->x_damage, damage->x, right, scaler->width,
	    &changed.x, &right);
	damage_bounds(scaler->y_damage, damage->y, bottom, scaler->height,
	    &changed.y, &bottom);
	changed.width = right - changed.x;
	changed.height = bottom - changed.y;
	update_blocky_region(scaler, source, &changed);
	*result = &scaler->output;
	*output_damage = changed;
	return 0;
}

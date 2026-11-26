/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "io_error.h"
#include "st_rip3_bezier.h"

#define CHECK(e)                                                        \
	do {                                                            \
		if (!(e)) {                                             \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e); \
			return 1;                                       \
		}                                                       \
	} while (0)

static int
samples(void)
{
	const struct st_rip3_point controls[] = {
	    {0, 0},
            {0, 8},
            {8, 8},
            {8, 0}
        };
	const struct st_rip3_point expected[] = {
	    {0, 0},
            {1, 4},
            {4, 6},
            {8, 0}
        };
	const struct st_rip3_point negative[] = {
	    {-8, -8},
            {-8, 0 },
            {0,  0 },
            {0,  -8}
        };
	const struct st_rip3_point negative_expected[] = {
	    {-8, -8},
            {-7, -4},
            {-4, -2},
            {0,  -8}
        };
	const struct st_rip3_point omitted[] = {
	    {0,  0},
            {0,  0},
            {0,  0},
            {64, 0}
        };
	struct st_rip3_point output[4095];
	CHECK(st_rip3_bezier_sample(output, controls, 4) == 0);
	CHECK(memcmp(output, expected, sizeof(expected)) == 0);
	CHECK(st_rip3_bezier_sample(output, negative, 4) == 0);
	CHECK(memcmp(output, negative_expected, sizeof(negative_expected)) == 0);
	CHECK(st_rip3_bezier_sample(output, omitted, 4) == 0);
	CHECK(output[0].x == 0 && output[1].x == 1 && output[2].x == 8 && output[3].x == 64);
	for (unsigned n = 0; n <= 2; n++) {
		CHECK(st_rip3_bezier_sample(output, controls, n) == 0);
		CHECK(output[0].x == 0 && output[1].x == 8 && output[1].y == 0);
	}
	const struct st_rip3_point wide[] = {
	    {65536, 32768},
            {0,     0    },
            {0,     0    },
            {65537, 65535}
        };
	CHECK(st_rip3_bezier_sample(output, wide, 2) == 0);
	CHECK(output[0].x == 65536 && output[0].y == 32768 && output[1].x == 1 && output[1].y == -1);
	/* Literal vectors from independently evaluated modular integer arithmetic;
	 * the 128/129 discontinuity exercises product overflow, not just rounding. */
	const struct st_rip3_point extreme[] = {
	    {-32768, 0},
            {32767,  0},
            {-32768, 0},
            {32767,  0}
        };

	static const struct {
		unsigned index;
		int      value;
	} gold[] = {
	    {1,    -32721},
	    {127,  -27041},
	    {128,  -26999},
	    {129,  27046 },
	    {1024, 32767 },
	    {2047, 32751 },
	    {2048, 32767 },
	    {4000, 31298 },
	    {4093, 32719 },
	    {4094, 32767 },
	};

	CHECK(st_rip3_bezier_sample(output, extreme, 4095) == 0);
	for (size_t i = 0; i < sizeof(gold) / sizeof(gold[0]); i++)
		CHECK(output[gold[i].index].x == gold[i].value);
	CHECK(st_rip3_bezier_sample(NULL, controls, 4) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_bezier_sample(output, NULL, 4) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_bezier_sample(output, controls, 4096) == IO_ERROR_OUT_OF_RANGE);
	return 0;
}

static int
records(void)
{
	struct st_rip3_bezier_record records[] = {
	    {1, {{0, 0}, {0, 8}, {8, 8}}},
	    {0, {{8, 0}, {0, 0}, {0, 0}}},
	};
	struct st_rip3_bezier_path path = {0};
	CHECK(st_rip3_bezier_expand(&path, records, 2, 4, false) == 0 && path.count == 5);
	const struct st_rip3_point expected[] = {
	    {0, 0},
            {1, 4},
            {4, 6},
            {8, 0},
            {8, 0}
        };
	CHECK(memcmp(path.points, expected, sizeof(expected)) == 0);
	st_rip3_bezier_free(&path);
	for (unsigned type = 0; type <= 6; type++) {
		records[0].type = type;
		bool point      = type == 0 || type == 3 || type == 4;
		CHECK(st_rip3_bezier_expand(&path, records, 2, 1, false) == 0 && path.count == 2);
		CHECK(path.points[0].x == 0 && path.points[1].x == 8);
		st_rip3_bezier_free(&path);
		CHECK(st_rip3_bezier_expand(&path, records, 2, 0, false) == (point ? 0 : IO_ERROR_OUT_OF_RANGE));
		if (point)
			st_rip3_bezier_free(&path);
	}
	records[0].type = 1;
	CHECK(st_rip3_bezier_expand(&path, records, 1, 4, false) == IO_ERROR_INVALID_FORMAT);
	CHECK(st_rip3_bezier_expand(&path, records, 1, 4, true) == 0 && path.count == 4);
	CHECK(path.points[3].x == 0 && path.points[3].y == 0);
	st_rip3_bezier_free(&path);
	/* P3 is zero-extended low16 before sampling; point records are low16
	 * too, while P0/P1/P2 retain their converted input integer. */
	records[1].points[0] = (struct st_rip3_point) {65537, 32768};
	CHECK(st_rip3_bezier_expand(&path, records, 2, 2, false) == 0 && path.count == 3);
	CHECK(path.points[1].x == 1 && path.points[1].y == -32768);
	CHECK(path.points[2].x == 1 && path.points[2].y == 32768);
	st_rip3_bezier_free(&path);
	/* Literal complete SHAPES record vector from the adopted grammar,
	 * with identity device conversion for this expansion/count fixture. */
	const struct st_rip3_bezier_record original[] = {
	    {2, {{308, 588}, {308, 532}, {388, 548}}},
	    {5, {{448, 548}, {508, 548}, {408, 588}}},
	    {5, {{488, 588}, {528, 588}, {508, 608}}},
	    {5, {{488, 628}, {468, 648}, {428, 648}}},
	    {6, {{388, 648}, {428, 608}, {388, 568}}},
	    {0, {{348, 608}, {0, 0}, {0, 0}}        },
	    {0, {{348, 648}, {0, 0}, {0, 0}}        },
	    {0, {{308, 648}, {0, 0}, {0, 0}}        },
	};
	CHECK(st_rip3_bezier_expand(&path, original, 8, 40, true) == 0 && path.count == 203);
	CHECK(path.points[39].x == 448 && path.points[40].x == 448 && path.points[39].y == 548
	      && path.points[40].y == 548);
	CHECK(path.points[200].x == 348 && path.points[200].y == 608 && path.points[202].x == 308
	      && path.points[202].y == 648);
	st_rip3_bezier_free(&path);
	const struct st_rip3_bezier_record open[] = {
	    {2, {{694, 218}, {622, 218}, {698, 366}}},
	    {6, {{642, 366}, {700, 366}, {624, 518}}},
	    {0, {{696, 520}, {0, 0}, {0, 0}}        },
	};
	CHECK(st_rip3_bezier_expand(&path, open, 3, 40, false) == 0 && path.count == 81);
	CHECK(path.points[79].x == 696 && path.points[80].x == 696 && path.points[80].y == 520);
	st_rip3_bezier_free(&path);
	return 0;
}

static int
budgets(void)
{
	struct st_rip3_bezier_record *records = calloc(2731, sizeof(*records));
	struct st_rip3_bezier_path    path    = {0}, sentinel;
	CHECK(records != NULL);
	sentinel = (struct st_rip3_bezier_path) {records[0].points, 99};
	CHECK(st_rip3_bezier_expand(&path, records, 2730, 0, true) == 0 && path.count == 2730);
	st_rip3_bezier_free(&path);
	CHECK(st_rip3_bezier_expand(&sentinel, records, 2731, 0, true) == IO_ERROR_OUT_OF_RANGE);
	CHECK(sentinel.points == records[0].points && sentinel.count == 99);
	records[0].type = 1;
	CHECK(st_rip3_bezier_expand(&path, records, 2729, 4095, true) == 0 && path.count == 4094);
	st_rip3_bezier_free(&path);
	CHECK(st_rip3_bezier_expand(&path, records, 2730, 4095, true) == IO_ERROR_OUT_OF_RANGE);
	for (size_t i = 0; i < 1171; i++)
		records[i].type = 1;
	CHECK(st_rip3_bezier_expand(&path, records, 1170, 40, true) == 0 && path.count == 3510);
	st_rip3_bezier_free(&path);
	CHECK(st_rip3_bezier_expand(&path, records, 1171, 40, true) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_bezier_expand(&path, records, 1, 4095, true) == 0 && path.count == 4094);
	st_rip3_bezier_free(&path);
	records[1].type = 0;
	CHECK(st_rip3_bezier_expand(&path, records, 2, 4095, true) == 0 && path.count == 4094);
	st_rip3_bezier_free(&path);
	records[1].type = 2;
	records[2].type = 0;
	CHECK(st_rip3_bezier_expand(&path, records, 3, 2047, true) == 0 && path.count == 4093);
	st_rip3_bezier_free(&path);
	records[0].type = 7;
	CHECK(st_rip3_bezier_expand(&sentinel, records, 1, 40, true) == IO_ERROR_INVALID_FORMAT);
	CHECK(st_rip3_bezier_expand(&path, records, 0, 40, true) == IO_ERROR_INVALID_FORMAT);
	CHECK(st_rip3_bezier_expand(&path, records, SIZE_MAX, 40, true) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_bezier_expand(&path, records, 1, 4096, true) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_bezier_expand(NULL, records, 1, 40, true) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(sentinel.points == records[0].points && sentinel.count == 99);
	free(records);
	return 0;
}

int
main(void)
{
	CHECK(samples() == 0);
	CHECK(records() == 0);
	CHECK(budgets() == 0);
	return 0;
}

/* X15 emits separate anchored polylines, retaining their original endpoints. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_igs.h"
#include "io_error.h"

#include <string.h>

void
cterm_igs_sweep_cancel(struct cterm_igs *igs)
{
	memset(&igs->sweep,0,sizeof(igs->sweep));
}

static int
draw(struct cterm_igs *igs)
{
	struct cterm_igs_sweep *s = &igs->sweep;
	while (s->increasing ? s->scalar < s->bound : s->scalar > s->bound) {
		/* The exclusive bound guarantees that each emitted scalar is still
		 * a signed protocol word. The next scalar is deliberately wider:
		 * crossing the bound must stop, never wrap into a repeating sweep. */
		s->points[2+s->axis] = (int)s->scalar;
		igs->draw_x = s->points[2]; igs->draw_y = s->points[3];
		int status = cterm_igs_polyline(igs,s->points,2);
		if (status == 0 && igs->reset_pending) status = IO_ERROR_INTERRUPTED;
		if (status == 0 && s->wait != 0) status = cterm_igs_wait_sync(igs,s->wait);
		if (status == IO_ERROR_IN_PROGRESS) return status;
		if (status != 0) { cterm_igs_sweep_cancel(igs); return status; }
		s->scalar += s->increasing ? s->step : -s->step;
	}
	cterm_igs_sweep_cancel(igs);
	return 0;
}

int
cterm_igs_sweep_resume(struct cterm_igs *igs)
{
	struct cterm_igs_sweep *s = &igs->sweep;
	if (!s->active) return IO_ERROR_INVALID_ARGUMENT;
	s->scalar += s->increasing ? s->step : -s->step;
	return draw(igs);
}

int
cterm_igs_sweep_begin(struct cterm_igs *igs, const int values[9])
{
	cterm_igs_sweep_cancel(igs);
	struct cterm_igs_sweep *s = &igs->sweep;
	memcpy(s->points,values,sizeof(s->points));
	igs->draw_start_x = values[0]; igs->draw_start_y = values[1];
	igs->draw_x = values[2]; igs->draw_y = values[3];
	s->axis = values[4];
	if (s->axis != 0 && s->axis != 1) { cterm_igs_sweep_cancel(igs); return 0; }
	s->bound = values[2+s->axis];
	s->scalar = values[5];
	s->increasing = values[6] != 0;
	s->step = values[7] < 1 ? 1 : values[7];
	s->wait = values[8] < 0 ? 0u : values[8] > 60 ? 60u : (unsigned)values[8];
	s->active = true;
	return draw(igs);
}

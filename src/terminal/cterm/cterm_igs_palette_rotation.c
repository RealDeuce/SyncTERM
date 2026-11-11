/* X8 palette rotation with palette queries before local waits and writes after. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_igs.h"
#include "io_error.h"

#include <stdint.h>
#include <string.h>

void
cterm_igs_palette_rotation_cancel(struct cterm_igs *igs)
{
	igs->palette_rotation.active = igs->palette_rotation.key = false;
	igs->palette_rotation.delaying = igs->palette_rotation.final = false;
	igs->palette_rotation.remaining = 0;
}

static void
query(struct cterm_igs *igs, int index, int result[3])
{
	struct cterm_igs_palette_rotation *c = &igs->palette_rotation;
	if (c->raw) {
		result[0] = igs->raw_palette[(unsigned)index&15u]&0x777;
		result[1] = result[2] = 0;
		return;
	}
	/* Invalid native queries leave the RGB output untouched. Preserve the
	 * preceding valid query in this command, without foreign shared scratch
	 * memory or a fabricated modulo/clamped pen. */
	if (index >= 0 && (unsigned)index <= cterm_igs_plane_mask(igs->resolution))
		memcpy(c->query,igs->requested_palette[index],sizeof(c->query));
	memcpy(result,c->query,sizeof(c->query));
}

static int
write_color(struct cterm_igs *igs, int index, const int value[3])
{
	return igs->palette_rotation.raw ? cterm_igs_palette_register(igs,index,value[0])
	    : cterm_igs_palette_pen(igs,index,value[0],value[1],value[2]);
}

static int
wait_write(struct cterm_igs *igs)
{
	int status = cterm_igs_wait_sync(igs,1);
	if (status != IO_ERROR_IN_PROGRESS) cterm_igs_palette_rotation_cancel(igs);
	return status;
}

static int
begin_pass(struct cterm_igs *igs)
{
	struct cterm_igs_palette_rotation *c = &igs->palette_rotation;
	c->delaying = c->final = false;
	c->index = c->last;
	query(igs,c->last,c->saved);
	query(igs,c->index+c->step,c->held);
	return wait_write(igs);
}

static int
end_pass(struct cterm_igs *igs)
{
	struct cterm_igs_palette_rotation *c = &igs->palette_rotation;
	if (c->key || --c->remaining == 0) {
		cterm_igs_palette_rotation_cancel(igs);
		return 0;
	}
	return begin_pass(igs);
}

int
cterm_igs_palette_rotation_resume(struct cterm_igs *igs)
{
	struct cterm_igs_palette_rotation *c = &igs->palette_rotation;
	if (!c->active) return IO_ERROR_INVALID_ARGUMENT;
	if (c->delaying) return end_pass(igs);
	int status = write_color(igs,c->index,c->held);
	if (status != 0) return status;
	if (igs->reset_pending) return IO_ERROR_INTERRUPTED;
	if (c->final) {
		if (c->delay == 0 || c->key) return end_pass(igs);
		uint64_t now, duration = (uint64_t)c->delay*5;
		status = igs->now_ms(&now);
		if (status != 0) return status;
		if (now > UINT64_MAX-duration) return IO_ERROR_OUT_OF_RANGE;
		igs->wait_deadline = now+duration;
		c->delaying = true;
		return IO_ERROR_IN_PROGRESS;
	}
	if (c->index == c->first) {
		c->final = true;
		memcpy(c->held,c->saved,sizeof(c->held));
	}
	else {
		c->index += c->step;
		query(igs,c->index+c->step,c->held);
	}
	return wait_write(igs);
}

static int
subtract_encoding(int value)
{
	uint16_t word = (uint16_t)(value-15);
	return word > 32767 ? (int)word-65536 : word;
}

int
cterm_igs_palette_rotation_begin(struct cterm_igs *igs, int first, int last, int times, int delay)
{
	struct cterm_igs_palette_rotation *c = &igs->palette_rotation;
	cterm_igs_palette_rotation_cancel(igs);
	memset(c->query,0,sizeof(c->query));
	c->raw = first > 15 || last > 15;
	if (c->raw) {
		c->restore_raw = true;
		first = subtract_encoding(first); last = subtract_encoding(last);
	}
	if (first == 1 && last == 1 && times == 1 && delay == 1) {
		/* Capture both namespaces, even when this command selected raw. */
		bool raw = c->raw; c->raw = false;
		for (int i = 0; i < 16; ++i) {
			query(igs,i,c->snapshot[i]);
			c->registers[i] = igs->raw_palette[i]&0x777;
		}
		c->raw = raw;
	}
	if (times == 0) {
		for (int i = 0; i < 16; ++i) {
			int status = c->restore_raw ? cterm_igs_palette_register(igs,i,c->registers[i])
			    : cterm_igs_palette_pen(igs,i,c->snapshot[i][0],c->snapshot[i][1],c->snapshot[i][2]);
			if (status != 0) return status;
			if (igs->reset_pending) return IO_ERROR_INTERRUPTED;
		}
		c->restore_raw = false;
		return 0;
	}
	first = first < 0 || first > 15 ? 15 : first;
	last = last < 0 || last > 15 ? 15 : last;
	if (times < 0 || first == last) return 0;
	c->first = first; c->last = last;
	c->step = first < last ? -1 : 1;
	/* Count in the host domain: retain n+1 passes but fix signed-word
	 * counter wrap at 32767 and key-abort wrap at 32766/32767. */
	c->remaining = (unsigned)times+1;
	c->delay = (uint16_t)delay;
	c->active = true;
	return begin_pass(igs);
}

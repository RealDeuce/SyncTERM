/* Stable-RAM GIST voice API/envelopes; reference chapters 06, 56 and 57. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_igs_gist.h"

#include <limits.h>
#include <string.h>

#include "st_igs_gist_data.h"

static int
signed_word(uint16_t value)
{
	return value < 32768 ? value : (int)value-65536;
}

static int32_t
signed_long(uint32_t value)
{
	return value <= INT32_MAX ? (int32_t)value : -(int32_t)~value-1;
}

static uint32_t
parameter(const struct st_igs_gist_voice *v, unsigned offset)
{
	return (uint32_t)v->parameters[offset/2]<<16 | v->parameters[offset/2+1];
}

static void
set_parameter(struct st_igs_gist_voice *v, unsigned offset, uint32_t value)
{
	v->parameters[offset/2] = (uint16_t)(value>>16);
	v->parameters[offset/2+1] = (uint16_t)value;
}

static void
write_register(struct st_igs_gist *g, unsigned reg, unsigned value)
{
	static const uint8_t masks[16] = {255,15,255,15,255,15,31,255,31,31,31,255,255,15,255,255};
	uint8_t byte = (uint8_t)value;
	g->registers[reg] = byte&masks[reg];
	if (g->write != NULL) g->write(g->context,reg,byte);
}

void
st_igs_gist_init(struct st_igs_gist *g)
{
	*g = (struct st_igs_gist){0};
	g->registers[7] = 0x3f;
}

bool
st_igs_gist_active(const struct st_igs_gist *g)
{
	return g->voice[0].parameters[0] != 0 || g->voice[1].parameters[0] != 0 || g->voice[2].parameters[0] != 0;
}

void
st_igs_gist_stop(struct st_igs_gist *g, int voice)
{
	if (voice < 0 || voice >= ST_IGS_GIST_VOICES) return;
	g->voice[voice].priority = 0;
	g->voice[voice].parameters[0] = 0;
	write_register(g,8+(unsigned)voice,0);
}

void
st_igs_gist_release(struct st_igs_gist *g, int voice)
{
	if (voice < 0 || voice >= ST_IGS_GIST_VOICES || g->voice[voice].parameters[0] == 0) return;
	g->voice[voice].parameters[0] = 1;
	g->voice[voice].control = -1;
}

int
st_igs_gist_on(struct st_igs_gist *g, const uint16_t patch[ST_IGS_GIST_WORDS],
    int voice, int volume, int pitch, int priority)
{
	voice = signed_word((uint16_t)voice); volume = signed_word((uint16_t)volume);
	pitch = signed_word((uint16_t)pitch); priority = signed_word((uint16_t)priority);
	if (voice < 0 || voice >= ST_IGS_GIST_VOICES) {
		for (voice = 0; voice < ST_IGS_GIST_VOICES; ++voice)
			if (g->voice[voice].parameters[0] == 0) break;
		if (voice == ST_IGS_GIST_VOICES) {
			voice = g->voice[0].priority < g->voice[1].priority ? 0 : 1;
			if (g->voice[voice].priority >= g->voice[2].priority) voice = 2;
		}
	}
	struct st_igs_gist_voice *v = &g->voice[voice];
	if (v->priority > priority) return -1;
	st_igs_gist_stop(g,voice);
	uint16_t duration = patch[0];
	if (duration == 0) return voice;
	/* Keep stop-before-read and forward-copy semantics for valid aliases. */
	for (unsigned i = 1; i < ST_IGS_GIST_WORDS; ++i) v->parameters[i] = patch[i];
	v->control = (int16_t)pitch; v->priority = (int16_t)priority;
	memset(v->accumulator,0,sizeof(v->accumulator));
	unsigned disabled = 0;
	if (signed_word(v->parameters[1]) >= 0) {
		if (pitch >= 0) {
			/* Fold directly; do not reproduce foreign CPU work or delays. */
			if (pitch > 108) pitch = 97+(pitch-109)%12;
			if (pitch < 24) pitch = 24+pitch%12;
			v->parameters[1] = gist_pitch[pitch-24];
		}
		write_register(g,2*(unsigned)voice,v->parameters[1]);
		write_register(g,2*(unsigned)voice+1,v->parameters[1]>>8);
	}
	else {
		disabled |= 1u<<(unsigned)voice;
		v->parameters[18] = 0; set_parameter(v,0x3a,0);
	}
	if (signed_word(v->parameters[2]) >= 0) write_register(g,6,v->parameters[2]);
	else {
		disabled |= 8u<<(unsigned)voice;
		v->parameters[40] = 0; set_parameter(v,0x66,0);
	}
	static const unsigned masks[3] = {0xf6,0xed,0xdb};
	write_register(g,7,disabled | (g->registers[7]&masks[voice]));
	if (volume >= 0) v->parameters[3] = (uint16_t)volume;
	if (v->parameters[4] == 0) {
		v->accumulator[0] = 0xf0000;
		if (signed_word(v->parameters[3]) >= 0) write_register(g,8+(unsigned)voice,v->parameters[3]);
	}
	v->parameters[0] = duration;
	return voice;
}

static void
amplitude(struct st_igs_gist_voice *v)
{
	unsigned phase = v->parameters[4]&255;
	uint32_t value = v->accumulator[0];
	if (phase == 1) {
		value += parameter(v,0x0a);
		if (signed_long(value) >= 0xf0000) { value = 0xf0000; ++v->parameters[4]; }
	}
	else if (phase == 2) {
		value += parameter(v,0x0e);
		if (signed_long(value) <= signed_long(parameter(v,0x12))) { value = parameter(v,0x12); ++v->parameters[4]; }
	}
	else if (phase == 4) {
		value += parameter(v,0x16);
		if (signed_long(value) <= 0) { value = 0; v->parameters[4] = 0; v->parameters[0] = 1; }
	}
	else return;
	v->accumulator[0] = value;
}

static void
pitch_envelope(struct st_igs_gist_voice *v, unsigned phase_offset, unsigned acc,
    unsigned attack, unsigned attack_limit, unsigned decay, unsigned decay_limit, unsigned release)
{
	uint16_t *phase = &v->parameters[phase_offset/2];
	unsigned state = *phase&255;
	uint32_t value = v->accumulator[acc];
	if (state == 1 || state == 2) {
		unsigned delta = state == 1 ? attack : decay, limit = state == 1 ? attack_limit : decay_limit;
		uint32_t step = parameter(v,delta), bound = parameter(v,limit);
		value += step;
		bool done = signed_long(step) < 0 ? signed_long(value) <= signed_long(bound) : signed_long(value) >= signed_long(bound);
		if (done) { value = bound; ++*phase; }
	}
	else if (state == 4) {
		uint32_t step = parameter(v,release);
		value += step;
		if (signed_long(step) < 0 ? signed_long(value) <= 0 : signed_long(value) >= 0) value = 0;
	}
	else return;
	v->accumulator[acc] = value;
}

static void
symmetric_modulation(struct st_igs_gist_voice *v, unsigned limit, unsigned delay, unsigned delta, unsigned acc)
{
	uint32_t bound = parameter(v,limit);
	if (bound == 0) return;
	if (v->parameters[delay/2] != 0) { --v->parameters[delay/2]; return; }
	uint32_t value = v->accumulator[acc]+parameter(v,delta);
	bool clamp = signed_long(value) >= signed_long(bound);
	if (!clamp) { bound = 0u-bound; clamp = signed_long(value) <= signed_long(bound); }
	if (clamp) { value = bound; set_parameter(v,delta,0u-parameter(v,delta)); }
	v->accumulator[acc] = value;
}

static void
tone_modulation(struct st_igs_gist_voice *v)
{
	uint32_t bound = parameter(v,0x3a);
	if (bound == 0) return;
	if (v->parameters[39] != 0) { --v->parameters[39]; return; }
	uint32_t delta = parameter(v,0x3e);
	bool negative = signed_long(delta) < 0;
	if (negative) bound = parameter(v,0x46);
	uint64_t sum = (uint64_t)v->accumulator[3]+delta;
	uint32_t value = (uint32_t)sum;
	bool carry = sum > UINT32_MAX;
	if (carry != negative) set_parameter(v,0x3e,parameter(v,negative ? 0x4a : 0x42));
	bool clamp = negative ? signed_long(value) <= signed_long(bound) : signed_long(value) >= signed_long(bound);
	if (clamp) { value = bound; set_parameter(v,0x3e,0u-parameter(v,0x3e)); }
	v->accumulator[3] = value;
}

static void
gain(struct st_igs_gist *g, struct st_igs_gist_voice *v, unsigned index)
{
	int volume = signed_word(v->parameters[3]);
	/* Edited native factor indices escape the table. Keep the stored word,
	 * but bound its use rather than reading unrelated host memory. */
	if (volume < 0) volume = 0;
	if (volume > 15) volume = 15;
	uint32_t total = v->accumulator[0]+v->accumulator[1];
	int value = 0;
	if (signed_long(total) >= 0) {
		int32_t product = (int32_t)signed_word((uint16_t)(total>>8))*gist_factors[volume];
		value = signed_word((uint16_t)((uint32_t)product>>16));
		if (value > 15) value = 15;
	}
	write_register(g,8+index,(unsigned)value);
}

static void
tone(struct st_igs_gist *g, struct st_igs_gist_voice *v, unsigned index)
{
	uint32_t sum = v->accumulator[2]+v->accumulator[3];
	int32_t product = (int32_t)signed_word((uint16_t)(sum>>16))*signed_word(v->parameters[1]);
	uint32_t shifted = (uint32_t)product<<4;
	uint16_t word = (uint16_t)(shifted>>16);
	if ((shifted&0x8000) != 0) ++word;
	word = (uint16_t)(word+v->parameters[1]);
	int value = signed_word(word);
	if (value < 0) value = 0;
	if (value > 4095) value = 4095;
	write_register(g,2*index,(unsigned)value);
	write_register(g,2*index+1,(unsigned)value>>8);
}

static void
noise(struct st_igs_gist *g, struct st_igs_gist_voice *v)
{
	uint16_t word = (uint16_t)((v->accumulator[4]+v->accumulator[5])>>16);
	word = (uint16_t)(word+v->parameters[2]);
	unsigned value = word&255;
	if (signed_word(word) < 0) value = 0;
	else if (value >= 32 && value < 128) value = 31;
	write_register(g,6,value);
}

static void
duration(struct st_igs_gist *g, struct st_igs_gist_voice *v, unsigned index)
{
	if (v->control >= 0 || --v->parameters[0] != 0) return;
	v->priority = 0;
	if (v->parameters[4] == 0) { write_register(g,8+index,0); return; }
	--v->parameters[0]; v->parameters[4] = 4;
	static const unsigned phases[2] = {18,40}, deltas[2] = {0x36,0x62}, accumulators[2] = {2,4};
	for (unsigned i = 0; i < 2; ++i) {
		if (v->parameters[phases[i]] == 0) continue;
		v->parameters[phases[i]] = 4;
		uint32_t delta = parameter(v,deltas[i]);
		if (((delta^v->accumulator[accumulators[i]])&0x80000000) == 0) set_parameter(v,deltas[i],0u-delta);
	}
}

void
st_igs_gist_update(struct st_igs_gist *g)
{
	for (unsigned remaining = ST_IGS_GIST_VOICES; remaining != 0; --remaining) {
		unsigned index = remaining-1;
		struct st_igs_gist_voice *v = &g->voice[index];
		if (v->parameters[0] == 0) continue;
		amplitude(v); symmetric_modulation(v,0x1a,0x22,0x1e,1);
		if ((v->parameters[4]|v->parameters[13]) != 0) gain(g,v,index);
		pitch_envelope(v,0x24,2,0x26,0x2a,0x2e,0x32,0x36); tone_modulation(v);
		if ((v->parameters[18]|v->parameters[29]) != 0) tone(g,v,index);
		pitch_envelope(v,0x50,4,0x52,0x56,0x5a,0x5e,0x62); symmetric_modulation(v,0x66,0x6e,0x6a,5);
		if ((v->parameters[40]|v->parameters[51]) != 0) noise(g,v);
		duration(g,v,index);
	}
}

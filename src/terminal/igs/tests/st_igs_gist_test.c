/* GIST scalar/record API tests; no physical audio, clock or PCM oracle. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_igs_gist.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
struct write { unsigned reg, value; };
struct capture { struct write writes[64]; size_t count; };
struct gist_case { unsigned index; uint16_t before[70], after[70]; size_t count; struct write writes[5]; };
struct gist_on_case {
	uint16_t before[3][70], patch[56], arguments[4];
	int result;
	uint16_t after[3][70];
	uint8_t registers[16], expected_registers[16];
	size_t count;
	struct write writes[6];
};
#include "gist_vectors.h"
_Static_assert(sizeof(gist_cases)/sizeof(*gist_cases) == 1044,"complete independent record matrix");

static void
observe(void *context, unsigned reg, uint8_t value)
{
	struct capture *c = context; CHECK(c->count < sizeof(c->writes)/sizeof(*c->writes));
	c->writes[c->count++] = (struct write){reg,value};
}

static void
attach(struct st_igs_gist *g, struct capture *c)
{
	st_igs_gist_init(g); memset(c,0,sizeof(*c)); g->context = c; g->write = observe;
}

static void
unpack(struct st_igs_gist_voice *v, const uint16_t words[70])
{
	memcpy(v->parameters,words,sizeof(v->parameters));
	v->control = (int16_t)(words[56] > 32767 ? (int)words[56]-65536 : words[56]);
	v->priority = (int16_t)(words[57] > 32767 ? (int)words[57]-65536 : words[57]);
	for (unsigned i = 0; i < 6; ++i) v->accumulator[i] = (uint32_t)words[58+2*i]<<16 | words[59+2*i];
}

static void
check_voice(const struct st_igs_gist_voice *v, const uint16_t words[70])
{
	CHECK(memcmp(v->parameters,words,sizeof(v->parameters)) == 0);
	CHECK((uint16_t)v->control == words[56] && (uint16_t)v->priority == words[57]);
	for (unsigned i = 0; i < 6; ++i) CHECK(v->accumulator[i] == ((uint32_t)words[58+2*i]<<16 | words[59+2*i]));
}

static void
vectors(void)
{
	static const unsigned masks[16] = {255,15,255,15,255,15,31,255,31,31,31,255,255,15,255,255};
	for (size_t i = 0; i < sizeof(gist_cases)/sizeof(*gist_cases); ++i) {
		const struct gist_case *v = &gist_cases[i]; struct st_igs_gist g; struct capture c; attach(&g,&c);
		unpack(&g.voice[v->index],v->before); uint8_t registers[16]; memcpy(registers,g.registers,sizeof(registers));
		st_igs_gist_update(&g);
		if (c.count != v->count) fprintf(stderr,"record %zu count %zu != %zu\n",i,c.count,v->count);
		CHECK(c.count == v->count); check_voice(&g.voice[v->index],v->after);
		for (size_t w = 0; w < c.count; ++w) {
			CHECK(c.writes[w].reg == v->writes[w].reg && c.writes[w].value == v->writes[w].value);
			registers[c.writes[w].reg] = (uint8_t)(c.writes[w].value&masks[c.writes[w].reg]);
		}
		CHECK(memcmp(registers,g.registers,sizeof(registers)) == 0);
		for (unsigned j = 0; j < 3; ++j) if (j != v->index) check_voice(&g.voice[j],(uint16_t[70]){0});
	}
}

static void
allocation_and_stops(void)
{
	struct st_igs_gist g; struct capture c; attach(&g,&c);
	uint16_t patch[56] = {100,100,65535,15,0};
	for (unsigned i = 0; i < 3; ++i) {
		c.count = 0; CHECK(st_igs_gist_on(&g,patch,-1,-1,-1,1) == (int)i);
		CHECK(c.writes[0].reg == 8+i && c.writes[0].value == 0);
		CHECK(g.voice[i].parameters[0] == 100 && g.voice[i].parameters[1] == 100 && g.voice[i].accumulator[0] == 0xf0000);
		CHECK(g.voice[i].parameters[40] == 0 && g.voice[i].control == -1 && g.voice[i].priority == 1);
		CHECK(g.registers[8+i] == 15);
	}
	CHECK(st_igs_gist_active(&g)); c.count = 0;
	CHECK(st_igs_gist_on(&g,patch,-1,-1,-1,1) == 2); /* Equal ties prefer C. */
	CHECK(c.writes[0].reg == 10);
	g.voice[0].priority = 7; g.voice[1].priority = 6; g.voice[2].priority = 5;
	struct st_igs_gist saved = g; c.count = 0;
	CHECK(st_igs_gist_on(&g,patch,-1,-1,-1,1) == -1 && c.count == 0);
	CHECK(memcmp(saved.voice,g.voice,sizeof(g.voice)) == 0);
	CHECK(st_igs_gist_on(&g,patch,1,3,60,7) == 1 && g.voice[1].control == 60 && g.voice[1].parameters[3] == 3);
	CHECK(g.voice[1].parameters[1] == 478);
	c.count = 0; st_igs_gist_release(&g,1);
	CHECK(c.count == 0 && g.voice[1].control == -1 && g.voice[1].parameters[0] == 1);
	st_igs_gist_update(&g); CHECK(g.voice[1].parameters[0] == 0 && g.registers[9] == 0);
	c.count = 0; st_igs_gist_release(&g,1); CHECK(c.count == 0);
	for (int i = 0; i < 3; ++i) st_igs_gist_stop(&g,i);
	CHECK(c.count == 3 && c.writes[0].reg == 8 && c.writes[1].reg == 9 && c.writes[2].reg == 10);
	CHECK(!st_igs_gist_active(&g)); c.count = 0;
	st_igs_gist_stop(&g,-1); st_igs_gist_stop(&g,3); st_igs_gist_release(&g,-1); st_igs_gist_release(&g,3); CHECK(c.count == 0);
	/* The guard also applies to an inconsistent free record. */
	g.voice[0].priority = 5; CHECK(st_igs_gist_on(&g,patch,-1,-1,-1,1) == -1 && c.count == 0);
	/* A duration-zero source leaves the accepted record stopped. */
	g.voice[0].priority = 0; patch[0] = 0;
	CHECK(st_igs_gist_on(&g,patch,0,-1,-1,1) == 0 && c.count == 1 && !st_igs_gist_active(&g));
}

static void
api_vectors(void)
{
	CHECK(sizeof(gist_on_cases)/sizeof(*gist_on_cases) == 160);
	for (size_t i = 0; i < sizeof(gist_on_cases)/sizeof(*gist_on_cases); ++i) {
		const struct gist_on_case *v = &gist_on_cases[i];
		struct st_igs_gist g; struct capture c; attach(&g,&c);
		for (unsigned j = 0; j < 3; ++j) unpack(&g.voice[j],v->before[j]);
		memcpy(g.registers,v->registers,sizeof(g.registers));
		int result = st_igs_gist_on(&g,v->patch,v->arguments[0],v->arguments[1],v->arguments[2],v->arguments[3]);
		CHECK(result == v->result && c.count == v->count);
		for (unsigned j = 0; j < 3; ++j) check_voice(&g.voice[j],v->after[j]);
		CHECK(memcmp(g.registers,v->expected_registers,sizeof(g.registers)) == 0);
		for (size_t j = 0; j < c.count; ++j)
			CHECK(c.writes[j].reg == v->writes[j].reg && c.writes[j].value == v->writes[j].value);
	}
}

static void
pitch_and_copy(void)
{
	struct st_igs_gist g; struct capture c; attach(&g,&c);
	uint16_t patch[56] = {10,100,65535,15,0};
	for (int pitch = 0; pitch <= 32767; ++pitch) {
		int folded = pitch; while (folded > 108) folded -= 12; while (folded < 24) folded += 12;
		c.count = 0; CHECK(st_igs_gist_on(&g,patch,0,-1,pitch,7) == 0);
		CHECK(g.voice[0].parameters[1] == pitch_reference[folded-24] && g.voice[0].control == pitch);
	}
	for (int pitch = -32768; pitch < 0; ++pitch) {
		c.count = 0; CHECK(st_igs_gist_on(&g,patch,0,-1,pitch,7) == 0);
		CHECK(g.voice[0].parameters[1] == 100 && g.voice[0].control == pitch);
	}
	patch[1] = 65535; patch[18] = 1; patch[29] = 1; patch[40] = 1; patch[51] = 1;
	c.count = 0; CHECK(st_igs_gist_on(&g,patch,0,-1,60,7) == 0);
	CHECK(g.voice[0].parameters[1] == 65535 && g.voice[0].parameters[18] == 0 && g.voice[0].parameters[29] == 0);
	CHECK(g.voice[0].parameters[40] == 0 && g.voice[0].parameters[51] == 0 && (g.registers[7]&9) == 9);
	CHECK(c.count == 3 && c.writes[0].reg == 8 && c.writes[1].reg == 7 && c.writes[2].reg == 8);
	/* Source edits/restores do not alter the already copied voice. */
	patch[1] = 123; c.count = 0; CHECK(st_igs_gist_on(&g,patch,0,-1,-1,7) == 0);
	patch[1] = 321; CHECK(g.voice[0].parameters[1] == 123);
	/* Valid source alias: stopping precedes reading duration. */
	c.count = 0; CHECK(st_igs_gist_on(&g,g.voice[0].parameters,0,-1,-1,7) == 0);
	CHECK(c.count == 1 && g.voice[0].parameters[0] == 0);
}

static void
envelopes_and_order(void)
{
	struct st_igs_gist g; struct capture c; attach(&g,&c);
	uint16_t patch[56] = {2,100,65535,15,1,0,32768,65535,32768,0,32768,65535,32768};
	CHECK(st_igs_gist_on(&g,patch,0,-1,-1,1) == 0);
	static const unsigned gain[] = {0,1};
	for (unsigned i = 0; i < sizeof(gain)/sizeof(*gain); ++i) {
		c.count = 0; st_igs_gist_update(&g); CHECK(g.registers[8] == gain[i]);
	}
	/* Expiry requests release; its volume does not immediately disappear. */
	CHECK(g.voice[0].parameters[4] == 4 && g.voice[0].parameters[0] != 0);
	for (unsigned i = 0; i < 8 && st_igs_gist_active(&g); ++i) { c.count = 0; st_igs_gist_update(&g); }
	CHECK(!st_igs_gist_active(&g) && g.registers[8] == 0);
	attach(&g,&c);
	for (unsigned i = 0; i < 3; ++i) {
		g.voice[i].parameters[0] = 1; g.voice[i].parameters[2] = (uint16_t)(4+2*i);
		g.voice[i].parameters[40] = 3; g.voice[i].control = 1;
	}
	st_igs_gist_update(&g);
	CHECK(c.count == 3 && c.writes[0].reg == 6 && c.writes[0].value == 8
	    && c.writes[1].value == 6 && c.writes[2].value == 4 && g.registers[6] == 4);
	/* Gain conversion preserves a raw hardware-envelope enable; this is
	 * distinct from the safe bound on the factor-table index. */
	attach(&g,&c); g.voice[0].parameters[0] = 10; g.voice[0].parameters[3] = 15;
	g.voice[0].parameters[4] = 0x100; g.voice[0].accumulator[0] = 0x00f00000; g.voice[0].control = 1;
	st_igs_gist_update(&g); CHECK(c.count == 1 && c.writes[0].value == 0xf0 && g.registers[8] == 16);
	g.voice[0].parameters[3] = 65535; c.count = 0; st_igs_gist_update(&g); CHECK(g.registers[8] == 0);
	g.voice[0].parameters[3] = 32767; c.count = 0; st_igs_gist_update(&g); CHECK(g.registers[8] == 16);
}

static void
original_patches(void)
{
	uint64_t hash = UINT64_C(14695981039346656037);
	for (unsigned p = 0; p < ST_IGS_GIST_PATCHES; ++p)
		for (unsigned w = 0; w < ST_IGS_GIST_WORDS; ++w) {
			uint16_t value = st_igs_gist_patches[p][w];
			hash = (hash^(value>>8))*UINT64_C(1099511628211); hash = (hash^(value&255))*UINT64_C(1099511628211);
		}
	CHECK(hash == UINT64_C(0xc364bd23f61802f0));
	CHECK(memcmp(st_igs_gist_patches[3],st_igs_gist_patches[11],112) == 0
	    && st_igs_gist_patches[3] != st_igs_gist_patches[11]);
	/* The real patches exercise complete attacks, automatic expiry and
	 * release. Bounded observation is a test limit, not a driver timeout. */
	for (unsigned p = 0; p < ST_IGS_GIST_PATCHES; ++p) {
		struct st_igs_gist g; st_igs_gist_init(&g);
		CHECK(st_igs_gist_on(&g,st_igs_gist_patches[p],-1,-1,-1,1) == 0);
		for (unsigned i = 0; i < 20000 && st_igs_gist_active(&g); ++i) st_igs_gist_update(&g);
		if (st_igs_gist_active(&g)) fprintf(stderr,"patch %u still active\n",p);
		CHECK(!st_igs_gist_active(&g));
	}
}

int
main(void)
{
	vectors(); api_vectors(); allocation_and_stops(); pitch_and_copy(); envelopes_and_order(); original_patches(); return 0;
}

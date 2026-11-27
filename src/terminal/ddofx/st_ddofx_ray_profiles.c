/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>
#include <string.h>

#include "st_ddofx_ray.h"
#include "st_ddofx_script.h"
#include "string_util.h"

enum {
	PROFILE_TEXT = 25
};

struct rc_profile {
	uint16_t chain, key;
	uint8_t  mouse, joystick, direction;
	bool     defined, action, stealth, resource_text;
	char     reply[PROFILE_TEXT], macro[PROFILE_TEXT];
	size_t   reply_length, macro_length;
};

struct rc_key {
	struct rc_key *next;
	uint16_t       word;
};

struct rc_profiles {
	struct rc_profile profiles[RC_PROFILES + 1];
	uint16_t          cells[RC_HEIGHT][RC_WIDTH];
	struct rc_key    *first_key, *last_key;
	int               previous_x, previous_y;
	unsigned          link, budget;
	uint64_t          resume_at;
	uint16_t          key;
	bool              entered, waiting;
};

static struct rc_profiles *
current(const struct st_ddofx *ddo)
{
	return ddo->graphics && ddo->graphics->ray ? ddo->graphics->ray->profiles : NULL;
}

void
st_ddofx_ray_clear_profiles(struct rc_world *w)
{
	if (!w || !w->profiles)
		return;
	struct rc_key *k = w->profiles->first_key;
	while (k) {
		struct rc_key *next = k->next;
		free(k);
		k = next;
	}
	free(w->profiles);
	w->profiles = NULL;
}

void
st_ddofx_ray_profiles_save(const struct rc_world *w, uint8_t *map, struct rc_saved_profile *records)
{
	const struct rc_profiles *p = w->profiles;
	for (int y = 0; y < RC_HEIGHT; ++y)
		for (int x = 0; x < RC_WIDTH; ++x)
			rc_put_word(map + 2 * (y * RC_PROFILE_STRIDE + x), p ? p->cells[y][x] : 0);
	for (unsigned i = 1; i <= RC_PROFILES; ++i) {
		struct rc_saved_profile *s = &records[i - 1];
		*s                         = (struct rc_saved_profile) {0};
		if (!p || !p->profiles[i].defined)
			continue;
		const struct rc_profile *r = &p->profiles[i];
		s->defined                 = 1;
		s->action                  = r->action;
		rc_put_word(s->key_high, r->key >> 8);
		rc_put_word(s->key_low, r->key & 255);
		s->mouse     = r->mouse;
		s->joystick  = r->joystick;
		s->direction = r->direction;
		s->stealth   = r->stealth;
		rc_put_word(s->chain, r->chain);
		s->reply[0] = (uint8_t)r->reply_length;
		s->macro[0] = (uint8_t)r->macro_length;
		memcpy(s->reply + 1, r->reply, s->reply[0]);
		memcpy(s->macro + 1, r->macro, s->macro[0]);
	}
}

bool
st_ddofx_ray_profiles_load(struct rc_world *w, const uint8_t *map, const struct rc_saved_profile *records)
{
	struct rc_profiles *p = calloc(1, sizeof(*p));
	if (!p)
		return false;
	for (int y = 0; y < RC_HEIGHT; ++y) {
		for (int x = 0; x < RC_WIDTH; ++x) {
			unsigned id = rc_word(map + 2 * (y * RC_PROFILE_STRIDE + x));
			if (id > RC_PROFILES)
				goto invalid;
			p->cells[y][x] = (uint16_t)id;
		}
	}
	bool defined = false;
	for (unsigned i = 1; i <= RC_PROFILES; ++i) {
		const struct rc_saved_profile *s = &records[i - 1];
		if (!s->defined)
			continue;
		if (s->reply[0] > PROFILE_TEXT || s->macro[0] > PROFILE_TEXT || rc_word(s->chain) > RC_PROFILES)
			goto invalid;
		defined              = true;
		struct rc_profile *r = &p->profiles[i];
		*r                   = (struct rc_profile) {.defined = true,
		    .action                        = s->action,
		    .stealth                       = s->stealth,
		    .chain                         = rc_word(s->chain),
		    .key                           = (s->key_high[0] << 8) | s->key_low[0],
		    .mouse                         = s->mouse,
		    .joystick                      = s->joystick,
		    .direction                     = s->direction};
		r->reply_length = s->reply[0];
		memcpy(r->reply, s->reply + 1, r->reply_length);
		r->macro_length = s->macro[0];
		memcpy(r->macro, s->macro + 1, r->macro_length);
	}
	st_ddofx_ray_clear_profiles(w);
	if (defined)
		w->profiles = p;
	else
		free(p);
	return true;
invalid:
	free(p);
	return false;
}

bool
st_ddofx_ray_accepts_key(const struct st_ddofx *ddo)
{
	return current(ddo) && ddo->graphics->ray->active && !ddo->background_paused;
}

void
st_ddofx_ray_key(struct st_ddofx *ddo, uint16_t word)
{
	if (!word || !st_ddofx_ray_accepts_key(ddo))
		return;
	struct rc_world    *w = ddo->graphics->ray;
	struct rc_profiles *p = w->profiles;
	if (!p->cells[(int)w->y - 1][(int)w->x - 1])
		return;
	struct rc_key *k = malloc(sizeof(*k));
	if (!k)
		return;
	*k = (struct rc_key) {.word = word};
	if (p->last_key)
		p->last_key->next = k;
	else
		p->first_key = k;
	p->last_key = k;
}

static bool
cell(int x, int y)
{
	return x >= 2 && x < RC_WIDTH && y >= 2 && y < RC_HEIGHT;
}

bool
st_ddofx_ray_profile_command(struct st_ddofx *ddo, const char *verb, char *args)
{
	bool define = !ascii_case_compare(verb, "5MAPPRO");
	bool remove = !ascii_case_compare(verb, "5DELMAPPRO");
	bool fill   = !ascii_case_compare(verb, "5FILPRO");
	if (!define && !remove && !fill && ascii_case_compare(verb, "5CPYPRO"))
		return false;
	int32_t id;
	if (!st_ddofx_field_number(ddo, &args, &id) || (unsigned)id > RC_PROFILES)
		return true;
	struct rc_world *w = ddo->graphics->ray;
	if (!id) {
		if (define || remove)
			st_ddofx_ray_clear_profiles(w);
		return true;
	}

	if (define) {
		int32_t n[9];
		for (unsigned i = 0; i < 9; ++i)
			if (!st_ddofx_field_number(ddo, &args, &n[i]))
				return true;
		const char *reply = st_ddofx_field(&args);
		if (!reply || !args || (unsigned)n[0] > 1 || (unsigned)n[1] > 255 || (unsigned)n[2] > 255
		    || (unsigned)n[3] > 7 || (unsigned)n[4] > 3 || n[5] < 1 || n[5] > 9
		    || (unsigned)n[6] > RC_PROFILES || ((n[7] || n[8]) && !cell(n[7], n[8])))
			return true;
		w = st_ddofx_ray_world(ddo->graphics);
		if (!w)
			return true;
		if (!w->profiles)
			w->profiles = calloc(1, sizeof(*w->profiles));
		if (!w->profiles)
			return true;
		struct st_ddofx_callback cb;
		st_ddofx_callback_define(ddo, &cb, reply, args, PROFILE_TEXT);
		struct rc_profile *r   = &w->profiles->profiles[id];
		*r                     = (struct rc_profile) {.defined = true,
		    .action                        = n[0],
		    .key                           = (n[1] << 8) | n[2],
		    .mouse                         = n[3],
		    .joystick                      = n[4],
		    .direction                     = n[5],
		    .chain                         = n[6],
		    .resource_text                 = cb.resource_text,
		    .stealth                       = cb.stealth};
		r->reply_length = cb.reply_length;
		memcpy(r->reply, cb.reply, r->reply_length);
		r->macro_length = cb.macro_length;
		memcpy(r->macro, cb.macro, cb.macro_length);
		if (n[7])
			w->profiles->cells[n[8] - 1][n[7] - 1] = id;
		return true;
	}

	struct rc_profiles *p = current(ddo);
	if (!p)
		return true;
	if (remove && !args) {
		memset(&p->profiles[id], 0, sizeof(p->profiles[id]));
		for (unsigned i = 1; i <= RC_PROFILES; ++i)
			if (p->profiles[i].chain == id)
				p->profiles[i].chain = 0;
	}
	else if (!p->profiles[id].defined)
		return true;
	if ((remove || fill) && !args) {
		for (int y = 1; y < RC_HEIGHT - 1; ++y)
			for (int x = 1; x < RC_WIDTH - 1; ++x)
				if (fill || p->cells[y][x] == id)
					p->cells[y][x] = fill ? id : 0;
	}
	else if (!remove && !fill) {
		int32_t xy[2];
		if (st_ddofx_numbers(ddo, args, xy, 2) && cell(xy[0], xy[1]))
			p->cells[xy[1] - 1][xy[0] - 1] = id;
	}
	return true;
}

static void
step(struct st_ddofx *ddo, struct rc_profiles *p)
{
	struct rc_world *w = ddo->graphics->ray;
	while (p->link && p->budget) {
		--p->budget;
		const struct rc_profile *r = &p->profiles[p->link];
		if (!r->defined || (ddo->background_download && !r->stealth)) {
			p->link = 0;
			break;
		}
		unsigned direction = 1 + (w->angle + 22) / 45 % 8;
		bool     match =
		    (!r->key || r->key == p->key) && (!r->mouse || r->mouse == ddo->graphics->mouse.buttons)
		    && (!r->joystick
		        || (!ddo->controller_paused
		            && (r->joystick == ddo->sticks[0].buttons || r->joystick == ddo->sticks[1].buttons)))
		    && (r->direction == 9 || r->direction == direction);
		if (r->action ? match : p->entered) {
			struct st_ddofx_callback cb = {.stealth = r->stealth, .resource_text = r->resource_text};
			cb.macro_length = r->macro_length;
			memcpy(cb.macro, r->macro, r->macro_length);
			cb.reply_length = r->reply_length;
			memcpy(cb.reply, r->reply, cb.reply_length);
			if (st_ddofx_callback_start(ddo, &cb, DDO_RAY)) {
				p->waiting = true;
				return;
			}
		}
		p->link = r->chain;
	}
}

void
st_ddofx_ray_after_profile(struct st_ddofx *ddo)
{
	struct rc_profiles *p = current(ddo);
	if (!p || !p->waiting)
		return;
	/* A macro can delete or redefine this link, or reset the entire world. */
	p->waiting = false;
	p->link    = p->profiles[p->link].chain;
	if (!ddo->background_paused)
		step(ddo, p);
}

uint64_t
st_ddofx_ray_profile_due(const struct st_ddofx *ddo)
{
	struct rc_profiles *p = current(ddo);
	if (!p || p->waiting || ddo->in_event)
		return UINT64_MAX;
	if (p->link)
		return p->resume_at;
	return ddo->graphics->ray->active && p->first_key ? 0 : UINT64_MAX;
}

void
st_ddofx_ray_profile_poll(struct st_ddofx *ddo, bool frame, uint64_t now)
{
	struct rc_profiles *p = current(ddo);
	if (!p || p->waiting || ddo->in_event || (p->link && now < p->resume_at))
		return;
	struct rc_world *w = ddo->graphics->ray;
	if (!p->link) {
		if (!w->active || (!frame && !p->first_key))
			return;
		int x = (int)w->x, y = (int)w->y;
		p->entered    = x != p->previous_x || y != p->previous_y;
		p->previous_x = x;
		p->previous_y = y;
		p->link       = p->cells[y - 1][x - 1];
		p->key        = 0;
		if (p->first_key) {
			struct rc_key *k = p->first_key;
			p->key           = k->word;
			p->first_key     = k->next;
			if (!p->first_key)
				p->last_key = NULL;
			free(k);
		}
	}
	/* A chain may intentionally revisit itself. Return to the session between slices. */
	p->budget = 32;
	p->resume_at = now + 5;
	step(ddo, p);
}

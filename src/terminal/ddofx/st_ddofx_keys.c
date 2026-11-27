/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>
#include <string.h>

#include "st_codepage.h"
#include "st_console.h"
#include "st_ddofx_script.h"

enum {
	KEY_COUNT = 300
};

struct binding {
	struct st_ddofx_callback callback;
	uint16_t                 word;
	bool                     pass;
};

struct st_ddofx_keys {
	struct binding          bindings[KEY_COUNT];
	struct st_console_event event;
	unsigned                next, count, active_slot;
	bool                    checked_button, active_button, local_only;
	uint16_t                word;
	unsigned                physical_scan;
	uint16_t                alt_echo;
	bool                    pass_echo;
};

void
st_ddofx_clear_keys(struct st_ddofx *ddo)
{
	if (ddo->in_key) {
		memset(ddo->keys->bindings, 0, sizeof(ddo->keys->bindings));
		ddo->keys->count = 0;
	}
	else {
		free(ddo->keys);
		ddo->keys = NULL;
	}
}

void
st_ddofx_define_key(struct st_ddofx *ddo, char *args)
{
	int32_t slot, high, low, pass;
	if (!st_ddofx_field_number(ddo, &args, &slot))
		return;
	if (!slot) {
		st_ddofx_clear_keys(ddo);
		return;
	}
	if (slot < 1 || slot > KEY_COUNT || !st_ddofx_field_number(ddo, &args, &high) || (unsigned)high > 255
	    || !st_ddofx_field_number(ddo, &args, &low) || (unsigned)low > 255
	    || !st_ddofx_field_number(ddo, &args, &pass) || (pass != 0 && pass != 1))
		return;
	const char *reply = st_ddofx_field(&args);
	if (!reply || !args)
		return;
	if (!ddo->keys)
		ddo->keys = calloc(1, sizeof(*ddo->keys));
	if (ddo->keys) {
		struct binding *b = &ddo->keys->bindings[slot - 1];
		st_ddofx_callback_define(ddo, &b->callback, reply, args);
		b->word = (uint16_t)((high << 8) | low);
		b->pass = pass;
		if (ddo->keys->count < (unsigned)slot)
			ddo->keys->count = (unsigned)slot;
	}
}

/* Text-only backends have no physical position. Use the PC keyboard
 * layout for ASCII; a preceding hardware event supplies the actual scan. */
static unsigned
ascii_scan(unsigned c)
{
	static const struct {
		unsigned    first;
		const char *row;
	} rows[] = {
	    {2,  "1234567890-="   },
            {2,  "!@#$%^&*()_+"   },
            {16, "qwertyuiop[]"   },
            {26, "{}"             },
            {30, "asdfghjkl;'\x60"},
	    {39, ":\"~"           },
            {43, "\\zxcvbnm,./"   },
            {43, "|ZXCVBNM<>?"    }
        };

	if (c == 0 || c >= 128)
		return 0;
	if (c >= 'A' && c <= 'Z')
		c += 'a' - 'A';
	for (unsigned i = 0; i < sizeof(rows) / sizeof(*rows); ++i) {
		const char *p = strchr(rows[i].row, (int)c);
		if (p)
			return rows[i].first + (unsigned)(p - rows[i].row);
	}
	switch (c) {
		case ' ':
			return 57;
		case 27:
			return 1;
		case 8:
			return 14;
		case 9:
			return 15;
		case 13:
			return 28;
		default:
			return 0;
	}
}

static uint16_t
special_key(unsigned code, uint32_t mods, unsigned physical)
{
	unsigned variant = mods & ST_CONSOLE_MODIFIER_ALT       ? 3
	                   : mods & ST_CONSOLE_MODIFIER_CONTROL ? 2
	                   : mods & ST_CONSOLE_MODIFIER_SHIFT   ? 1
	                                                        : 0;
	if (code >= ST_CONSOLE_KEY_F1 && code <= ST_CONSOLE_KEY_F12) {
		static const uint8_t first[4] = {59, 84, 94, 104}, f11[4] = {133, 135, 137, 139};
		unsigned             n = code - ST_CONSOLE_KEY_F1;
		return (uint16_t)((n < 10 ? first[variant] + n : f11[variant] + n - 10) << 8);
	}
	static const uint8_t nav[][4] = {
	    [ST_CONSOLE_KEY_UP - ST_CONSOLE_KEY_UP]        = {72, '8', 141, 152},
	    [ST_CONSOLE_KEY_DOWN - ST_CONSOLE_KEY_UP]      = {80, '2', 145, 160},
	    [ST_CONSOLE_KEY_LEFT - ST_CONSOLE_KEY_UP]      = {75, '4', 115, 155},
	    [ST_CONSOLE_KEY_RIGHT - ST_CONSOLE_KEY_UP]     = {77, '6', 116, 157},
	    [ST_CONSOLE_KEY_INSERT - ST_CONSOLE_KEY_UP]    = {82, '0', 146, 162},
	    [ST_CONSOLE_KEY_DELETE - ST_CONSOLE_KEY_UP]    = {83, '.', 147, 163},
	    [ST_CONSOLE_KEY_HOME - ST_CONSOLE_KEY_UP]      = {71, '7', 119, 151},
	    [ST_CONSOLE_KEY_END - ST_CONSOLE_KEY_UP]       = {79, '1', 117, 159},
	    [ST_CONSOLE_KEY_PAGE_UP - ST_CONSOLE_KEY_UP]   = {73, '9', 132, 153},
	    [ST_CONSOLE_KEY_PAGE_DOWN - ST_CONSOLE_KEY_UP] = {81, '3', 118, 161},
	};
	if (code < ST_CONSOLE_KEY_UP || code > ST_CONSOLE_KEY_PAGE_DOWN)
		return 0;
	const uint8_t *values = nav[code - ST_CONSOLE_KEY_UP];
	if (variant == 1)
		return (uint16_t)((values[0] << 8) | (physical == values[0] ? values[1] : 0));
	return (uint16_t)(values[variant] << 8);
}

static uint16_t
alt_word(unsigned scan)
{
	if (scan >= 2 && scan <= 13)
		scan += 118;
	return (uint16_t)(scan << 8);
}

static uint16_t
logical_key(const struct st_console_event *e, unsigned physical)
{
	if (e->type == ST_CONSOLE_EVENT_DOORWAY_KEY)
		return (uint16_t)e->data.doorway_key.scan_code << 8;
	if (e->type == ST_CONSOLE_EVENT_CODEPAGE_BYTE)
		return e->data.codepage_byte;
	bool     text = e->type == ST_CONSOLE_EVENT_TEXT;
	uint32_t c    = text ? e->data.text.codepoint : e->data.key.code;
	uint32_t mods = text ? e->data.text.modifiers : e->data.key.modifiers;
	if (!text && c >= ST_CONSOLE_KEY_UP)
		return special_key(c, mods, physical);
	uint8_t low;
	if (c < 128)
		low = (uint8_t)c;
	else if (!st_codepage_from_unicode(&low, ST_CODEPAGE_CP437, c))
		return 0;
	unsigned scan = physical ? physical : ascii_scan(low);
	if (mods & ST_CONSOLE_MODIFIER_ALT)
		return alt_word(scan);
	if (mods & ST_CONSOLE_MODIFIER_CONTROL) {
		if (low == '\r' && (!physical || physical == 28)) {
			low  = 10;
			scan = 28;
		}
		else if (low == '\b' && (!physical || physical == 14)) {
			low  = 127;
			scan = 14;
		}
		else if (low == '\t' && (!physical || physical == 15))
			return 0x9400;
		else if (low >= 1 && low <= 31)
			scan = physical ? physical : ascii_scan(low + 64);
		else if (low >= 64 && low <= 127)
			low &= 31;
		else if (low == ' ')
			low = 0;
	}
	else if (low == '\t' && (mods & ST_CONSOLE_MODIFIER_SHIFT))
		low = 0;
	return (uint16_t)((scan << 8) | low);
}

enum key_match {
	KEY_PASS,
	KEY_CONSUMED,
	KEY_CALLBACK
};

static enum key_match
next_binding(struct st_ddofx *ddo)
{
	struct st_ddofx_keys *keys = ddo->keys;
	while (keys->next < keys->count || keys->next < st_ddofx_control_count(ddo)) {
		unsigned slot = keys->next;
		if (!keys->checked_button) {
			keys->checked_button = true;
			if (st_ddofx_button_key(ddo, slot, keys->word)) {
				keys->active_button = true;
				keys->active_slot   = slot;
				return KEY_CALLBACK;
			}
		}
		keys->checked_button = false;
		++keys->next;
		if (st_ddofx_text_field_hotkey(ddo, slot, keys->word) || st_ddofx_choices_consume_key(ddo, slot, keys->word))
			return KEY_CONSUMED;
		struct binding *b = &keys->bindings[slot];
		if (b->word && (b->word == keys->word || b->word == UINT16_MAX)
		    && st_ddofx_callback_start(ddo, &b->callback, DDO_KEY)) {
			keys->active_button = false;
			keys->active_slot   = slot;
			return KEY_CALLBACK;
		}
	}
	return KEY_PASS;
}

int
st_ddofx_resume_key(struct st_ddofx *ddo, const struct st_ddofx_sink *sink)
{
	struct st_ddofx_keys *keys = ddo->keys;
	bool                  pass = keys->active_button ? st_ddofx_button_pass(ddo, keys->active_slot)
	                                                 : keys->bindings[keys->active_slot].pass;
	if (pass) {
		enum key_match match = next_binding(ddo);
		if (match == KEY_CALLBACK)
			return 0;
		pass = match == KEY_PASS;
	}
	ddo->in_key = false;
	pass        = pass && !keys->local_only && !ddo->keys_locked;
	if (keys->event.type == ST_CONSOLE_EVENT_PHYSICAL_KEY)
		keys->pass_echo = pass;
	if (pass && sink->pass_key) {
		int status = sink->pass_key(sink->context, &keys->event);
		if (!status)
			st_ddofx_ray_key(ddo, keys->word);
		return status;
	}
	return 0;
}

bool
st_ddofx_key(struct st_ddofx *ddo, const struct st_console_event *e)
{
	struct st_ddofx_keys *keys = ddo->keys;
	if (!keys && (st_ddofx_control_count(ddo) || st_ddofx_ray_accepts_key(ddo))) {
		ddo->keys = calloc(1, sizeof(*ddo->keys));
		keys      = ddo->keys;
	}
	if (!keys)
		return ddo->keys_locked;
	uint16_t word;
	if (e->type == ST_CONSOLE_EVENT_PHYSICAL_KEY) {
		unsigned scan = e->data.physical_key.evdev;
		if (!e->data.physical_key.pressed) {
			if (keys->physical_scan == scan) {
				keys->physical_scan = 0;
				keys->alt_echo      = 0;
			}
			return ddo->keys_locked || st_ddofx_text_field_editing(ddo);
		}
		keys->physical_scan = scan <= 83 ? scan : 0;
		keys->alt_echo      = 0;
		/* Some backends emit only the physical press for Alt+letter.
		 * If a translated copy follows, apply this same pass decision. */
		if (!(e->data.physical_key.modifiers & ST_CONSOLE_MODIFIER_ALT)
		    || !((scan >= 2 && scan <= 13) || (scan >= 16 && scan <= 27) || (scan >= 30 && scan <= 41)
		         || (scan >= 43 && scan <= 53)))
			return ddo->keys_locked || st_ddofx_text_field_editing(ddo);
		word = alt_word(scan);
	}
	else {
		word                = logical_key(e, keys->physical_scan);
		keys->physical_scan = 0;
		if (keys->alt_echo && keys->alt_echo == word) {
			keys->alt_echo = 0;
			return !keys->pass_echo || ddo->keys_locked;
		}
		keys->alt_echo = 0;
	}
	/* A Unicode text event can match the wildcard even without a DOS encoding. */
	if (!word && e->type != ST_CONSOLE_EVENT_TEXT)
		return ddo->keys_locked || st_ddofx_text_field_editing(ddo);
	if (st_ddofx_text_field_edit(ddo, word)) {
		if (e->type == ST_CONSOLE_EVENT_PHYSICAL_KEY) {
			keys->alt_echo = word;
			keys->pass_echo = false;
		}
		return true;
	}
	keys->word = word;
	keys->next = 0;
	keys->checked_button = false;
	enum key_match match = next_binding(ddo);
	if (match == KEY_PASS && !ddo->keys_locked)
		st_ddofx_ray_key(ddo, word);
	if (match == KEY_PASS && e->type != ST_CONSOLE_EVENT_PHYSICAL_KEY)
		return ddo->keys_locked;
	keys->event = *e;
	keys->local_only = ddo->background_download;
	if (e->type == ST_CONSOLE_EVENT_PHYSICAL_KEY)
		keys->alt_echo = word;
	if (match != KEY_CALLBACK) {
		keys->pass_echo = match == KEY_PASS && !ddo->keys_locked;
		return !keys->pass_echo;
	}
	ddo->in_key = true;
	return true;
}

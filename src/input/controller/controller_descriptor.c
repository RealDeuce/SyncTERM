/* Decode HID short items. Input report offsets include padding and fields
 * belonging to other top-level collections on the same interface. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "controller_internal.h"
#include "st_md5.h"

struct globals {
	unsigned page, id, width, count;
	int64_t  low, high;
};

struct usage_range {
	uint32_t first, last;
};

static int64_t
signed_item(uint32_t value, unsigned bits)
{
	return bits && (value & (UINT32_C(1) << (bits - 1))) ? (int64_t)value - (INT64_C(1) << bits) : value;
}

static bool
add_field(struct st_pad *p, const struct globals *g, unsigned bit, uint32_t usage, unsigned flags)
{
	unsigned page = usage >> 16, id = usage & 65535;
	if ((flags & 7) != 2 || !g->width || g->width > 32 || g->high <= g->low)
		return true;
	struct pad_field f =
	    {.report = g->id, .bit = bit, .width = g->width, .minimum = g->low, .maximum = g->high, .target = -1};
	if (page == 9 || (page == 1 && id >= 0x90 && id <= 0x93)) {
		f.kind = ST_BUTTON;
		if (page == 9)
			snprintf(f.label, sizeof f.label, "Button %u", id);
		else {
			static const unsigned codes[] = {544, 545, 547, 546};
			f.target                      = pad_target(1, codes[id - 0x90]);
			snprintf(f.label, sizeof f.label, "%s", st_controller_target((size_t)f.target)->label);
		}
	}
	else if (page == 1 && id == 0x39) {
		if (g->high - g->low != 7 && g->high - g->low != 3)
			return true;
		f.kind     = ST_HAT;
		unsigned n = 0;
		for (size_t i = 0; i < p->count; i++)
			n += p->fields[i].kind == ST_HAT;
		f.target = n < 4 ? pad_target(3, 16 + 2 * n) : -1;
		snprintf(f.label, sizeof f.label, "Hat %u", n + 1);
	}
	else if ((page == 1 && id >= 0x30 && id <= 0x38) || page == 2 || page >= 0xff00) {
		f.kind                           = g->high - g->low == 1 ? ST_BUTTON : ST_STICK;
		static const char *const names[] = {"X", "Y", "Z", "Rx", "Ry", "Rz", "Slider", "Dial", "Wheel"};
		if (page == 1) {
			snprintf(f.label, sizeof f.label, "%s axis", names[id - 0x30]);
			if (f.kind != ST_BUTTON && (id == 0x30 || id == 0x31 || id == 0x33 || id == 0x34))
				f.target = pad_target(3, id - 0x30);
		}
		else {
			snprintf(f.label, sizeof f.label, "Usage %04X:%04X", page, id);
			if (page == 2 && f.kind != ST_BUTTON) {
				switch (id) {
					case 0xba:
						f.target = pad_target(3, 7);
						break;
					case 0xbb:
						f.target = pad_target(3, 6);
						break;
					case 0xc4:
						f.target = pad_target(3, 9);
						break;
					case 0xc5:
						f.target = pad_target(3, 10);
						break;
					case 0xc8:
						f.target = pad_target(3, 8);
						break;
				}
			}
		}
	}
	else
		return true;
	if (p->count >= 4096)
		return false;
	struct pad_field *fields = realloc(p->fields, (p->count + 1) * sizeof(f));
	if (!fields)
		return false;
	p->fields          = fields;
	fields[p->count++] = f;
	return true;
}

/* Array slots contain selectors into the usage list. Button usage zero is
 * the empty selection. One report can select several buttons at once. */
static bool
add_button_array(struct st_pad *pad, const struct globals *g, unsigned bit, const struct usage_range *usages,
    unsigned count)
{
	if (!g->count || !g->width || g->width > 32 || g->low > g->high)
		return true;
	for (unsigned i = 0; i < count; i++)
		if ((usages[i].first >> 16) != 9 || (usages[i].last >> 16) != 9
		    || (i && !(usages[i].first & 65535)))
			return true;
	size_t         first  = pad->count;
	struct globals button = *g;
	button.low            = 0;
	button.high           = 1;
	int64_t selector = g->low, start = selector;
	for (unsigned i = 0; i < count; i++) {
		for (uint32_t u = usages[i].first; u <= usages[i].last && selector <= g->high; u++, selector++) {
			if (!(u & 65535)) {
				start = selector + 1;
				continue;
			}
			if (!add_field(pad, &button, bit, u, 2))
				return false;
		}
	}
	if (pad->count > first) {
		struct pad_field *group = &pad->fields[first];
		group->array_slots      = g->count;
		group->array_fields     = (unsigned)(pad->count - first);
		group->first_selector   = start;
		group->signed_selector  = g->low < 0;
	}
	return true;
}

struct st_pad *
pad_parse(const uint8_t *data, size_t len, unsigned wanted, unsigned *collections)
{
	struct globals     g = {0}, stack[16];
	struct usage_range usage[256];
	unsigned           depth = 0, saved = 0, used = 0, offsets[256] = {0}, selected_depth = 0;
	struct st_pad     *pad = calloc(1, sizeof(*pad));
	*collections           = 0;
	if (!pad)
		return NULL;
	bool numbered = false;
	for (size_t pos = 0; pos < len;) {
		uint8_t item = data[pos++];
		if (item == 0xfe) {
			if (len - pos < 2 || len - pos - 2 < data[pos])
				goto invalid;
			pos += 2 + (size_t)data[pos];
			continue;
		}
		unsigned n = item & 3, type = (item >> 2) & 3, tag = item >> 4;
		if (n == 3)
			n = 4;
		if (len - pos < n)
			goto invalid;
		uint32_t v = 0;
		for (unsigned j = 0; j < n; j++)
			v |= (uint32_t)data[pos++] << (8 * j);
		if (type == 1) {
			switch (tag) {
				case 0:
					g.page = v;
					break;
				case 1:
					g.low = signed_item(v, n * 8);
					break;
				case 2:
					g.high = g.low < 0 ? signed_item(v, n * 8) : (int64_t)v;
					break;
				case 7:
					g.width = v;
					break;
				case 8:
					if (!v || v > 255)
						goto invalid;
					g.id     = v;
					numbered = true;
					break;
				case 9:
					g.count = v;
					break;
				case 10:
					if (saved == 16)
						goto invalid;
					stack[saved++] = g;
					break;
				case 11:
					if (!saved)
						goto invalid;
					g = stack[--saved];
					break;
			}
		}
		else if (type == 2 && tag <= 2) {
			uint32_t u = n == 4 ? v : (g.page << 16) | v;
			if (tag == 2) {
				if (!used || u < usage[used - 1].first)
					goto invalid;
				usage[used - 1].last = u;
			}
			else {
				if (used == 256)
					goto invalid;
				usage[used++] = (struct usage_range) {u, u};
			}
		}
		else if (type == 0) {
			if (tag == 10) {
				if (++depth > 256)
					goto invalid;
				uint32_t u = used ? usage[0].first : 0;
				if (depth == 1 && v == 1 && (u == 0x10004 || u == 0x10005)
				    && (*collections)++ == wanted) {
					pad->device_class = u == 0x10004 ? 1 : 2;
					selected_depth    = depth;
				}
			}
			else if (tag == 12) {
				if (!depth)
					goto invalid;
				if (selected_depth == depth)
					selected_depth = 0;
				--depth;
			}
			else if (tag == 8) {
				uint64_t end = offsets[g.id] + (uint64_t)g.width * g.count;
				if (end > 65535u * 8u)
					goto invalid;
				if (selected_depth && used && (v & 7) == 2 && g.width && g.width <= 32) {
					unsigned r = 0;
					uint32_t u = usage[0].first;
					for (unsigned j = 0; j < g.count; j++) {
						if (!add_field(pad, &g, offsets[g.id] + j * g.width, u, v))
							goto invalid;
						if (u < usage[r].last)
							++u;
						else if (r + 1 < used)
							u = usage[++r].first;
					}
				}
				if (selected_depth && used && !(v & 7)
				    && !add_button_array(pad, &g, offsets[g.id], usage, used))
					goto invalid;
				offsets[g.id] = (unsigned)end;
			}
			used = 0;
		}
	}
	if (depth || !pad->count)
		goto invalid;
	pad->source   = 1;
	pad->numbered = numbered;
	for (unsigned i = 0; i < 256; i++)
		pad->payload_bytes[i] = (uint16_t)((offsets[i] + 7) / 8);
	/* Identically named usages in different physical collections may
	 * have different purposes. Leave ambiguous defaults for the mapper. */
	for (size_t i = 0; i < pad->count; i++) {
		int target = pad->fields[i].target;
		if (target < 0)
			continue;
		for (size_t j = i + 1; j < pad->count; j++)
			if (pad->fields[j].target == target) {
				pad->fields[i].target = -1;
				pad->fields[j].target = -1;
			}
	}
	return pad;
invalid:
	free(pad->fields);
	free(pad);
	return NULL;
}

static uint32_t
read_bits(const uint8_t *bytes, unsigned bit, unsigned width)
{
	uint32_t value = 0;
	for (unsigned i = 0; i < width; i++, bit++)
		value |= (uint32_t)((bytes[bit / 8] >> (bit % 8)) & 1) << i;
	return value;
}

void
pad_report(struct st_pad *p, const uint8_t *bytes, size_t size, bool numbered)
{
	if (!size)
		return;
	unsigned id = numbered ? *bytes++ : 0;
	if (size - numbered < p->payload_bytes[id])
		return;
	for (size_t i = 0; i < p->count; i++) {
		struct pad_field *f = p->fields + i;
		if (f->report != id)
			continue;
		if (f->array_slots) {
			for (unsigned k = 0; k < f->array_fields; k++)
				f[k].selected = false;
			for (unsigned k = 0; k < f->array_slots; k++) {
				uint32_t bits  = read_bits(bytes, f->bit + k * f->width, f->width);
				int64_t  value = f->signed_selector ? signed_item(bits, f->width) : (int64_t)bits;
				int64_t  field = value - f->first_selector;
				if (field >= 0 && field < f->array_fields)
					f[field].selected = true;
			}
			for (unsigned k = 0; k < f->array_fields; k++)
				pad_value(p, i + k, f[k].selected);
			i += f->array_fields - 1;
		}
		else {
			uint32_t bits = read_bits(bytes, f->bit, f->width);
			pad_value(p, i, f->minimum < 0 ? signed_item(bits, f->width) : (int64_t)bits);
		}
	}
}

void
pad_detach_collections(struct pad_collections *set)
{
	for (unsigned i = 0; i < set->count; i++)
		if (set->pads[i])
			set->pads[i]->removed = true;
	free(set->pads);
	*set = (struct pad_collections) {0};
}

void
pad_input_collections(struct pad_collections *set, const uint8_t *data, size_t size, st_controller_emit emit,
    void *ctx)
{
	for (unsigned i = 0; i < set->count; i++) {
		struct st_pad *p = set->pads[i];
		if (p) {
			pad_report(p, data, size, p->numbered);
			pad_publish(p, emit, ctx);
		}
	}
}

bool
pad_attach_collections(struct st_controllers *hub, struct pad_collections *set, const uint8_t *data, size_t size,
    unsigned vendor, unsigned product, const char *name)
{
	struct st_md5 md5;
	uint8_t       digest[16];
	char          fingerprint[33];
	st_md5_init(&md5);
	st_md5_update(&md5, data, size);
	st_md5_finish(&md5, digest);
	for (size_t i = 0; i < 16; i++)
		snprintf(fingerprint + 2 * i, 3, "%02x", digest[i]);

	unsigned count    = 1;
	bool     attached = false;
	for (unsigned i = 0; i < count; i++) {
		struct st_pad *pad = pad_parse(data, size, i, &count);
		if (!pad)
			continue;
		if (!set->pads) {
			set->pads = calloc(count, sizeof(*set->pads));
			if (!set->pads) {
				free(pad->fields);
				free(pad);
				break;
			}
			set->count = count;
		}
		snprintf(pad->name, sizeof pad->name, "%s (%04X:%04X, %u)",
		    name && *name            ? name
		    : pad->device_class == 2 ? "Gamepad"
		                             : "Joystick",
		    vendor, product, i + 1);
		snprintf(pad->profile, sizeof pad->profile, "%04x-%04x-%s-%u.map", vendor, product, fingerprint,
		    i);
		if (pad_attach(hub, pad)) {
			for (unsigned id = 0; id < 256; id++) {
				size_t bytes = pad->payload_bytes[id] + pad->numbered;
				if (bytes > set->report_size)
					set->report_size = bytes;
			}
			set->pads[i] = pad;
			attached     = true;
		}
		else {
			free(pad->fields);
			free(pad);
		}
	}
	if (!attached)
		pad_detach_collections(set);
	return attached;
}

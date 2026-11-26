/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef CONTROLLER_INTERNAL_H_
#define CONTROLLER_INTERNAL_H_
#include "st_controller.h"

struct pad_field {
	char                 label[64];
	enum st_control_kind kind;
	unsigned             report, bit, width;
	unsigned             array_slots, array_fields;
	int64_t              first_selector;
	bool                 selected, signed_selector;
	int64_t              minimum, maximum, raw, baseline;
	bool                 known, armed, inverted;
	int                  target;
};

struct pad_value {
	bool known;
	int  value;
};

struct st_pad {
	struct st_pad    *next;
	uint32_t          id, announced;
	bool              removed, remapped, numbered, dirty;
	unsigned          device_class, source, port, generation;
	uint16_t          payload_bytes[256];
	char              name[128], profile[96];
	struct pad_field *fields;
	size_t            count;
	struct pad_value *reported;
	int               actuation, actuated_value;
};

struct pad_driver {
	bool (*init)(struct st_controllers *);
	void (*poll)(struct st_controllers *, st_controller_emit, void *);
	void (*close)(struct st_controllers *);
};
struct st_controllers *pad_create(const char *, st_port_next, const struct pad_driver *);

struct st_controllers {
	struct st_pad           *pads;
	uint32_t                 next_id;
	char                    *directory;
	const struct pad_driver *driver;
	void                    *interfaces;
	uint64_t                 next_scan;
	st_port_next             ports;
};

struct pad_collections {
	struct st_pad **pads;
	unsigned        count;
	size_t          report_size;
};

bool pad_attach_collections(struct st_controllers *, struct pad_collections *, const uint8_t *, size_t,
    unsigned vendor, unsigned product, const char *name);
void pad_detach_collections(struct pad_collections *);
void pad_input_collections(struct pad_collections *, const uint8_t *, size_t, st_controller_emit, void *);

int            pad_target(unsigned type, unsigned code);
int            pad_scale(const struct pad_field *, enum st_control_kind kind);
void           pad_value(struct st_pad *, size_t, int64_t);
void           pad_publish(struct st_pad *, st_controller_emit, void *);
bool           pad_attach(struct st_controllers *, struct st_pad *);
void           pad_load_mapping(struct st_controllers *, struct st_pad *);
struct st_pad *pad_parse(const uint8_t *, size_t, unsigned collection, unsigned *collection_count);
void           pad_report(struct st_pad *, const uint8_t *, size_t, bool numbered);
bool           pad_native_init(struct st_controllers *hub);
void           pad_native_close(struct st_controllers *);
void           pad_native_poll(struct st_controllers *, st_controller_emit, void *);
#endif

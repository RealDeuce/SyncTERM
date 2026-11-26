/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_CONTROLLER_H_
#define ST_CONTROLLER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum st_control_kind {
	ST_BUTTON,
	ST_STICK,
	ST_TRIGGER,
	ST_HAT
};

struct st_control_target {
	const char          *label, *name;
	enum st_control_kind kind;
	unsigned             code;
};

#include "st_controller_event.h"

typedef void (*st_controller_emit)(void *, const struct st_controller_event *);
struct st_controllers;
struct st_pad;

/* A libretro sample is copied on the frontend thread and drained on the application thread. */
#define ST_CONTROLLER_PORTS 4

struct st_port_sample {
	unsigned port;
	unsigned generation;
	bool     present, analog;
	uint16_t buttons;
	int16_t  sticks[4], triggers[2];
};

typedef bool (*st_port_next)(struct st_port_sample *);

struct st_controllers *st_controllers_create(const char *settings_path, st_port_next ports);
void                   st_controllers_destroy(struct st_controllers *);
bool                   st_controllers_available(const struct st_controllers *);
uint32_t               st_controllers_wait_ms(const struct st_controllers *);
void                   st_controllers_poll(struct st_controllers *, st_controller_emit, void *);
void                   st_controllers_forget_listener(struct st_controllers *);

size_t                          st_controller_target_count(void);
const struct st_control_target *st_controller_target(size_t index);
struct st_pad                  *st_controller_first(struct st_controllers *);
struct st_pad                  *st_controller_next(struct st_pad *);
uint32_t                        st_controller_id(const struct st_pad *);
const char                     *st_controller_name(const struct st_pad *);
size_t                          st_controller_fields(const struct st_pad *);
const char                     *st_controller_field(const struct st_pad *, size_t index);
int                             st_controller_binding(const struct st_pad *, size_t index, bool *inverted);
bool                            st_controller_compatible(const struct st_pad *, size_t index, int target);
bool                            st_controller_preview(const struct st_pad *, size_t index, int *value);
void                            st_controller_arm(struct st_pad *);
int                             st_controller_actuation(const struct st_pad *, int *value);
int st_controller_bind(struct st_controllers *, struct st_pad *, size_t field, int target, bool invert,
    bool persist);
#endif

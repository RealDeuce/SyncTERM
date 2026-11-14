/* BIG control state and resumable flow/footer operations (21/32/33/43). */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_BIG_CONTROL_H_
#define ST_BIG_CONTROL_H_
#include "st_big_loop.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define ST_BIG_REPLY_CAPACITY 256
enum { ST_BIG_ZONE_COUNT = 48, ST_BIG_ZONE_BYTES = 81,
	ST_BIG_ZONE_DATA_SIZE = ST_BIG_ZONE_COUNT * ST_BIG_ZONE_BYTES };
struct st_big_control {
	struct { uint16_t mode, count[2]; uint8_t byte[2]; } flow;
	struct { uint16_t count; uint8_t pending, append_cr, payload[ST_BIG_REPLY_CAPACITY]; } input;
	struct { uint16_t step_x, step_y; } pointer;
	/* Flat counted records preserve in-table cross-record byte access. */
	struct { uint16_t rectangles[ST_BIG_ZONE_COUNT][4], loopback;
		uint8_t payload[ST_BIG_ZONE_DATA_SIZE]; } zones;
	struct { uint16_t command, did_loop; uint8_t gang_marker, gang[129]; } frame;
};
enum st_big_output_kind { ST_BIG_OUTPUT_XON, ST_BIG_OUTPUT_XOFF, ST_BIG_OUTPUT_REPLY };
struct st_big_control_sink {
	void *context;
	/* Optional local transport readiness. No foreign UART/status model.
	 * IN_PROGRESS waits; send retries mean the byte was not yet accepted.
	 * A send's byte argument remains latched while that request waits. */
	int (*ready)(void *, enum st_big_output_kind);
	int (*send)(void *, enum st_big_output_kind, uint8_t byte);
};
struct st_big_flow_reader { bool begun; };
struct st_big_flow_settings {
	unsigned phase;
};
/* X5's incremental class0 body, without its selector or shared footer.
 * next advances independent current-mode tests/defaults until another read
 * or completion. value commits one reached read, including the post-read
 * loop override and byte-before-count prefixes. Neither sends flow bytes.
 * Owners may change between serialized calls; a selected read stays selected. */
void st_big_flow_settings_begin(struct st_big_flow_settings *);
int st_big_flow_settings_next(struct st_big_flow_settings *, struct st_big_control *, bool *read);
int st_big_flow_settings_value(struct st_big_flow_settings *, struct st_big_control *,
	const struct st_big_loop *, uint16_t value);
enum st_big_footer_action { ST_BIG_FOOTER_REPEAT, ST_BIG_FOOTER_CHAIN, ST_BIG_FOOTER_RETURN };
struct st_big_footer {
	unsigned phase;
	bool chained;
	uint16_t index;
	uint8_t byte;
	struct st_big_flow_reader flow;
	enum st_big_footer_action action;
	int error;
};
void st_big_control_init(struct st_big_control *);
int32_t st_big_zone_id(uint16_t);
/* New interpreter frame only; application flow/input owners survive I/R. */
void st_big_control_enter(struct st_big_control *);
void st_big_flow_begin(struct st_big_flow_reader *, const struct st_big_control *, bool on);
/* Legacy flow sites complete without sending bytes or consuming budget.
 * The terminal's paused state supplies local session/connection backpressure;
 * X5 settings remain protocol state. No serial flow-control support is needed. */
int st_big_flow_poll(struct st_big_flow_reader *, struct st_big_control *,
	const struct st_big_control_sink *, size_t *budget);
void st_big_footer_begin(struct st_big_footer *, bool chained);
/* REPEAT precedes finishing flow/type/gang/reply work. CHAIN retains pending
 * input. RETURN preclears pending, reads live count/payload and optionally
 * appends CR without an extra ready call. Error prefixes are not rolled back.
 * Invalid calls preserve state; action is written only on completion. */
int st_big_footer_poll(struct st_big_footer *, struct st_big_loop *,
	struct st_big_control *, const struct st_big_control_sink *, size_t *budget,
	enum st_big_footer_action *action);
#endif

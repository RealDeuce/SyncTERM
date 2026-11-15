/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_BACKUP_H_
#define ST_RIP3_BACKUP_H_

#include <stdbool.h>
#include <stddef.h>

struct st_rip3_backup;

enum st_rip3_backup_place {
	ST_RIP3_BACKUP_BASE,
	ST_RIP3_BACKUP_SLOT,
	ST_RIP3_BACKUP_STACK,
};

/* One documented RIP data-object class per backup area. Callbacks must return
 * independent owned snapshots, preserve outputs/live state on failure, and
 * restore a whole table including its protection flags. The area owns each
 * successful snapshot until destruction/overwrite/consumption. Ops are copied;
 * their function code must outlive the area. All operations share one owner
 * thread, including callbacks; callbacks must not reenter the area. */
struct st_rip3_backup_ops {
	int (*clone)(const void *object, void **result);
	int (*restore)(void *object, const void *snapshot);
	void (*destroy)(void *snapshot);
};

struct st_rip3_backup_info {
	bool base;
	unsigned stack;
	unsigned slots;
	unsigned free;
	bool slot_used[10];
	bool slot_protected[10];
};

int st_rip3_backup_create(struct st_rip3_backup **result,
	const struct st_rip3_backup_ops *ops);
void st_rip3_backup_destroy(struct st_rip3_backup *backup);
/* SLOT takes index 0..9; BASE/STACK require index zero. Save STACK pushes;
 * restore STACK pops. Base is separate from the ten shared slot/stack places.
 * An occupied unprotected slot can be overwritten even when the area is full.
 * Failed cloning/restoration never overwrites or consumes an existing save.
 * BASE restoration retains it; unprotected SLOT restoration consumes it;
 * protected SLOT restoration retains it. */
int st_rip3_backup_save(struct st_rip3_backup *backup,
	enum st_rip3_backup_place place, unsigned index, const void *object);
int st_rip3_backup_restore(struct st_rip3_backup *backup,
	enum st_rip3_backup_place place, unsigned index, void *object);
/* Only occupied numbered slots can be protected; undefined protection is a
 * no-op. Individual protected clears fail. Clearing STACK removes all layers. */
int st_rip3_backup_protect(struct st_rip3_backup *backup, unsigned index, bool protect);
int st_rip3_backup_clear(struct st_rip3_backup *backup,
	enum st_rip3_backup_place place, unsigned index);
/* Explicit backup clearing, not a soft scene reset (which retains backups).
 * Without force, protected numbered slots survive. With force, all are cleared. */
int st_rip3_backup_clear_all(struct st_rip3_backup *backup, bool force);
int st_rip3_backup_info(const struct st_rip3_backup *backup,
	struct st_rip3_backup_info *result);
/* BACKSTAT's documented USE or PROT colon-separated ASCII, NUL terminated.
 * Insufficient output capacity leaves the caller's buffer unchanged. */
int st_rip3_backup_status(const struct st_rip3_backup *backup,
	bool protection, char *result, size_t capacity);

#endif

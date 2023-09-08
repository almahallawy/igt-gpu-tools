/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 *
 * Authors:
 *    Matthew Brost <matthew.brost@intel.com>
 */

#ifndef XE_SPIN_H
#define XE_SPIN_H

#include <stdint.h>
#include <stdbool.h>

#include "xe_query.h"
#include "lib/igt_dummyload.h"

/** struct xe_spin_opts
 *
 * @addr: offset of spinner within vm
 * @preempt: allow spinner to be preempted or not
 *
 * Used to initialize struct xe_spin spinner behavior.
 */
struct xe_spin_opts {
	uint64_t addr;
	bool preempt;
};

/* Mapped GPU object */
struct xe_spin {
	uint32_t batch[16];
	uint64_t pad;
	uint32_t start;
	uint32_t end;
};

igt_spin_t *xe_spin_create(int fd, const struct igt_spin_factory *opt);
void xe_spin_init(struct xe_spin *spin, struct xe_spin_opts *opts);

#define xe_spin_init_opts(fd, ...) \
	xe_spin_init(fd, &((struct xe_spin_opts){__VA_ARGS__}))

bool xe_spin_started(struct xe_spin *spin);
void xe_spin_sync_wait(int fd, struct igt_spin *spin);
void xe_spin_wait_started(struct xe_spin *spin);
void xe_spin_end(struct xe_spin *spin);
void xe_spin_free(int fd, struct igt_spin *spin);

struct xe_cork {
	struct xe_spin *spin;
	int fd;
	uint32_t vm;
	uint32_t bo;
	uint32_t exec_queue;
	uint32_t syncobj;
};

void xe_cork_init(int fd, struct drm_xe_engine_class_instance *hwe,
		  struct xe_cork *cork);
bool xe_cork_started(struct xe_cork *cork);
void xe_cork_wait_started(struct xe_cork *cork);
void xe_cork_end(struct xe_cork *cork);
void xe_cork_wait_done(struct xe_cork *cork);
void xe_cork_fini(struct xe_cork *cork);
uint32_t xe_cork_sync_handle(struct xe_cork *cork);

#endif	/* XE_SPIN_H */

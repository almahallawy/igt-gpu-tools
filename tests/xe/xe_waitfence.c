// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "igt.h"
#include "lib/igt_syncobj.h"
#include "lib/intel_reg.h"
#include "xe_drm.h"

#include "xe/xe_ioctl.h"
#include "xe/xe_query.h"
#include "xe/xe_spin.h"
#include <string.h>

/**
 * TEST: Check if waitfences work
 * Category: Software building block
 * Sub-category: waitfence
 * Functionality: waitfence
 * Test category: functionality test
 * Run type: BAT
 * Description: Test waitfences functionality
 */

#define MY_FLAG	vram_if_possible(fd, 0)

uint64_t wait_fence = 0;

static void do_bind(int fd, uint32_t vm, uint32_t bo, uint64_t offset,
		    uint64_t addr, uint64_t size, uint64_t val)
{
	struct drm_xe_sync sync[1] = {};
	sync[0].flags = DRM_XE_SYNC_USER_FENCE | DRM_XE_SYNC_SIGNAL;

	sync[0].addr = to_user_pointer(&wait_fence);
	sync[0].timeline_value = val;
	xe_vm_bind(fd, vm, bo, offset, addr, size, sync, 1);
}

enum waittype {
	RELTIME,
	ABSTIME,
};

/**
 * SUBTEST: reltime
 * Description: Check basic waitfences functionality with timeout
 *              as relative timeout in nanoseconds
 *
 * SUBTEST: abstime
 * Description: Check basic waitfences functionality with timeout
 *              passed as absolute time in nanoseconds
 */
static void
waitfence(int fd, enum waittype wt)
{
	uint32_t bo_1;
	uint32_t bo_2;
	uint32_t bo_3;
	uint32_t bo_4;
	uint32_t bo_5;
	uint32_t bo_6;
	uint32_t bo_7;
	int64_t timeout;

	uint32_t vm = xe_vm_create(fd, 0, 0);
	bo_1 = xe_bo_create_flags(fd, vm, 0x40000, MY_FLAG);
	do_bind(fd, vm, bo_1, 0, 0x200000, 0x40000, 1);
	bo_2 = xe_bo_create_flags(fd, vm, 0x40000, MY_FLAG);
	do_bind(fd, vm, bo_2, 0, 0xc0000000, 0x40000, 2);
	bo_3 = xe_bo_create_flags(fd, vm, 0x40000, MY_FLAG);
	do_bind(fd, vm, bo_3, 0, 0x180000000, 0x40000, 3);
	bo_4 = xe_bo_create_flags(fd, vm, 0x10000, MY_FLAG);
	do_bind(fd, vm, bo_4, 0, 0x140000000, 0x10000, 4);
	bo_5 = xe_bo_create_flags(fd, vm, 0x100000, MY_FLAG);
	do_bind(fd, vm, bo_5, 0, 0x100000000, 0x100000, 5);
	bo_6 = xe_bo_create_flags(fd, vm, 0x1c0000, MY_FLAG);
	do_bind(fd, vm, bo_6, 0, 0xc0040000, 0x1c0000, 6);
	bo_7 = xe_bo_create_flags(fd, vm, 0x10000, MY_FLAG);
	do_bind(fd, vm, bo_7, 0, 0xeffff0000, 0x10000, 7);

	if (wt == RELTIME) {
		timeout = xe_wait_ufence(fd, &wait_fence, 7, NULL, MS_TO_NS(10));
		igt_debug("wait type: RELTIME - timeout: %ld, timeout left: %ld\n",
			  MS_TO_NS(10), timeout);
	} else {
		struct timespec ts;
		int64_t current, signalled;

		clock_gettime(CLOCK_MONOTONIC, &ts);
		current = ts.tv_sec * 1e9 + ts.tv_nsec;
		timeout = current + MS_TO_NS(10);
		signalled = xe_wait_ufence_abstime(fd, &wait_fence, 7, NULL, timeout);
		igt_debug("wait type: ABSTIME - timeout: %" PRId64
			  ", signalled: %" PRId64
			  ", elapsed: %" PRId64 "\n",
			  timeout, signalled, signalled - current);
	}

	xe_vm_unbind_sync(fd, vm, 0, 0x200000, 0x40000);
	xe_vm_unbind_sync(fd, vm, 0, 0xc0000000, 0x40000);
	xe_vm_unbind_sync(fd, vm, 0, 0x180000000, 0x40000);
	xe_vm_unbind_sync(fd, vm, 0, 0x140000000, 0x10000);
	xe_vm_unbind_sync(fd, vm, 0, 0x100000000, 0x100000);
	xe_vm_unbind_sync(fd, vm, 0, 0xc0040000, 0x1c0000);
	xe_vm_unbind_sync(fd, vm, 0, 0xeffff0000, 0x10000);
	gem_close(fd, bo_7);
	gem_close(fd, bo_6);
	gem_close(fd, bo_5);
	gem_close(fd, bo_4);
	gem_close(fd, bo_3);
	gem_close(fd, bo_2);
	gem_close(fd, bo_1);
}

igt_main
{
	int fd;

	igt_fixture {
		fd = drm_open_driver(DRIVER_XE);
		xe_device_get(fd);
	}

	igt_subtest("reltime")
		waitfence(fd, RELTIME);

	igt_subtest("abstime")
		waitfence(fd, ABSTIME);

	igt_fixture {
		xe_device_put(fd);
		close(fd);
	}
}

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
 * Description: Test waitfences functionality
 */

#define MY_FLAG	vram_if_possible(fd, 0)

uint64_t wait_fence = 0;

static void do_bind(int fd, uint32_t vm, uint32_t bo, uint64_t offset,
		    uint64_t addr, uint64_t size, uint64_t val)
{
	struct drm_xe_sync sync[1] = {};
	sync[0].flags = DRM_XE_SYNC_FLAG_USER_FENCE | DRM_XE_SYNC_FLAG_SIGNAL;

	sync[0].addr = to_user_pointer(&wait_fence);
	sync[0].timeline_value = val;
	xe_vm_bind_async(fd, vm, 0, bo, offset, addr, size, sync, 1);
}

static int64_t wait_with_eci_abstime(int fd, uint64_t *addr, uint64_t value,
				     struct drm_xe_engine_class_instance *eci,
				     int64_t timeout)
{
	struct drm_xe_wait_user_fence wait = {
		.addr = to_user_pointer(addr),
		.op = DRM_XE_UFENCE_WAIT_OP_EQ,
		.flags = !eci ? 0 : DRM_XE_UFENCE_WAIT_FLAG_ABSTIME,
		.value = value,
		.mask = DRM_XE_UFENCE_WAIT_MASK_U64,
		.timeout = timeout,
		.num_engines = eci ? 1 : 0,
		.instances = eci ? to_user_pointer(eci) : 0,
	};
	struct timespec ts;

	igt_assert(eci);
	igt_assert_eq(igt_ioctl(fd, DRM_IOCTL_XE_WAIT_USER_FENCE, &wait), 0);
	igt_assert_eq(clock_gettime(CLOCK_MONOTONIC, &ts), 0);

	return ts.tv_sec * 1e9 + ts.tv_nsec;
}

enum waittype {
	RELTIME,
	ABSTIME,
	ENGINE,
};

/**
 * SUBTEST: reltime
 * Description: Check basic waitfences functionality with timeout
 *              as relative timeout in nanoseconds
 *
 * SUBTEST: abstime
 * Description: Check basic waitfences functionality with timeout
 *              passed as absolute time in nanoseconds
 *
 * SUBTEST: engine
 * Description: Check basic waitfences functionality with timeout
 *              passed as absolute time in nanoseconds and provide engine class
 *              instance
 */
static void
waitfence(int fd, enum waittype wt)
{
	struct drm_xe_engine_class_instance *eci = NULL;
	struct timespec ts;
	int64_t current, signalled;
	uint32_t bo_1;
	uint32_t bo_2;
	uint32_t bo_3;
	uint32_t bo_4;
	uint32_t bo_5;
	uint32_t bo_6;
	uint32_t bo_7;
	int64_t timeout;

	uint32_t vm = xe_vm_create(fd, DRM_XE_VM_CREATE_FLAG_ASYNC_DEFAULT, 0);
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
	} else if (wt == ENGINE) {
		eci = xe_hw_engine(fd, 1);
		clock_gettime(CLOCK_MONOTONIC, &ts);
		current = ts.tv_sec * 1e9 + ts.tv_nsec;
		timeout = current + MS_TO_NS(10);
		signalled = wait_with_eci_abstime(fd, &wait_fence, 7, eci, timeout);
		igt_debug("wait type: ENGINE ABSTIME - timeout: %" PRId64
			  ", signalled: %" PRId64
			  ", elapsed: %" PRId64 "\n",
			  timeout, signalled, signalled - current);
	} else {
		clock_gettime(CLOCK_MONOTONIC, &ts);
		current = ts.tv_sec * 1e9 + ts.tv_nsec;
		timeout = current + MS_TO_NS(10);
		signalled = xe_wait_ufence_abstime(fd, &wait_fence, 7, NULL, timeout);
		igt_debug("wait type: ABSTIME - timeout: %" PRId64
			  ", signalled: %" PRId64
			  ", elapsed: %" PRId64 "\n",
			  timeout, signalled, signalled - current);
	}
}

/**
 * TEST: Negative test for wait ufence ioctl
 * Category: Software building block
 * Sub-category: waitfence
 * Functionality: waitfence
 * Run type: FULL
 * Test category: negative test
 *
 * SUBTEST: invalid-flag
 * Description: Check query with invalid flag returns expected error code
 *
 * SUBTEST: invalid-ops
 * Description: Check query with invalid ops returns expected error code
 *
 * SUBTEST: invalid-engine
 * Description: Check query with invalid engine info returns expected error code
 */

static void
invalid_flag(int fd)
{
	uint32_t bo;

	struct drm_xe_wait_user_fence wait = {
		.addr = to_user_pointer(&wait_fence),
		.op = DRM_XE_UFENCE_WAIT_OP_EQ,
		.flags = -1,
		.value = 1,
		.mask = DRM_XE_UFENCE_WAIT_MASK_U64,
		.timeout = -1,
		.num_engines = 0,
		.instances = 0,
	};

	uint32_t vm = xe_vm_create(fd, DRM_XE_VM_CREATE_FLAG_ASYNC_DEFAULT, 0);

	bo = xe_bo_create_flags(fd, vm, 0x40000, MY_FLAG);

	do_bind(fd, vm, bo, 0, 0x200000, 0x40000, 1);

	do_ioctl_err(fd, DRM_IOCTL_XE_WAIT_USER_FENCE, &wait, EINVAL);
}

static void
invalid_ops(int fd)
{
	uint32_t bo;

	struct drm_xe_wait_user_fence wait = {
		.addr = to_user_pointer(&wait_fence),
		.op = -1,
		.flags = 0,
		.value = 1,
		.mask = DRM_XE_UFENCE_WAIT_MASK_U64,
		.timeout = 1,
		.num_engines = 0,
		.instances = 0,
	};

	uint32_t vm = xe_vm_create(fd, DRM_XE_VM_CREATE_FLAG_ASYNC_DEFAULT, 0);

	bo = xe_bo_create_flags(fd, vm, 0x40000, MY_FLAG);

	do_bind(fd, vm, bo, 0, 0x200000, 0x40000, 1);

	do_ioctl_err(fd, DRM_IOCTL_XE_WAIT_USER_FENCE, &wait, EINVAL);
}

static void
invalid_engine(int fd)
{
	uint32_t bo;

	struct drm_xe_wait_user_fence wait = {
		.addr = to_user_pointer(&wait_fence),
		.op = DRM_XE_UFENCE_WAIT_OP_EQ,
		.flags = 0,
		.value = 1,
		.mask = DRM_XE_UFENCE_WAIT_MASK_U64,
		.timeout = -1,
		.num_engines = 1,
		.instances = 0,
	};

	uint32_t vm = xe_vm_create(fd, DRM_XE_VM_CREATE_FLAG_ASYNC_DEFAULT, 0);

	bo = xe_bo_create_flags(fd, vm, 0x40000, MY_FLAG);

	do_bind(fd, vm, bo, 0, 0x200000, 0x40000, 1);

	do_ioctl_err(fd, DRM_IOCTL_XE_WAIT_USER_FENCE, &wait, EFAULT);
}


igt_main
{
	int fd;

	igt_fixture
		fd = drm_open_driver(DRIVER_XE);

	igt_subtest("reltime")
		waitfence(fd, RELTIME);

	igt_subtest("abstime")
		waitfence(fd, ABSTIME);

	igt_subtest("engine")
		waitfence(fd, ENGINE);

	igt_subtest("invalid-flag")
		invalid_flag(fd);

	igt_subtest("invalid-ops")
		invalid_ops(fd);

	igt_subtest("invalid-engine")
		invalid_engine(fd);

	igt_fixture
		drm_close_driver(fd);
}

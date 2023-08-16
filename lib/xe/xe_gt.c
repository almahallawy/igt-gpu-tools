/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 *
 * Authors:
 *    Janga Rahul Kumar <janga.rahul.kumar@intel.com>
 */

#include <fcntl.h>
#include <sys/stat.h>

#include "igt_core.h"
#include "igt_sysfs.h"
#include "lib/intel_chipset.h"
#include "xe_gt.h"
#include "xe_ioctl.h"
#include "xe_query.h"

#ifdef __linux__
#include <sys/sysmacros.h>
#else
#define minor(__v__) ((__v__) & 0xff)
#endif

/**
 * has_xe_gt_reset:
 * @fd: open xe drm file descriptor
 *
 * Check gt force reset sysfs entry is available or not
 *
 * Returns: reset sysfs entry available
 */
bool has_xe_gt_reset(int fd)
{
	char reset_sysfs_path[100];
	struct stat st;
	int gt;
	int reset_sysfs_fd = -1;
	int sysfs_fd = -1;

	igt_assert_eq(fstat(fd, &st), 0);
	sysfs_fd = igt_sysfs_open(fd);

	igt_assert(sysfs_fd != -1);
	xe_for_each_gt(fd, gt) {
		sprintf(reset_sysfs_path, "/sys/kernel/debug/dri/%d/gt%d/force_reset",
				minor(st.st_rdev), gt);
		reset_sysfs_fd = openat(sysfs_fd, reset_sysfs_path, O_RDONLY);

		if (reset_sysfs_fd == -1) {
			close(sysfs_fd);
			return 0;
		}

		close(reset_sysfs_fd);
	}

	close(sysfs_fd);
	return 1;
}

/**
 * xe_force_gt_reset_all:
 *
 * Forces reset of all the GT's.
 */
void xe_force_gt_reset_all(int xe_fd)
{
	int gt;

	xe_for_each_gt(xe_fd, gt)
		xe_force_gt_reset(xe_fd, gt);
}

/**
 * xe_hang_ring:
 * @fd: open xe drm file descriptor
 * @ring: execbuf ring flag
 *
 * This helper function injects a hanging batch into @ring. It returns a
 * #igt_hang_t structure which must be passed to xe_post_hang_ring() for
 * hang post-processing (after the gpu hang interaction has been tested).
 *
 * Returns:
 * Structure with helper internal state for xe_post_hang_ring().
 */
igt_hang_t xe_hang_ring(int fd, uint64_t ahnd, uint32_t ctx, int ring,
				unsigned int flags)
{
	uint16_t class;
	uint32_t vm;
	unsigned int exec_queue;
	igt_spin_t *spin_t;

	vm = xe_vm_create(fd, 0, 0);

	switch (ring) {
	case I915_EXEC_DEFAULT:
		if (IS_PONTEVECCHIO(intel_get_drm_devid(fd)))
			class = DRM_XE_ENGINE_CLASS_COPY;
		else
			class = DRM_XE_ENGINE_CLASS_RENDER;
		break;
	case I915_EXEC_RENDER:
		if (IS_PONTEVECCHIO(intel_get_drm_devid(fd)))
			igt_skip("Render engine not supported on this platform.\n");
		else
			class = DRM_XE_ENGINE_CLASS_RENDER;
		break;
	case I915_EXEC_BLT:
		class = DRM_XE_ENGINE_CLASS_COPY;
		break;
	case I915_EXEC_BSD:
		class = DRM_XE_ENGINE_CLASS_VIDEO_DECODE;
		break;
	case I915_EXEC_VEBOX:
		class = DRM_XE_ENGINE_CLASS_VIDEO_ENHANCE;
		break;
	default:
		igt_assert_f(false, "Unknown engine: %x", (uint32_t) flags);
	}

	exec_queue = xe_exec_queue_create_class(fd, vm, class);

	spin_t = igt_spin_new(fd, .ahnd = ahnd, .engine = exec_queue, .vm = vm,
				.flags = IGT_SPIN_NO_PREEMPTION);
	return (igt_hang_t){ spin_t, exec_queue, 0, flags };
}

/**
 * xe_post_hang_ring:
 * @fd: open xe drm file descriptor
 * @arg: hang state from xe_hang_ring()
 *
 * This function does the necessary post-processing after a gpu hang injected
 * with xe_hang_ring().
 */
void xe_post_hang_ring(int fd, igt_hang_t arg)
{
	xe_exec_queue_destroy(fd, arg.ctx);
	xe_vm_destroy(fd, arg.spin->vm);
}


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


// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

/**
 * TEST: Basic tests for access counter functionality
 * Category: Software building block
 * Run type: FULL
 * Sub-category: access counter
 * Functionality: access counter
 * Test category: functionality test
 * SUBTEST: invalid-param
 * Description: Giving invalid granularity size parameter and checks for invalid error.
 */

#include "igt.h"
#include "lib/igt_syncobj.h"
#include "lib/intel_reg.h"
#include "xe_drm.h"

#include "xe/xe_ioctl.h"
#include "xe/xe_query.h"
#include <string.h>

#define SIZE_64M  3
igt_main
{
	int fd;

	igt_fixture {
		uint16_t devid;

		fd = drm_open_driver(DRIVER_XE);
		devid = intel_get_drm_devid(fd);
		igt_require(xe_supports_faults(fd));
		igt_require(IS_PONTEVECCHIO(devid));
	}

	igt_subtest("invalid-param") {
		struct drm_xe_engine_class_instance instance = {
			 .engine_class = DRM_XE_ENGINE_CLASS_VM_BIND_SYNC,
		 };

		int ret;
		const int expected = -EINVAL;

		struct drm_xe_ext_set_property ext = {
			.base.next_extension = 0,
			.base.name = XE_EXEC_QUEUE_EXTENSION_SET_PROPERTY,
			.property = XE_EXEC_QUEUE_SET_PROPERTY_ACC_GRANULARITY,
			.value = SIZE_64M + 1,
		};

		struct drm_xe_exec_queue_create create = {
			.extensions = to_user_pointer(&ext),
			.vm_id = xe_vm_create(fd, 0, 0),
			.width = 1,
			.num_placements = 1,
			.instances = to_user_pointer(&instance),
		};

		if (igt_ioctl(fd, DRM_IOCTL_XE_EXEC_QUEUE_CREATE, &create)) {
			ret = -errno;
			errno = 0;
		}

		igt_assert_eq(ret, expected);
		ext.value = -1;

		if (igt_ioctl(fd, DRM_IOCTL_XE_EXEC_QUEUE_CREATE, &create)) {
			ret = -errno;
			errno = 0;
		}

		igt_assert_eq(ret, expected);
	}

	igt_fixture
		drm_close_driver(fd);
}

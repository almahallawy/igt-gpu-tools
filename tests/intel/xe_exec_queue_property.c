// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

/**
 * TEST: Basic tests to check exec_queue set property functionality
 * Category: Software building block
 * Sub-category: exec queue property
 * Functionality: exec queue set property
 * Test category: functionality test
 * SUBTEST: priority-set-property
 * Description: tests basic priority property by setting invalid values and positive values.
 * SUBTEST: persistence-set-property
 * Description: tests basic persistence property by setting positive values
 */

#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "igt.h"
#include "xe_drm.h"
#include "lib/igt_syncobj.h"
#include "lib/intel_reg.h"
#include "xe/xe_ioctl.h"
#include "xe/xe_query.h"

#define DRM_SCHED_PRIORITY_HIGH  2
#define DRM_SCHED_PRIORITY_NORMAL 1

static void test_set_property(int xe, int property_name,
			      int property_value, int err_val)
{
	struct drm_xe_engine_class_instance instance = {
			.engine_class = DRM_XE_ENGINE_CLASS_VM_BIND_SYNC,
	};
	struct drm_xe_ext_set_property ext = {
		.base.next_extension = 0,
		.base.name = XE_EXEC_QUEUE_EXTENSION_SET_PROPERTY,
		.property = property_name,
		.value = property_value,
	};

	struct drm_xe_exec_queue_create create = {
		.extensions = to_user_pointer(&ext),
		.width = 1,
		.num_placements = 1,
		.instances = to_user_pointer(&instance),
		.vm_id = xe_vm_create(xe, 0, 0),
	};
	int ret = 0;

	if (igt_ioctl(xe, DRM_IOCTL_XE_EXEC_QUEUE_CREATE, &create)) {
		ret = -errno;
		errno = 0;
	}
	igt_assert_eq(ret, err_val);
}

igt_main
{
	int xe;

	igt_fixture {
		xe = drm_open_driver(DRIVER_XE);
	}

	igt_subtest("priority-set-property") {
		/* Tests priority property by setting positive values. */
		test_set_property(xe, XE_EXEC_QUEUE_SET_PROPERTY_PRIORITY,
				  DRM_SCHED_PRIORITY_NORMAL, 0);

		/* Tests priority property by setting invalid value. */
		test_set_property(xe, XE_EXEC_QUEUE_SET_PROPERTY_PRIORITY,
				  DRM_SCHED_PRIORITY_HIGH + 1, -EINVAL);
		igt_fork(child, 1) {
			igt_drop_root();

			/* Tests priority property by dropping root permissions. */
			test_set_property(xe, XE_EXEC_QUEUE_SET_PROPERTY_PRIORITY,
					  DRM_SCHED_PRIORITY_HIGH, -EPERM);
			test_set_property(xe, XE_EXEC_QUEUE_SET_PROPERTY_PRIORITY,
					  DRM_SCHED_PRIORITY_NORMAL, 0);
		}
		igt_waitchildren();
	}

	igt_subtest("persistence-set-property") {
		/* Tests persistence property by setting positive values. */
		test_set_property(xe, XE_EXEC_QUEUE_SET_PROPERTY_PERSISTENCE, 1, 0);

	}

	igt_fixture {
		xe_device_put(xe);
		drm_close_driver(xe);
	}
}

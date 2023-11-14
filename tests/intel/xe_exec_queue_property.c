// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

/**
 * TEST: Basic tests to check exec_queue set/get property functionality
 * Category: Software building block
 * Sub-category: exec queue property
 * Functionality: exec queue set property
 * Test category: functionality test
 * SUBTEST: priority-set-property
 * Description: tests basic priority property by setting invalid values and positive values.
 * SUBTEST: persistence-set-property
 * Description: tests basic persistence property by setting positive values
 * SUBTEST: %s-property-min-max
 * Description: Test to check if %s arg[1] schedule parameter checks for min max values.
 *
 * arg[1]:
 *
 * @preempt_timeout_us:		preempt timeout us
 * @timeslice_duration_us:	timeslice duration us
 * @job_timeout_ms:		job timeout ms
 */

#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "igt.h"
#include "igt_sysfs.h"
#include "lib/igt_syncobj.h"
#include "lib/intel_reg.h"
#include "xe_drm.h"
#include "xe/xe_ioctl.h"
#include "xe/xe_query.h"

#define DRM_SCHED_PRIORITY_HIGH  2
#define DRM_SCHED_PRIORITY_NORMAL 1

static int get_property_name(const char *property)
{
	if (strstr(property, "preempt"))
		return XE_EXEC_QUEUE_SET_PROPERTY_PREEMPTION_TIMEOUT;
	else if (strstr(property, "job_timeout"))
		return XE_EXEC_QUEUE_SET_PROPERTY_JOB_TIMEOUT;
	else if (strstr(property, "timeslice"))
		return XE_EXEC_QUEUE_SET_PROPERTY_TIMESLICE;
	else
		return -1;
}

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

static void test_property_min_max(int xe, int engine, const char **property)
{
	unsigned int max;
	unsigned int min;
	unsigned int set;
	int property_name;
	int defaults;

	defaults = openat(engine, ".defaults", O_DIRECTORY);
	igt_require(defaults != -1);

	igt_sysfs_scanf(defaults, property[2], "%u", &max);
	igt_sysfs_scanf(defaults, property[1], "%u", &min);
	igt_sysfs_scanf(engine, property[0], "%u", &set);

	property_name = get_property_name(property[0]);
	igt_assert_neq(property_name, -1);

	/* Tests scheduler properties by setting positive values */
	test_set_property(xe, property_name, max, 0);
	test_set_property(xe, property_name, min, 0);

	/* Tests scheduler properties by setting invalid values */
	test_set_property(xe, property_name, max + 1, -EINVAL);
	test_set_property(xe, property_name, min - 1, -EINVAL);
}

/**
 * SUBTEST: Invalid-exec-queue
 * Description: Negative test to check the expected behaviour with invalid exec_queue_id.
 * Test category: functionality test
 */
static void Invalid_exec_queue_id(int xe)
{
	struct drm_xe_exec_queue_get_property args = {
		.exec_queue_id = 0xffff,
		.property = XE_EXEC_QUEUE_GET_PROPERTY_BAN,
	};

	do_ioctl_err(xe, DRM_IOCTL_XE_EXEC_QUEUE_GET_PROPERTY, &args, ENOENT);
}

/**
 * SUBTEST: non-zero-reserved
 * Description: Negative test to check the expected behaviour with non-zero reserved.
 * Test category: functionality test
 */
static void non_zero_reserved(int xe)
{
	struct drm_xe_exec_queue_get_property args = {
		.reserved[0] = 0xffff,
		.property = XE_EXEC_QUEUE_GET_PROPERTY_BAN,
	};
	uint32_t vm;
	uint32_t exec_queue;

	vm = xe_vm_create(xe, 0, 0);
	exec_queue = xe_exec_queue_create_class(xe, vm, DRM_XE_ENGINE_CLASS_COPY);
	args.exec_queue_id = exec_queue;

	do_ioctl_err(xe, DRM_IOCTL_XE_EXEC_QUEUE_GET_PROPERTY, &args, EINVAL);

	xe_exec_queue_destroy(xe, exec_queue);
	xe_vm_destroy(xe, vm);
}

/**
 * SUBTEST: basic-get-property
 * Description: Basic test to check if get property value works fine.
 * Test category: functionality test
 */
static void basic_get_property(int xe)
{
	struct drm_xe_exec_queue_get_property args = {
		.value = -1,
		.reserved[0] = 0,
		.property = XE_EXEC_QUEUE_GET_PROPERTY_BAN,
	};

	uint32_t exec_queue;
	uint32_t vm;

	vm = xe_vm_create(xe, 0, 0);
	exec_queue = xe_exec_queue_create_class(xe, vm, DRM_XE_ENGINE_CLASS_COPY);
	args.exec_queue_id = exec_queue;

	do_ioctl(xe, DRM_IOCTL_XE_EXEC_QUEUE_GET_PROPERTY, &args);
	igt_assert(args.value == 0);

	xe_exec_queue_destroy(xe, exec_queue);
	xe_vm_destroy(xe, vm);
}

igt_main
{
	static const struct {
		const char *name;
		void (*fn)(int, int, const char **);
	} tests[] = {{"property-min-max", test_property_min_max}, {} };

	const char *property[][3] = { {"preempt_timeout_us", "preempt_timeout_min", "preempt_timeout_max"},
				      {"timeslice_duration_us", "timeslice_duration_min", "timeslice_duration_max"},
				      {"job_timeout_ms", "job_timeout_min", "job_timeout_max"},
	};
	int count = sizeof(property) / sizeof(property[0]);
	int sys_fd;
	int xe;
	int gt;

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

	igt_subtest_group {
		igt_fixture {
			sys_fd = igt_sysfs_open(xe);
			igt_require(sys_fd != -1);
			close(sys_fd);
		}

		for (int i = 0; i < count; i++) {
			for (typeof(*tests) *t = tests; t->name; t++) {
				igt_subtest_with_dynamic_f("%s-%s", property[i][0], t->name) {
					xe_for_each_gt(xe, gt) {
						int engines_fd = -1;
						int gt_fd = -1;

						gt_fd = xe_sysfs_gt_open(xe, gt);
						igt_require(gt_fd != -1);
						engines_fd = openat(gt_fd, "engines", O_RDONLY);
						igt_require(engines_fd != -1);

						igt_sysfs_engines(xe, engines_fd, property[i], t->fn);
						close(engines_fd);
						close(gt_fd);
					}
				}
			}
		}
	}

	igt_subtest("Invalid-exec-queue")
		Invalid_exec_queue_id(xe);

	igt_subtest("non-zero-reserved")
		non_zero_reserved(xe);

	igt_subtest("basic-get-property")
		basic_get_property(xe);

	igt_fixture {
		xe_device_put(xe);
		drm_close_driver(xe);
	}
}

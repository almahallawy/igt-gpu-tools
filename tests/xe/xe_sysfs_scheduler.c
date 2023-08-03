// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

/**
 * TEST: xe sysfs scheduler
 * Run type: FULL
 *
 * SUBTEST: %s-invalid
 * Description: Test to check if %s arg[1] schedule parameter rejects any unrepresentable intervals.
 *
 * SUBTEST: %s-min-max
 * Description: Test to check if %s arg[1] schedule parameter checks for min max values.
 *
 * SUBTEST: %s-nonprivileged-user
 * Description: Test %s arg[1] schedule parameter for nonprivileged user.
 *
 * arg[1]:
 *
 * @preempt_timeout_us:		preempt timeout us
 * @timeslice_duration_us:	timeslice duration us
 * @job_timeout_ms:		job timeout ms
 */

#include <dirent.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "igt.h"
#include "igt_sysfs.h"

#include "xe_drm.h"
#include "xe/xe_query.h"

static void test_invalid(int xe, int engine, const char **property)
{
	unsigned int saved, set;
	unsigned int min, max;

	igt_sysfs_scanf(engine, property[2], "%u", &max);
	igt_sysfs_scanf(engine, property[1], "%u", &min);

	igt_assert(igt_sysfs_scanf(engine, property[0], "%u", &saved) == 1);
	igt_debug("Initial %s:%u\n", property[0], saved);

	igt_sysfs_printf(engine, property[0], "%d", max+100);
	igt_sysfs_scanf(engine, property[0], "%u", &set);
	igt_assert_eq(set, saved);

	igt_sysfs_printf(engine, property[0], "%d", min-100);
	igt_sysfs_scanf(engine, property[0], "%u", &set);
	igt_assert_eq(set, saved);
}

static void test_min_max(int xe, int engine, const char **property)
{
	unsigned int default_max, max;
	unsigned int default_min, min;
	unsigned int set;
	int defaults;

	defaults = openat(engine, ".defaults", O_DIRECTORY);
	igt_require(defaults != -1);

	igt_sysfs_scanf(defaults, property[2], "%u", &default_max);
	igt_sysfs_scanf(defaults, property[1], "%u", &default_min);

	igt_sysfs_printf(engine, property[2], "%d", default_max-10);
	igt_sysfs_scanf(engine, property[2], "%u", &max);
	igt_assert_eq(max, (default_max-10));

	igt_sysfs_printf(engine, property[2], "%d", default_max+1);
	igt_sysfs_scanf(engine, property[2], "%u", &max);
	igt_assert_neq(max, (default_max+1));

	igt_sysfs_printf(engine, property[1], "%d", default_min+1);
	igt_sysfs_scanf(engine, property[1], "%u", &min);
	igt_assert_eq(min, (default_min+1));

	igt_sysfs_printf(engine, property[1], "%d", default_min-10);
	igt_sysfs_scanf(engine, property[1], "%u", &min);
	igt_assert_neq(min, (default_min-10));

	igt_sysfs_printf(engine, property[0], "%d", min);
	igt_sysfs_scanf(engine, property[0], "%u", &set);
	igt_assert_eq(set, min);

	igt_sysfs_printf(engine, property[0], "%d", max);
	igt_sysfs_scanf(engine, property[0], "%u", &set);
	igt_assert_eq(set, max);

	igt_sysfs_printf(engine, property[0], "%d", default_min);
	igt_sysfs_scanf(engine, property[0], "%u", &set);
	igt_assert_eq(set, default_min);

	igt_sysfs_printf(engine, property[0], "%d", min);
	igt_sysfs_scanf(engine, property[0], "%u", &set);
	igt_assert_eq(set, min);

	/* Reset max, min to original values */
	igt_sysfs_printf(engine, property[1], "%d", default_min);
	igt_sysfs_printf(engine, property[2], "%d", default_max);
}

static void test_param_nonpriv(int xe, int engine, const char **property)
{
	unsigned int default_max, max;
	unsigned int default_min, min;
	unsigned int set;
	struct stat st;
	int defaults;

	fstat(engine, &st);
	fchmod(engine, (st.st_mode | S_IROTH | S_IWOTH));

	defaults = openat(engine, ".defaults", O_DIRECTORY);
	igt_require(defaults != -1);

	igt_sysfs_scanf(defaults, property[2], "%u", &default_max);
	igt_sysfs_scanf(defaults, property[1], "%u", &default_min);

	igt_sysfs_printf(engine, property[2], "%d", default_max-10);
	igt_sysfs_scanf(engine, property[2], "%u", &max);
	igt_assert_eq(max, (default_max-10));

	igt_sysfs_printf(engine, property[1], "%d", default_min+1);
	igt_sysfs_scanf(engine, property[1], "%u", &min);
	igt_assert_eq(min, (default_min+1));

	igt_fork(child, 1) {
		igt_drop_root();
		igt_sysfs_printf(engine, property[0], "%d", default_min);
		igt_sysfs_scanf(engine, property[0], "%u", &set);
		igt_assert_neq(set, default_min);

		igt_sysfs_printf(engine, property[0], "%d", min);
		igt_sysfs_scanf(engine, property[0], "%u", &set);
		igt_assert_eq(set, min);

		igt_sysfs_printf(engine, property[0], "%d", default_max);
		igt_sysfs_scanf(engine, property[0], "%u", &set);
		igt_assert_neq(set, default_max);

		igt_sysfs_printf(engine, property[0], "%d", max);
		igt_sysfs_scanf(engine, property[0], "%u", &set);
		igt_assert_eq(set, max);
	}
	igt_waitchildren();

	fchmod(engine, st.st_mode);

	/* Reset max, min to original values */
	igt_sysfs_printf(engine, property[1], "%d", default_min);
	igt_sysfs_printf(engine, property[2], "%d", default_max);
}

igt_main
{
	static const struct {
		const char *name;
		void (*fn)(int, int, const char **);
	} tests[] = {
		{ "invalid", test_invalid },
		{ "min-max", test_min_max },
		{ "nonprivileged-user", test_param_nonpriv },
		{ }
	};

	const char *property[][3] = { {"preempt_timeout_us", "preempt_timeout_min", "preempt_timeout_max"},
				      {"timeslice_duration_us", "timeslice_duration_min", "timeslice_duration_max"},
				      {"job_timeout_ms", "job_timeout_min", "job_timeout_max"},
	};
	int count = sizeof(property) / sizeof(property[0]);
	int xe = -1;
	int sys_fd;
	int gt;

	igt_fixture {
		xe = drm_open_driver(DRIVER_XE);
		xe_device_get(xe);

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
	igt_fixture {
		xe_device_put(xe);
		close(xe);
	}
}


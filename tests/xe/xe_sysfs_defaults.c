// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

/**
 * TEST: xe sysfs defaults
 * Category: Infrastructure
 * Functionality: driver handler
 * Run type: FULL
 * Sub-category: xe
 * Test category: SysMan
 * SUBTEST: engine-defaults
 */

#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "igt.h"
#include "igt_sysfs.h"

#include "xe_drm.h"
#include "xe/xe_query.h"

static void test_defaults(int xe, int engine, const char **property)
{
	struct dirent *de;
	int property_value;
	int defaults;
	DIR *dir;

	defaults = openat(engine, ".defaults", O_DIRECTORY);
	igt_require(defaults != -1);

	dir = fdopendir(engine);
	while ((de = readdir(dir))) {
		if (*de->d_name == '.')
			continue;

		igt_debug("Checking attr '%s'\n", de->d_name);

		igt_assert_f(property_value = igt_sysfs_get_u64(defaults, de->d_name),
			     "Default value %s is not present!\n", de->d_name);

		igt_debug("Default property:%s, value:%d\n", de->d_name, property_value);

		igt_assert_f(!igt_sysfs_set(defaults, de->d_name, "garbage"),
					    "write into default value of %s succeeded!\n",
					    de->d_name);
	}
	closedir(dir);
}

igt_main
{
	int xe, sys_fd;
	int gt;

	igt_fixture {
		xe = drm_open_driver(DRIVER_XE);
		xe_device_get(xe);

		sys_fd = igt_sysfs_open(xe);
		igt_require(sys_fd != -1);
	}

	igt_subtest_with_dynamic("engine-defaults") {
		xe_for_each_gt(xe, gt) {
			int engines_fd = -1;
			char buf[100];

			sprintf(buf, "device/gt%d/engines", gt);
			engines_fd = openat(sys_fd, buf, O_RDONLY);
			igt_require(engines_fd != -1);

			igt_sysfs_engines(xe, engines_fd, NULL, test_defaults);

			close(engines_fd);
		}
	}

	igt_fixture {
		close(sys_fd);
		xe_device_put(xe);
		close(xe);
	}
}


// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

/**
 * TEST: Test gtidle properties
 * Category: Software building block
 * Sub-category: Power Management
 * Functionality: GT C States
 * Test category: functionality test
 */

#include "igt.h"
#include "igt_sysfs.h"

#include "xe/xe_query.h"

#define SLEEP_DURATION 3000 /* in milliseconds */

const double tolerance = 0.1;

#define assert_within_epsilon(x, ref, tol) \
	igt_assert_f((double)(x) <= (1.0 + (tol)) * (double)(ref) && \
		     (double)(x) >= (1.0 - (tol)) * (double)(ref), \
		     "'%s' != '%s' (%f not within +%.1f%%/-%.1f%% tolerance of %f)\n",\
		     #x, #ref, (double)(x), \
		     (tol) * 100.0, (tol) * 100.0, \
		     (double)(ref))

IGT_TEST_DESCRIPTION("Tests for gtidle properties");

static unsigned int measured_usleep(unsigned int usec)
{
	struct timespec ts = { };
	unsigned int slept;

	slept = igt_nsec_elapsed(&ts);
	igt_assert(slept == 0);
	do {
		usleep(usec - slept);
		slept = igt_nsec_elapsed(&ts) / 1000;
	} while (slept < usec);

	return igt_nsec_elapsed(&ts) / 1000;
}

static bool is_gt_in_c6(int fd, int gt)
{
	char gt_c_state[16];
	int gt_fd;

	gt_fd = xe_sysfs_gt_open(fd, gt);
	igt_assert(gt_fd >= 0);
	igt_assert(igt_sysfs_scanf(gt_fd, "gtidle/idle_status", "%s", gt_c_state) == 1);
	close(gt_fd);

	return strcmp(gt_c_state, "gt-c6") == 0;
}

static unsigned long read_idle_residency(int fd, int gt)
{
	unsigned long residency = 0;
	int gt_fd;

	gt_fd = xe_sysfs_gt_open(fd, gt);
	igt_assert(gt_fd >= 0);
	igt_assert(igt_sysfs_scanf(gt_fd, "gtidle/idle_residency_ms", "%lu", &residency) == 1);
	close(gt_fd);

	return residency;
}

/**
 * SUBTEST: idle-residency
 * Description: basic residency test to validate idle residency
 *		measured over a time interval is within the tolerance
 * Run type: FULL
 */
static void test_idle_residency(int fd, int gt)
{
	unsigned long elapsed_ms, residency_start, residency_end;

	igt_assert_f(igt_wait(is_gt_in_c6(fd, gt), 1000, 1), "GT not in C6\n");

	residency_start = read_idle_residency(fd, gt);
	elapsed_ms = measured_usleep(SLEEP_DURATION * 1000) / 1000;
	residency_end = read_idle_residency(fd, gt);

	igt_info("Measured %lums of idle residency in %lums\n",
		 residency_end - residency_start, elapsed_ms);

	assert_within_epsilon(residency_end - residency_start, elapsed_ms, tolerance);
}

igt_main
{
	int fd, gt;

	igt_fixture {
		fd = drm_open_driver(DRIVER_XE);
		igt_require(!IS_PONTEVECCHIO(xe_dev_id(fd)));
	}

	igt_describe("Validate idle residency measured over a time interval is within the tolerance");
	igt_subtest("idle-residency")
		xe_for_each_gt(fd, gt)
			test_idle_residency(fd, gt);

	igt_fixture {
		close(fd);
	}
}

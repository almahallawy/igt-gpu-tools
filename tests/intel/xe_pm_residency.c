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
#include <limits.h>

#include "igt.h"
#include "igt_device.h"
#include "igt_sysfs.h"

#include "xe/xe_query.h"
#include "xe/xe_util.h"

#define SLEEP_DURATION 3000 /* in milliseconds */

const double tolerance = 0.1;

#define assert_within_epsilon(x, ref, tol) \
	igt_assert_f((double)(x) <= (1.0 + (tol)) * (double)(ref) && \
		     (double)(x) >= (1.0 - (tol)) * (double)(ref), \
		     "'%s' != '%s' (%f not within +%.1f%%/-%.1f%% tolerance of %f)\n",\
		     #x, #ref, (double)(x), \
		     (tol) * 100.0, (tol) * 100.0, \
		     (double)(ref))

enum test_type {
	TEST_S2IDLE,
	TEST_IDLE,
};

/**
 * SUBTEST: gt-c6-on-idle
 * Description: Validate GT C6 state on idle
 * Run type: BAT
 *
 * SUBTEST: idle-residency
 * Description: basic residency test to validate idle residency
 *		measured over a time interval is within the tolerance
 * Run type: FULL
 *
 * SUBTEST: gt-c6-freeze
 * Description: Validate idle residency measured over suspend(s2idle)
 *              is greater than suspend time or within tolerance
 * Run type: FULL
 */
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

static void test_idle_residency(int fd, int gt, enum test_type flag)
{
	unsigned long elapsed_ms, residency_start, residency_end;

	igt_assert_f(igt_wait(xe_is_gt_in_c6(fd, gt), 1000, 1), "GT not in C6\n");

	if (flag == TEST_S2IDLE) {
		/*
		 * elapsed time during suspend is approximately equal to autoresume delay
		 * when a full suspend cycle(SUSPEND_TEST_NONE) is used.
		 */
		elapsed_ms = igt_get_autoresume_delay(SUSPEND_STATE_FREEZE);
		residency_start = read_idle_residency(fd, gt);
		igt_system_suspend_autoresume(SUSPEND_STATE_FREEZE, SUSPEND_TEST_NONE);
		residency_end = read_idle_residency(fd, gt);

		/*
		 * Idle residency may increase even after suspend, only assert if residency
		 * is lesser than autoresume delay and is not within tolerance.
		 */
		if ((residency_end - residency_start) >= elapsed_ms)
			return;
	}

	if (flag == TEST_IDLE) {
		residency_start = read_idle_residency(fd, gt);
		elapsed_ms = measured_usleep(SLEEP_DURATION * 1000) / 1000;
		residency_end = read_idle_residency(fd, gt);
	}

	igt_info("Measured %lums of idle residency in %lums\n",
		 residency_end - residency_start, elapsed_ms);

	assert_within_epsilon(residency_end - residency_start, elapsed_ms, tolerance);
}

igt_main
{
	uint32_t d3cold_allowed;
	int fd, gt;
	char pci_slot_name[NAME_MAX];

	igt_fixture {
		fd = drm_open_driver(DRIVER_XE);
		igt_require(!IS_PONTEVECCHIO(xe_dev_id(fd)));
	}

	igt_describe("Validate GT C6 on idle");
	igt_subtest("gt-c6-on-idle")
		xe_for_each_gt(fd, gt)
			igt_assert_f(igt_wait(xe_is_gt_in_c6(fd, gt), 1000, 1), "GT not in C6\n");

	igt_describe("Validate idle residency measured over suspend cycle is within the tolerance");
	igt_subtest("gt-c6-freeze") {
		if (xe_has_vram(fd)) {
			igt_device_get_pci_slot_name(fd, pci_slot_name);
			igt_pm_get_d3cold_allowed(pci_slot_name, &d3cold_allowed);
			igt_pm_set_d3cold_allowed(pci_slot_name, 0);
		}
		xe_for_each_gt(fd, gt)
			test_idle_residency(fd, gt, TEST_S2IDLE);

		if (xe_has_vram(fd))
			igt_pm_set_d3cold_allowed(pci_slot_name, d3cold_allowed);
	}

	igt_describe("Validate idle residency measured over a time interval is within the tolerance");
	igt_subtest("idle-residency")
		xe_for_each_gt(fd, gt)
			test_idle_residency(fd, gt, TEST_IDLE);

	igt_fixture {
		close(fd);
	}
}

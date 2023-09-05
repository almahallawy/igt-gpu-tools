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
#include <fcntl.h>
#include <limits.h>

#include "igt.h"
#include "igt_device.h"
#include "igt_power.h"
#include "igt_sysfs.h"

#include "xe/xe_query.h"
#include "xe/xe_util.h"

#define NUM_REPS 16 /* No of Repetitions */
#define SLEEP_DURATION 3000 /* in milliseconds */

const double tolerance = 0.1;
int fw_handle = -1;

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
 *
 * SUBTEST: idle-residency
 * Description: basic residency test to validate idle residency
 *		measured over a time interval is within the tolerance
 *
 * SUBTEST: gt-c6-freeze
 * Description: Validate idle residency measured over suspend(s2idle)
 *              is greater than suspend time or within tolerance
 *
 * SUBTEST: toggle-gt-c6
 * Description: toggles GT C states by acquiring/releasing forcewake,
 *		also validates power consumed by GPU in GT C6 is lesser than that of GT C0.
 */
IGT_TEST_DESCRIPTION("Tests for gtidle properties");

static void close_fw_handle(int sig)
{
	if (fw_handle >= 0)
		close(fw_handle);
}

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

static void measure_power(struct igt_power *gpu, double *power)
{
	struct power_sample power_sample[2];

	igt_power_get_energy(gpu, &power_sample[0]);
	measured_usleep(SLEEP_DURATION * 1000);
	igt_power_get_energy(gpu, &power_sample[1]);
	*power = igt_power_get_mW(gpu, &power_sample[0], &power_sample[1]);
}

static void toggle_gt_c6(int fd, int n)
{
	double gt_c0_power, gt_c6_power;
	int gt;
	struct igt_power gpu;

	igt_power_open(fd, &gpu, "gpu");

	do {
		fw_handle = igt_debugfs_open(fd, "forcewake_all", O_RDONLY);
		igt_assert(fw_handle >= 0);
		/* check if all gts are in C0 after forcewake is acquired */
		xe_for_each_gt(fd, gt)
			igt_assert_f(!xe_is_gt_in_c6(fd, gt),
				     "Forcewake acquired, GT should be in C0\n");

		if (n == NUM_REPS)
			measure_power(&gpu, &gt_c0_power);

		close(fw_handle);
		/* check if all gts are in C6 after forcewake is released */
		xe_for_each_gt(fd, gt)
			igt_assert_f(igt_wait(xe_is_gt_in_c6(fd, gt), 1000, 1),
				     "Forcewake released, GT should be in C6\n");

		if (n == NUM_REPS)
			measure_power(&gpu, &gt_c6_power);
	} while (n--);

	igt_power_close(&gpu);
	igt_info("GPU consumed %fmW in GT C6 and %fmW in GT C0\n", gt_c6_power, gt_c0_power);

	/* FIXME: Remove dgfx check after hwmon is added */
	if (!xe_has_vram(fd))
		igt_assert_f(gt_c6_power < gt_c0_power,
			     "Power consumed in GT C6 should be lower than GT C0\n");
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

	igt_describe("Toggle GT C states by acquiring/releasing forcewake and validate power measured");
	igt_subtest("toggle-gt-c6") {
		igt_install_exit_handler(close_fw_handle);
		toggle_gt_c6(fd, NUM_REPS);
	}

	igt_fixture {
		close(fd);
	}
}

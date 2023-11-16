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
#include <time.h>

#include "igt.h"
#include "igt_device.h"
#include "igt_power.h"
#include "igt_sysfs.h"

#include "lib/igt_syncobj.h"
#include "xe/xe_ioctl.h"
#include "xe/xe_query.h"
#include "xe/xe_util.h"

#define NUM_REPS 16 /* No of Repetitions */
#define SLEEP_DURATION 3 /* in seconds */

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
 * SUBTEST: idle-residency-on-exec
 * Description: Validate idle residency measured when a background
 *		load is only active for ~1% of the time
 * Run type: FULL
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

static void exec_load(int fd, struct drm_xe_engine_class_instance *hwe, unsigned long *done)
{
	uint32_t bo = 0;
	uint32_t exec_queue, syncobj, vm;
	uint64_t addr = 0x1a0000;
	uint64_t batch_addr, batch_offset, data_addr, data_offset;
	size_t bo_size;
	int b;
	struct {
		uint32_t batch[16];
		uint64_t pad;
		uint32_t data;
	} *data;

	struct drm_xe_sync sync = {
		.flags = DRM_XE_SYNC_FLAG_SYNCOBJ | DRM_XE_SYNC_FLAG_SIGNAL,
	};

	struct drm_xe_exec exec = {
		.num_batch_buffer = 1,
		.num_syncs = 1,
		.syncs = to_user_pointer(&sync),
	};

	vm = xe_vm_create(fd, 0, 0);
	exec_queue = xe_exec_queue_create(fd, vm, hwe, 0);
	bo_size = xe_get_default_alignment(fd);

	bo = xe_bo_create(fd, vm, bo_size,
			  vram_if_possible(fd, hwe->gt_id),
			  DRM_XE_GEM_CREATE_FLAG_NEEDS_VISIBLE_VRAM);
	data = xe_bo_map(fd, bo, bo_size);
	syncobj = syncobj_create(fd, 0);

	xe_vm_bind_sync(fd, vm, bo, 0, addr, bo_size);

	batch_offset = (char *)&data->batch - (char *)data;
	batch_addr = addr + batch_offset;
	data_offset = (char *)&data->data - (char *)data;
	data_addr = addr + data_offset;

	/* Aim for ~1% busy */
	do {
		uint64_t submit, elapsed;
		struct timespec tv = {};

		b = 0;
		done[1]++;
		data->batch[b++] = MI_STORE_DWORD_IMM_GEN4;
		data->batch[b++] = data_addr;
		data->batch[b++] = data_addr >> 32;
		data->batch[b++] = done[1];
		data->batch[b++] = MI_BATCH_BUFFER_END;
		igt_assert(b <= ARRAY_SIZE(data->batch));

		exec.exec_queue_id = exec_queue;
		exec.address = batch_addr;
		sync.handle = syncobj;

		igt_nsec_elapsed(&tv);
		xe_exec(fd, &exec);
		submit = igt_nsec_elapsed(&tv);

		igt_assert(syncobj_wait(fd, &syncobj, 1, INT64_MAX, 0, NULL));
		elapsed = igt_nsec_elapsed(&tv);
		igt_assert_eq(data->data, done[1]);

		igt_debug("Execution took %.3fms (submit %.1fus, wait %.1fus)\n",
			  1e-6 * elapsed,
			  1e-3 * submit,
			  1e-3 * (elapsed - submit));

		syncobj_reset(fd, &syncobj, 1);

		/*
		 * Execute the above workload for ~1% of the elapsed time and sleep for
		 * the rest of the time (~99%)
		 */
		usleep(elapsed / 10);
	} while (!READ_ONCE(*done));

	xe_vm_unbind_sync(fd, vm, 0, addr, bo_size);
	syncobj_destroy(fd, syncobj);
	munmap(data, bo_size);
	gem_close(fd, bo);
	xe_exec_queue_destroy(fd, exec_queue);
	xe_vm_destroy(fd, vm);
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

	igt_assert_f(igt_wait(xe_is_gt_in_c6(fd, gt), 1000, 1), "GT %d not in C6\n", gt);

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
		elapsed_ms = measured_usleep(SLEEP_DURATION * USEC_PER_SEC) / 1000;
		residency_end = read_idle_residency(fd, gt);
	}

	igt_info("Measured %lums of idle residency in %lums\n",
		 residency_end - residency_start, elapsed_ms);

	assert_within_epsilon(residency_end - residency_start, elapsed_ms, tolerance);
}

static void idle_residency_on_exec(int fd, struct drm_xe_engine_class_instance *hwe)
{
	const int tol = 20;
	unsigned long *done;
	unsigned long end, start;
	unsigned long elapsed_ms, residency_end, residency_start;

	igt_debug("Running on %s:%d\n",
		  xe_engine_class_string(hwe->engine_class), hwe->engine_instance);
	done = mmap(0, 4096, PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
	igt_assert(done != MAP_FAILED);
	memset(done, 0, 4096);

	igt_fork(child, 1)
		exec_load(fd, hwe, done);

	start = READ_ONCE(done[1]);
	residency_start = read_idle_residency(fd, hwe->gt_id);
	elapsed_ms = measured_usleep(SLEEP_DURATION * USEC_PER_SEC) / 1000;
	residency_end = read_idle_residency(fd, hwe->gt_id);
	end = READ_ONCE(done[1]);
	*done = 1;

	igt_waitchildren();

	/* At least one wakeup/s needed for a reasonable test */
	igt_assert(end - start);

	/* While very nearly busy, expect full GT C6 */
	assert_within_epsilon((residency_end - residency_start), elapsed_ms, tol);

	munmap(done, 4096);
}

static void measure_power(struct igt_power *gpu, double *power)
{
	struct power_sample power_sample[2];

	igt_power_get_energy(gpu, &power_sample[0]);
	measured_usleep(SLEEP_DURATION * USEC_PER_SEC);
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
				     "Forcewake acquired, GT %d should be in C0\n", gt);

		if (n == NUM_REPS)
			measure_power(&gpu, &gt_c0_power);

		close(fw_handle);
		/* check if all gts are in C6 after forcewake is released */
		xe_for_each_gt(fd, gt)
			igt_assert_f(igt_wait(xe_is_gt_in_c6(fd, gt), 1000, 1),
				     "Forcewake released, GT %d should be in C6\n", gt);

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
	struct drm_xe_engine_class_instance *hwe;

	igt_fixture {
		fd = drm_open_driver(DRIVER_XE);
		igt_require(!IS_PONTEVECCHIO(xe_dev_id(fd)));
	}

	igt_describe("Validate GT C6 on idle");
	igt_subtest("gt-c6-on-idle")
		xe_for_each_gt(fd, gt)
			igt_assert_f(igt_wait(xe_is_gt_in_c6(fd, gt), 1000, 1), "GT %d not in C6\n", gt);

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

	igt_describe("Validate idle residency on exec");
	igt_subtest("idle-residency-on-exec") {
		xe_for_each_gt(fd, gt) {
			xe_for_each_engine(fd, hwe) {
				if (gt == hwe->gt_id && !hwe->engine_instance)
					idle_residency_on_exec(fd, hwe);
			}
		}
	}

	igt_describe("Toggle GT C states by acquiring/releasing forcewake and validate power measured");
	igt_subtest("toggle-gt-c6") {
		igt_install_exit_handler(close_fw_handle);
		toggle_gt_c6(fd, NUM_REPS);
	}

	igt_fixture {
		close(fd);
	}
}

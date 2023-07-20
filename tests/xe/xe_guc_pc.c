// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

/**
 * TEST: Test GuC frequency request functionality
 * Category: Firmware building block
 * Sub-category: GuC
 * Functionality: frequency request
 * Test category: functionality test
 */

#include "igt.h"
#include "lib/igt_syncobj.h"
#include "igt_sysfs.h"

#include "xe_drm.h"
#include "xe/xe_ioctl.h"
#include "xe/xe_query.h"
#include "xe/xe_util.h"

#include <string.h>
#include <sys/time.h>

#define MAX_N_ENGINES 16

/*
 * Too many intermediate components and steps before freq is adjusted
 * Specially if workload is under execution, so let's wait 100 ms.
 */
#define ACT_FREQ_LATENCY_US 100000

static void exec_basic(int fd, struct drm_xe_engine_class_instance *eci,
		       int n_engines, int n_execs)
{
	uint32_t vm;
	uint64_t addr = 0x1a0000;
	struct drm_xe_sync sync[2] = {
		{ .flags = DRM_XE_SYNC_SYNCOBJ | DRM_XE_SYNC_SIGNAL, },
		{ .flags = DRM_XE_SYNC_SYNCOBJ | DRM_XE_SYNC_SIGNAL, },
	};
	struct drm_xe_exec exec = {
		.num_batch_buffer = 1,
		.num_syncs = 2,
		.syncs = to_user_pointer(sync),
	};
	uint32_t engines[MAX_N_ENGINES];
	uint32_t bind_engines[MAX_N_ENGINES];
	uint32_t syncobjs[MAX_N_ENGINES];
	size_t bo_size;
	uint32_t bo = 0;
	struct {
		uint32_t batch[16];
		uint64_t pad;
		uint32_t data;
	} *data;
	int i, b;

	igt_assert(n_engines <= MAX_N_ENGINES);
	igt_assert(n_execs > 0);

	vm = xe_vm_create(fd, DRM_XE_VM_CREATE_ASYNC_BIND_OPS, 0);
	bo_size = sizeof(*data) * n_execs;
	bo_size = ALIGN(bo_size + xe_cs_prefetch_size(fd),
			xe_get_default_alignment(fd));

	bo = xe_bo_create(fd, eci->gt_id, vm, bo_size);
	data = xe_bo_map(fd, bo, bo_size);

	for (i = 0; i < n_engines; i++) {
		engines[i] = xe_engine_create(fd, vm, eci, 0);
		bind_engines[i] = 0;
		syncobjs[i] = syncobj_create(fd, 0);
	};

	sync[0].handle = syncobj_create(fd, 0);

	xe_vm_bind_async(fd, vm, bind_engines[0], bo, 0, addr,
			 bo_size, sync, 1);

	for (i = 0; i < n_execs; i++) {
		uint64_t batch_offset = (char *)&data[i].batch - (char *)data;
		uint64_t batch_addr = addr + batch_offset;
		uint64_t sdi_offset = (char *)&data[i].data - (char *)data;
		uint64_t sdi_addr = addr + sdi_offset;
		int e = i % n_engines;

		b = 0;
		data[i].batch[b++] = MI_STORE_DWORD_IMM_GEN4;
		data[i].batch[b++] = sdi_addr;
		data[i].batch[b++] = sdi_addr >> 32;
		data[i].batch[b++] = 0xc0ffee;
		data[i].batch[b++] = MI_BATCH_BUFFER_END;
		igt_assert(b <= ARRAY_SIZE(data[i].batch));

		sync[0].flags &= ~DRM_XE_SYNC_SIGNAL;
		sync[1].flags |= DRM_XE_SYNC_SIGNAL;
		sync[1].handle = syncobjs[e];

		exec.engine_id = engines[e];
		exec.address = batch_addr;

		if (e != i)
			syncobj_reset(fd, &syncobjs[e], 1);

		xe_exec(fd, &exec);

		igt_assert(syncobj_wait(fd, &syncobjs[e], 1,
					INT64_MAX, 0, NULL));
		igt_assert_eq(data[i].data, 0xc0ffee);
	}

	igt_assert(syncobj_wait(fd, &sync[0].handle, 1, INT64_MAX, 0, NULL));

	sync[0].flags |= DRM_XE_SYNC_SIGNAL;
	xe_vm_unbind_async(fd, vm, bind_engines[0], 0, addr,
			   bo_size, sync, 1);
	igt_assert(syncobj_wait(fd, &sync[0].handle, 1, INT64_MAX, 0, NULL));

	for (i = 0; i < n_execs; i++)
		igt_assert_eq(data[i].data, 0xc0ffee);

	syncobj_destroy(fd, sync[0].handle);
	for (i = 0; i < n_engines; i++) {
		syncobj_destroy(fd, syncobjs[i]);
		xe_engine_destroy(fd, engines[i]);
		if (bind_engines[i])
			xe_engine_destroy(fd, bind_engines[i]);
	}

	munmap(data, bo_size);
	gem_close(fd, bo);
	xe_vm_destroy(fd, vm);
}

static int set_freq(int fd, int gt_id, const char *freq_name, uint32_t freq)
{
	int ret = -EAGAIN;
	char freq_attr[16];
	int gt_fd;

	snprintf(freq_attr, sizeof(freq_attr), "freq_%s", freq_name);
	gt_fd = xe_sysfs_gt_open(fd, gt_id);
	igt_assert(gt_fd >= 0);

	while (ret == -EAGAIN)
		ret = igt_sysfs_printf(gt_fd, freq_attr, "%u", freq);

	close(gt_fd);
	return ret;
}

static uint32_t get_freq(int fd, int gt_id, const char *freq_name)
{
	uint32_t freq;
	int err = -EAGAIN;
	char freq_attr[16];
	int gt_fd;

	snprintf(freq_attr, sizeof(freq_attr), "freq_%s", freq_name);
	gt_fd = xe_sysfs_gt_open(fd, gt_id);
	igt_assert(gt_fd >= 0);

	while (err == -EAGAIN)
		err = igt_sysfs_scanf(gt_fd, freq_attr, "%u", &freq);

	close(gt_fd);
	return freq;
}


/**
 * SUBTEST: freq_basic_api
 * Description: Test basic get and set frequency API
 * Run type: BAT
 */

static void test_freq_basic_api(int fd, int gt_id)
{
	uint32_t rpn = get_freq(fd, gt_id, "rpn");
	uint32_t rpe = get_freq(fd, gt_id, "rpe");
	uint32_t rp0 = get_freq(fd, gt_id, "rp0");

	/*
	 * Negative bound tests
	 * RPn is the floor
	 * RP0 is the ceiling
	 */
	igt_assert(set_freq(fd, gt_id, "min", rpn - 1) < 0);
	igt_assert(set_freq(fd, gt_id, "min", rp0 + 1) < 0);
	igt_assert(set_freq(fd, gt_id, "max", rpn - 1) < 0);
	igt_assert(set_freq(fd, gt_id, "max", rp0 + 1) < 0);

	/* Assert min requests are respected from rp0 to rpn */
	igt_assert(set_freq(fd, gt_id, "min", rp0) > 0);
	igt_assert(get_freq(fd, gt_id, "min") == rp0);
	igt_assert(set_freq(fd, gt_id, "min", rpe) > 0);
	igt_assert(get_freq(fd, gt_id, "min") == rpe);
	igt_assert(set_freq(fd, gt_id, "min", rpn) > 0);
	igt_assert(get_freq(fd, gt_id, "min") == rpn);

	/* Assert max requests are respected from rpn to rp0 */
	igt_assert(set_freq(fd, gt_id, "max", rpn) > 0);
	igt_assert(get_freq(fd, gt_id, "max") == rpn);
	igt_assert(set_freq(fd, gt_id, "max", rpe) > 0);
	igt_assert(get_freq(fd, gt_id, "max") == rpe);
	igt_assert(set_freq(fd, gt_id, "max", rp0) > 0);
	igt_assert(get_freq(fd, gt_id, "max") == rp0);
}

/**
 * SUBTEST: freq_fixed_idle
 * Description: Test fixed frequency request with engine in idle state
 * Run type: BAT
 *
 * SUBTEST: freq_fixed_exec
 * Description: Test fixed frequency request when engine is doing some work
 * Run type: FULL
 */

static void test_freq_fixed(int fd, int gt_id, bool gt_idle)
{
	uint32_t rpn = get_freq(fd, gt_id, "rpn");
	uint32_t rpe = get_freq(fd, gt_id, "rpe");
	uint32_t rp0 = get_freq(fd, gt_id, "rp0");

	igt_debug("Starting testing fixed request\n");

	/*
	 * For Fixed freq we need to set both min and max to the desired value
	 * Then we check if hardware is actually operating at the desired freq
	 * And let's do this for all the 3 known Render Performance (RP) values.
	 */
	igt_assert(set_freq(fd, gt_id, "min", rpn) > 0);
	igt_assert(set_freq(fd, gt_id, "max", rpn) > 0);
	usleep(ACT_FREQ_LATENCY_US);
	igt_assert(get_freq(fd, gt_id, "cur") == rpn);

	if (gt_idle) {
		/* Wait for GT to go in C6 as previous get_freq wakes up GT*/
		igt_assert(igt_wait(xe_is_gt_in_c6(fd, gt_id), 1000, 10));
		igt_assert(get_freq(fd, gt_id, "act") == 0);
	} else {
		igt_assert(get_freq(fd, gt_id, "act") == rpn);
	}

	igt_assert(set_freq(fd, gt_id, "min", rpe) > 0);
	igt_assert(set_freq(fd, gt_id, "max", rpe) > 0);
	usleep(ACT_FREQ_LATENCY_US);
	igt_assert(get_freq(fd, gt_id, "cur") == rpe);

	if (gt_idle) {
		igt_assert(igt_wait(xe_is_gt_in_c6(fd, gt_id), 1000, 10));
		igt_assert(get_freq(fd, gt_id, "act") == 0);
	} else {
		igt_assert(get_freq(fd, gt_id, "act") == rpe);
	}

	igt_assert(set_freq(fd, gt_id, "min", rp0) > 0);
	igt_assert(set_freq(fd, gt_id, "max", rp0) > 0);
	usleep(ACT_FREQ_LATENCY_US);
	/*
	 * It is unlikely that PCODE will *always* respect any request above RPe
	 * So for this level let's only check if GuC PC is doing its job
	 * and respecting our request, by propagating it to the hardware.
	 */
	igt_assert(get_freq(fd, gt_id, "cur") == rp0);

	if (gt_idle) {
		igt_assert(igt_wait(xe_is_gt_in_c6(fd, gt_id), 1000, 10));
		igt_assert(get_freq(fd, gt_id, "act") == 0);
	}

	igt_debug("Finished testing fixed request\n");
}

/**
 * SUBTEST: freq_range_idle
 * Description: Test range frequency request with engine in idle state
 * Run type: BAT
 *
 * SUBTEST: freq_range_exec
 * Description: Test range frequency request when engine is doing some work
 * Run type: FULL
 */

static void test_freq_range(int fd, int gt_id, bool gt_idle)
{
	uint32_t rpn = get_freq(fd, gt_id, "rpn");
	uint32_t rpe = get_freq(fd, gt_id, "rpe");
	uint32_t cur, act;

	igt_debug("Starting testing range request\n");

	igt_assert(set_freq(fd, gt_id, "min", rpn) > 0);
	igt_assert(set_freq(fd, gt_id, "max", rpe) > 0);
	usleep(ACT_FREQ_LATENCY_US);
	cur = get_freq(fd, gt_id, "cur");
	igt_assert(rpn <= cur && cur <= rpe);

	if (gt_idle) {
		igt_assert(igt_wait(xe_is_gt_in_c6(fd, gt_id), 1000, 10));
		igt_assert(get_freq(fd, gt_id, "act") == 0);
	} else {
		act = get_freq(fd, gt_id, "act");
		igt_assert(rpn <= act && act <= rpe);
	}

	igt_debug("Finished testing range request\n");
}

/**
 * SUBTEST: freq_low_max
 * Description: Test frequency request to minimal and maximum values
 * Run type: FULL
 */

static void test_freq_low_max(int fd, int gt_id)
{
	uint32_t rpn = get_freq(fd, gt_id, "rpn");
	uint32_t rpe = get_freq(fd, gt_id, "rpe");

	/*
	 *  When max request < min request, max is ignored and min works like
	 * a fixed one. Let's assert this assumption
	 */
	igt_assert(set_freq(fd, gt_id, "min", rpe) > 0);
	igt_assert(set_freq(fd, gt_id, "max", rpn) > 0);
	usleep(ACT_FREQ_LATENCY_US);
	igt_assert(get_freq(fd, gt_id, "cur") == rpe);
	igt_assert(get_freq(fd, gt_id, "act") == rpe);
}

/**
 * SUBTEST: freq_suspend
 * Description: Check frequency after returning from suspend
 * Run type: FULL
 */

static void test_suspend(int fd, int gt_id)
{
	uint32_t rpn = get_freq(fd, gt_id, "rpn");

	igt_assert(set_freq(fd, gt_id, "min", rpn) > 0);
	igt_assert(set_freq(fd, gt_id, "max", rpn) > 0);
	usleep(ACT_FREQ_LATENCY_US);
	igt_assert(get_freq(fd, gt_id, "cur") == rpn);

	igt_system_suspend_autoresume(SUSPEND_STATE_S3,
				      SUSPEND_TEST_NONE);

	igt_assert(get_freq(fd, gt_id, "min") == rpn);
	igt_assert(get_freq(fd, gt_id, "max") == rpn);
}

/**
 * SUBTEST: freq_reset
 * Description: test frequency reset only once
 * Run type: FULL
 *
 * SUBTEST: freq_reset_multiple
 * Description: test frequency reset multiple times
 * Run type: FULL
 */

static void test_reset(int fd, int gt_id, int cycles)
{
	uint32_t rpn = get_freq(fd, gt_id, "rpn");

	for (int i = 0; i < cycles; i++) {
		igt_assert_f(set_freq(fd, gt_id, "min", rpn) > 0,
			     "Failed after %d good cycles\n", i);
		igt_assert_f(set_freq(fd, gt_id, "max", rpn) > 0,
			     "Failed after %d good cycles\n", i);
		usleep(ACT_FREQ_LATENCY_US);
		igt_assert_f(get_freq(fd, gt_id, "cur") == rpn,
			     "Failed after %d good cycles\n", i);

		xe_force_gt_reset(fd, gt_id);

		igt_assert_f(get_freq(fd, gt_id, "min") == rpn,
			     "Failed after %d good cycles\n", i);
		igt_assert_f(get_freq(fd, gt_id, "max") == rpn,
			     "Failed after %d good cycles\n", i);
	}
}

igt_main
{
	struct drm_xe_engine_class_instance *hwe;
	int fd;
	int gt;
	int ncpus = sysconf(_SC_NPROCESSORS_ONLN);
	uint32_t stash_min;
	uint32_t stash_max;

	igt_fixture {
		fd = drm_open_driver(DRIVER_XE);

		/* The defaults are the same. Stashing the gt0 is enough */
		stash_min = get_freq(fd, 0, "min");
		stash_max = get_freq(fd, 0, "max");
	}

	igt_subtest("freq_basic_api") {
		xe_for_each_gt(fd, gt)
			test_freq_basic_api(fd, gt);
	}

	igt_subtest("freq_fixed_idle") {
		xe_for_each_gt(fd, gt) {
			igt_require(igt_wait(xe_is_gt_in_c6(fd, gt), 1000, 10));
			test_freq_fixed(fd, gt, true);
		}
	}

	igt_subtest("freq_fixed_exec") {
		xe_for_each_gt(fd, gt) {
			xe_for_each_hw_engine(fd, hwe)
				igt_fork(child, ncpus) {
					igt_debug("Execution Started\n");
					exec_basic(fd, hwe, MAX_N_ENGINES, 16);
					igt_debug("Execution Finished\n");
				}
			/* While exec in threads above, let's check the freq */
			test_freq_fixed(fd, gt, false);
			igt_waitchildren();
		}
	}

	igt_subtest("freq_range_idle") {
		xe_for_each_gt(fd, gt) {
			igt_require(igt_wait(xe_is_gt_in_c6(fd, gt), 1000, 10));
			test_freq_range(fd, gt, true);
		}
	}

	igt_subtest("freq_range_exec") {
		xe_for_each_gt(fd, gt) {
			xe_for_each_hw_engine(fd, hwe)
				igt_fork(child, ncpus) {
					igt_debug("Execution Started\n");
					exec_basic(fd, hwe, MAX_N_ENGINES, 16);
					igt_debug("Execution Finished\n");
				}
			/* While exec in threads above, let's check the freq */
			test_freq_range(fd, gt, false);
			igt_waitchildren();
		}
	}

	igt_subtest("freq_low_max") {
		xe_for_each_gt(fd, gt) {
			test_freq_low_max(fd, gt);
		}
	}

	igt_subtest("freq_suspend") {
		xe_for_each_gt(fd, gt) {
			test_suspend(fd, gt);
		}
	}

	igt_subtest("freq_reset") {
		xe_for_each_gt(fd, gt) {
			test_reset(fd, gt, 1);
		}
	}

	igt_subtest("freq_reset_multiple") {
		xe_for_each_gt(fd, gt) {
			test_reset(fd, gt, 50);
		}
	}

	igt_fixture {
		xe_for_each_gt(fd, gt) {
			set_freq(fd, gt, "min", stash_min);
			set_freq(fd, gt, "max", stash_max);
		}
		drm_close_driver(fd);
	}
}

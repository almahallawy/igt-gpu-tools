// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

/**
 * TEST: Basic tests for verify pmu perf interface
 * Category: Hardware building block
 * Sub-category: pmu interface
 * Functionality: pmu
 * Test category: functionality test
 */

#include <fcntl.h>
#include <string.h>

#include "igt.h"
#include "lib/igt_syncobj.h"
#include "lib/intel_reg.h"
#include "lib/igt_perf.h"
#include "xe_drm.h"
#include "xe/xe_ioctl.h"
#include "xe/xe_query.h"
#include "xe/xe_spin.h"

#define MAX_INSTANCE 9

static uint64_t pmu_read(int fd)
{
	uint64_t  data[2];

	igt_assert_eq(read(fd, data, sizeof(data)), sizeof(data));

	return data[0];
}

static int open_pmu(int fd, uint64_t config)
{
	int perf_fd;

	perf_fd = perf_xe_open(fd, config);
	igt_skip_on(perf_fd < 0 && errno == ENODEV);
	igt_assert(perf_fd >= 0);

	return perf_fd;
}

static uint64_t engine_group_get_config(int gt, int class)
{
	uint64_t config;

	switch (class) {
	case DRM_XE_ENGINE_CLASS_COPY:
		config = DRM_XE_PMU_COPY_GROUP_BUSY(gt);
		break;
	case DRM_XE_ENGINE_CLASS_RENDER:
	case DRM_XE_ENGINE_CLASS_COMPUTE:
		config = DRM_XE_PMU_RENDER_GROUP_BUSY(gt);
		break;
	case DRM_XE_ENGINE_CLASS_VIDEO_DECODE:
	case DRM_XE_ENGINE_CLASS_VIDEO_ENHANCE:
		config = DRM_XE_PMU_MEDIA_GROUP_BUSY(gt);
		break;
	}

	return config;
}

/**
 * Test: Basic test for measure the active time when engine of any class active
 *
 * SUBTEST: any-engine-group-busy
 * Description:
 *      Run a test to measure the global activity time by submitting
 *      the WL to all existing engines.
 * Run type: FULL
 *
 */
static void test_any_engine_busyness(int fd, struct drm_xe_engine_class_instance *eci)
{
	uint32_t vm;
	uint64_t addr = 0x1a0000;
	struct drm_xe_sync sync[2] = {
		{ .flags = DRM_XE_SYNC_FLAG_SYNCOBJ | DRM_XE_SYNC_FLAG_SIGNAL, },
		{ .flags = DRM_XE_SYNC_FLAG_SYNCOBJ | DRM_XE_SYNC_FLAG_SIGNAL, },
	};
	struct drm_xe_exec exec = {
		.num_batch_buffer = 1,
		.num_syncs = 2,
		.syncs = to_user_pointer(sync),
	};
	uint32_t exec_queue;
	uint32_t syncobj;
	size_t bo_size;
	uint32_t bo = 0;
	struct xe_spin *spin;
	struct xe_spin_opts spin_opts = { .addr = addr, .preempt = false };
	uint32_t pmu_fd;
	uint64_t count, idle;

	vm = xe_vm_create(fd, DRM_XE_VM_CREATE_FLAG_ASYNC_DEFAULT, 0);
	bo_size = sizeof(*spin);
	bo_size = ALIGN(bo_size + xe_cs_prefetch_size(fd),
			xe_get_default_alignment(fd));

	bo = xe_bo_create(fd, vm, bo_size, vram_if_possible(fd, eci->gt_id));
	spin = xe_bo_map(fd, bo, bo_size);

	exec_queue = xe_exec_queue_create(fd, vm, eci, 0);
	syncobj = syncobj_create(fd, 0);

	sync[0].handle = syncobj_create(fd, 0);
	xe_vm_bind_async(fd, vm, 0, bo, 0, addr, bo_size, sync, 1);

	pmu_fd = open_pmu(fd, DRM_XE_PMU_ANY_ENGINE_GROUP_BUSY(eci->gt_id));
	idle = pmu_read(pmu_fd);
	igt_assert(!idle);

	xe_spin_init(spin, &spin_opts);

	sync[0].flags &= ~DRM_XE_SYNC_FLAG_SIGNAL;
	sync[1].flags |= DRM_XE_SYNC_FLAG_SIGNAL;
	sync[1].handle = syncobj;

	exec.exec_queue_id = exec_queue;
	exec.address = addr;
	xe_exec(fd, &exec);

	xe_spin_wait_started(spin);
	usleep(50000);

	igt_assert(!syncobj_wait(fd, &syncobj, 1, 1, 0, NULL));
	xe_spin_end(spin);

	igt_assert(syncobj_wait(fd, &syncobj, 1, INT64_MAX, 0, NULL));
	igt_assert(syncobj_wait(fd, &sync[0].handle, 1, INT64_MAX, 0, NULL));

	sync[0].flags |= DRM_XE_SYNC_FLAG_SIGNAL;
	xe_vm_unbind_async(fd, vm, 0, 0, addr, bo_size, sync, 1);
	igt_assert(syncobj_wait(fd, &sync[0].handle, 1, INT64_MAX, 0, NULL));

	syncobj_destroy(fd, sync[0].handle);
	syncobj_destroy(fd, syncobj);

	count = pmu_read(pmu_fd);
	igt_assert_lt_u64(idle, count);
	igt_debug("Incrementing counter all-busy-group %ld ns\n", count);

	xe_exec_queue_destroy(fd, exec_queue);
	munmap(spin, bo_size);
	gem_close(fd, bo);
	xe_vm_destroy(fd, vm);
	close(pmu_fd);
}

/**
 * Test: Basic test for measure the active time across engine class
 *
 * SUBTEST: render-busy
 * Description:
 *	Run a test to measure the active engine class time by submitting the
 *	WL to all instances of a class
 * Run type: FULL
 *
 * SUBTEST: compute-busy
 * Description: Run copy-group-busy test
 * Run type: FULL
 *
 * SUBTEST: copy-busy
 * Description: Run copy-group-busy test
 * Run type: FULL
 *
 * SUBTEST: vcs-busy
 * Description: Run copy-group-busy test
 * Run type: FULL
 *
 * SUBTEST: vecs-busy
 * Description: Run copy-group-busy test
 * Run type: FULL
 *
 */

static void test_engine_group_busyness(int fd, int gt, int class, const char *name)
{
	uint32_t vm;
	uint64_t addr = 0x1a0000;
	struct drm_xe_sync sync[2] = {
		{ .flags = DRM_XE_SYNC_FLAG_SYNCOBJ | DRM_XE_SYNC_FLAG_SIGNAL, },
		{ .flags = DRM_XE_SYNC_FLAG_SYNCOBJ | DRM_XE_SYNC_FLAG_SIGNAL, },
	};
	struct drm_xe_exec exec = {
		.num_batch_buffer = 1,
		.num_syncs = 2,
		.syncs = to_user_pointer(sync),
	};
	uint32_t exec_queues[MAX_INSTANCE];
	uint32_t syncobjs[MAX_INSTANCE];
	int    pmu_fd;
	size_t bo_size;
	uint32_t bo = 0, i = 0;
	struct {
		struct xe_spin spin;
	} *data;
	struct xe_spin_opts spin_opts = { .addr = addr, .preempt = false };
	struct drm_xe_engine_class_instance *hwe;
	struct drm_xe_engine_class_instance eci[MAX_INSTANCE];
	int num_placements = 0;
	uint64_t config, count, idle;

	config = engine_group_get_config(gt, class);

	xe_for_each_hw_engine(fd, hwe) {
		if (hwe->engine_class != class || hwe->gt_id != gt)
			continue;

		eci[num_placements++] = *hwe;
	}

	igt_skip_on_f(!num_placements, "Engine class:%d gt:%d not enabled on this platform\n",
		      class, gt);

	vm = xe_vm_create(fd, DRM_XE_VM_CREATE_FLAG_ASYNC_DEFAULT, 0);
	bo_size = sizeof(*data) * num_placements;
	bo_size = ALIGN(bo_size + xe_cs_prefetch_size(fd), xe_get_default_alignment(fd));

	bo = xe_bo_create(fd, vm, bo_size, vram_if_possible(fd, gt));
	data = xe_bo_map(fd, bo, bo_size);

	for (i = 0; i < num_placements; i++) {
		struct drm_xe_exec_queue_create create = {
			.vm_id = vm,
			.width = 1,
			.num_placements = num_placements,
			.instances = to_user_pointer(eci),
		};

		igt_assert_eq(igt_ioctl(fd, DRM_IOCTL_XE_EXEC_QUEUE_CREATE,
					&create), 0);
		exec_queues[i] = create.exec_queue_id;
		syncobjs[i] = syncobj_create(fd, 0);
	};

	sync[0].handle = syncobj_create(fd, 0);
	xe_vm_bind_async(fd, vm, 0, bo, 0, addr, bo_size, sync, 1);

	pmu_fd = open_pmu(fd, config);
	idle = pmu_read(pmu_fd);
	igt_assert(!idle);

	for (i = 0; i < num_placements; i++) {
		spin_opts.addr = addr + (char *)&data[i].spin - (char *)data;
		xe_spin_init(&data[i].spin, &spin_opts);
		sync[0].flags &= ~DRM_XE_SYNC_FLAG_SIGNAL;
		sync[1].flags |= DRM_XE_SYNC_FLAG_SIGNAL;
		sync[1].handle = syncobjs[i];

		exec.exec_queue_id = exec_queues[i];
		exec.address = spin_opts.addr;
		xe_exec(fd, &exec);
		xe_spin_wait_started(&data[i].spin);
	}

	for (i = 0; i < num_placements; i++) {
		xe_spin_end(&data[i].spin);
		igt_assert(syncobj_wait(fd, &syncobjs[i], 1, INT64_MAX, 0,
					NULL));
	}

	igt_assert(syncobj_wait(fd, &sync[0].handle, 1, INT64_MAX, 0, NULL));

	sync[0].flags |= DRM_XE_SYNC_FLAG_SIGNAL;
	xe_vm_unbind_async(fd, vm, 0, 0, addr, bo_size, sync, 1);
	igt_assert(syncobj_wait(fd, &sync[0].handle, 1, INT64_MAX, 0, NULL));


	syncobj_destroy(fd, sync[0].handle);
	for (i = 0; i < num_placements; i++) {
		syncobj_destroy(fd, syncobjs[i]);
		xe_exec_queue_destroy(fd, exec_queues[i]);
	}

	count = pmu_read(pmu_fd);
	igt_assert_lt_u64(idle, count);
	igt_debug("Incrementing counter %s-gt-%d  %ld ns\n", name, gt, count);

	munmap(data, bo_size);
	gem_close(fd, bo);
	xe_vm_destroy(fd, vm);
	close(pmu_fd);
}

igt_main
{
	struct drm_xe_engine_class_instance *hwe;
	const struct section {
		const char *name;
		int class;
	} sections[] = {
		{ "render-busy", DRM_XE_ENGINE_CLASS_RENDER },
		{ "compute-busy", DRM_XE_ENGINE_CLASS_COMPUTE },
		{ "copy-busy", DRM_XE_ENGINE_CLASS_COPY },
		{ "vcs-busy", DRM_XE_ENGINE_CLASS_VIDEO_DECODE },
		{ "vecs-busy", DRM_XE_ENGINE_CLASS_VIDEO_ENHANCE },
		{ NULL },
	};
	int gt;
	int class;
	int fd;

	igt_fixture {
		fd = drm_open_driver(DRIVER_XE);
		xe_device_get(fd);
	}

	for (const struct section *s = sections; s->name; s++) {
		igt_subtest_f("%s", s->name)
			xe_for_each_gt(fd, gt)
				xe_for_each_hw_engine_class(class)
					if (class == s->class)
						test_engine_group_busyness(fd, gt, class, s->name);
	}

	igt_subtest("any-engine-group-busy")
		xe_for_each_hw_engine(fd, hwe)
			test_any_engine_busyness(fd, hwe);

	igt_fixture {
		xe_device_put(fd);
		close(fd);
	}
}

// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "igt.h"
#include "igt_core.h"
#include "igt_device.h"
#include "igt_drm_fdinfo.h"
#include "lib/igt_syncobj.h"
#include "xe_drm.h"
#include "xe/xe_ioctl.h"
#include "xe/xe_query.h"
#include "xe/xe_spin.h"
/**
 * TEST: xe drm fdinfo
 * Description: Read and verify drm client memory consumption using fdinfo
 * Feature: SMI, core
 * Category: Software building block
 * Sub-category: driver
 * Functionality: Per client memory statistics
 * Run type: FULL
 * Test category: SysMan
 *
 * SUBTEST: basic
 * Description: Check if basic fdinfo content is present
 *
 * SUBTEST: drm-total-resident
 * Description: Create and compare total and resident memory consumption by client
 *
 * SUBTEST: drm-shared
 * Description: Create and compare shared memory consumption by client
 *
 * SUBTEST: drm-active
 * Description: Create and compare active memory consumption by client
 */

IGT_TEST_DESCRIPTION("Read and verify drm client memory consumption using fdinfo");

#define BO_SIZE (65536)

/* Subtests */
static void test_active(int fd, struct drm_xe_engine *engine)
{
	struct drm_xe_mem_region *memregion;
	uint64_t memreg = all_memory_regions(fd), region;
	struct drm_client_fdinfo info = { };
	uint32_t vm;
	uint64_t addr = 0x1a0000;
	struct drm_xe_sync sync[2] = {
		{ .type = DRM_XE_SYNC_TYPE_SYNCOBJ, .flags = DRM_XE_SYNC_FLAG_SIGNAL, },
		{ .type = DRM_XE_SYNC_TYPE_SYNCOBJ, .flags = DRM_XE_SYNC_FLAG_SIGNAL, },
	};
	struct drm_xe_exec exec = {
		.num_batch_buffer = 1,
		.num_syncs = 2,
		.syncs = to_user_pointer(sync),
	};
#define N_EXEC_QUEUES   2
	uint32_t exec_queues[N_EXEC_QUEUES];
	uint32_t bind_exec_queues[N_EXEC_QUEUES];
	uint32_t syncobjs[N_EXEC_QUEUES + 1];
	size_t bo_size;
	uint32_t bo = 0;
	struct {
		struct xe_spin spin;
		uint32_t batch[16];
		uint64_t pad;
		uint32_t data;
	} *data;
	struct xe_spin_opts spin_opts = { .preempt = true };
	int i, b, ret;

	vm = xe_vm_create(fd, DRM_XE_VM_CREATE_FLAG_ASYNC_DEFAULT, 0);
	bo_size = sizeof(*data) * N_EXEC_QUEUES;
	bo_size = ALIGN(bo_size + xe_cs_prefetch_size(fd),
			xe_get_default_alignment(fd));

	xe_for_each_mem_region(fd, memreg, region) {
		uint64_t pre_size;

		memregion = xe_mem_region(fd, region);

		ret = igt_parse_drm_fdinfo(fd, &info, NULL, 0, NULL, 0);
		igt_assert_f(ret != 0, "failed with err:%d\n", errno);
		pre_size = info.region_mem[memregion->instance + 1].active;

		bo = xe_bo_create(fd, vm, bo_size, region, 0);
		data = xe_bo_map(fd, bo, bo_size);

		for (i = 0; i < N_EXEC_QUEUES; i++) {
			exec_queues[i] = xe_exec_queue_create(fd, vm,
							      &engine->instance, 0);
			bind_exec_queues[i] = xe_bind_exec_queue_create(fd, vm, 0, true);
			syncobjs[i] = syncobj_create(fd, 0);
		}
		syncobjs[N_EXEC_QUEUES] = syncobj_create(fd, 0);

		sync[0].handle = syncobj_create(fd, 0);
		xe_vm_bind_async(fd, vm, bind_exec_queues[0], bo, 0, addr, bo_size,
				 sync, 1);

		for (i = 0; i < N_EXEC_QUEUES; i++) {
			uint64_t spin_offset = (char *)&data[i].spin - (char *)data;
			uint64_t spin_addr = addr + spin_offset;
			int e = i;

			if (i == 0) {
				/* Cork 1st exec_queue with a spinner */
				spin_opts.addr = spin_addr;
				xe_spin_init(&data[i].spin, &spin_opts);
				exec.exec_queue_id = exec_queues[e];
				exec.address = spin_opts.addr;
				sync[0].flags &= ~DRM_XE_SYNC_FLAG_SIGNAL;
				sync[1].flags |= DRM_XE_SYNC_FLAG_SIGNAL;
				sync[1].handle = syncobjs[e];
				xe_exec(fd, &exec);
				xe_spin_wait_started(&data[i].spin);

				addr += bo_size;
				sync[1].flags &= ~DRM_XE_SYNC_FLAG_SIGNAL;
				sync[1].handle = syncobjs[e];
				xe_vm_bind_async(fd, vm, bind_exec_queues[e], bo, 0, addr,
						 bo_size, sync + 1, 1);
				addr += bo_size;
			} else {
				sync[0].flags |= DRM_XE_SYNC_FLAG_SIGNAL;
				xe_vm_bind_async(fd, vm, bind_exec_queues[e], bo, 0, addr,
						 bo_size, sync, 1);
			}
		}

		b = igt_parse_drm_fdinfo(fd, &info, NULL, 0, NULL, 0);
		igt_assert_f(b != 0, "failed with err:%d\n", errno);

		/* Client memory consumption includes public objects
		 * as well as internal objects hence if bo is active on
		 * N_EXEC_QUEUES active memory consumption should be
		 * > = bo_size
		 */
		igt_info("total:%ld active:%ld pre_size:%ld bo_size:%ld\n",
			 info.region_mem[memregion->instance + 1].total,
			 info.region_mem[memregion->instance + 1].active,
			 pre_size,
			 bo_size);
		igt_assert(info.region_mem[memregion->instance + 1].active >=
			   pre_size + bo_size);

		xe_spin_end(&data[0].spin);

		syncobj_destroy(fd, sync[0].handle);
		sync[0].handle = syncobj_create(fd, 0);
		sync[0].flags |= DRM_XE_SYNC_FLAG_SIGNAL;
		xe_vm_unbind_all_async(fd, vm, 0, bo, sync, 1);
		igt_assert(syncobj_wait(fd, &sync[0].handle, 1, INT64_MAX, 0, NULL));

		syncobj_destroy(fd, sync[0].handle);
		for (i = 0; i < N_EXEC_QUEUES; i++) {
			syncobj_destroy(fd, syncobjs[i]);
			xe_exec_queue_destroy(fd, exec_queues[i]);
			xe_exec_queue_destroy(fd, bind_exec_queues[i]);
		}

		munmap(data, bo_size);
		gem_close(fd, bo);
	}
	xe_vm_destroy(fd, vm);
}

static void test_shared(int xe)
{
	struct drm_xe_mem_region *memregion;
	uint64_t memreg = all_memory_regions(xe), region;
	struct drm_client_fdinfo info = { };
	struct drm_gem_flink flink;
	struct drm_gem_open open_struct;
	uint32_t bo;
	int ret;

	xe_for_each_mem_region(xe, memreg, region) {
		uint64_t pre_size;

		memregion = xe_mem_region(xe, region);

		ret = igt_parse_drm_fdinfo(xe, &info, NULL, 0, NULL, 0);
		igt_assert_f(ret != 0, "failed with err:%d\n", errno);
		pre_size = info.region_mem[memregion->instance + 1].shared;

		bo = xe_bo_create(xe, 0, BO_SIZE, region, 0);

		flink.handle = bo;
		ret = igt_ioctl(xe, DRM_IOCTL_GEM_FLINK, &flink);
		igt_assert_eq(ret, 0);

		open_struct.name = flink.name;
		ret = igt_ioctl(xe, DRM_IOCTL_GEM_OPEN, &open_struct);
		igt_assert_eq(ret, 0);
		igt_assert(open_struct.handle != 0);

		ret = igt_parse_drm_fdinfo(xe, &info, NULL, 0, NULL, 0);
		igt_assert_f(ret != 0, "failed with err:%d\n", errno);

		igt_info("total:%ld pre_size:%ld shared:%ld\n",
			 info.region_mem[memregion->instance + 1].total,
			 pre_size,
			 info.region_mem[memregion->instance + 1].shared);
		igt_assert(info.region_mem[memregion->instance + 1].shared >=
			   pre_size + BO_SIZE);

		gem_close(xe, open_struct.handle);
		gem_close(xe, bo);
	}
}

static void test_total_resident(int xe)
{
	struct drm_xe_mem_region *memregion;
	uint64_t memreg = all_memory_regions(xe), region;
	struct drm_client_fdinfo info = { };
	uint32_t vm;
	uint32_t handle;
	uint64_t addr = 0x1a0000;
	int ret;

	vm = xe_vm_create(xe, DRM_XE_VM_CREATE_FLAG_SCRATCH_PAGE, 0);

	xe_for_each_mem_region(xe, memreg, region) {
		uint64_t pre_size;

		memregion = xe_mem_region(xe, region);

		ret = igt_parse_drm_fdinfo(xe, &info, NULL, 0, NULL, 0);
		igt_assert_f(ret != 0, "failed with err:%d\n", errno);
		pre_size = info.region_mem[memregion->instance + 1].shared;

		handle = xe_bo_create(xe, vm, BO_SIZE, region, 0);
		xe_vm_bind_sync(xe, vm, handle, 0, addr, BO_SIZE);

		ret = igt_parse_drm_fdinfo(xe, &info, NULL, 0, NULL, 0);
		igt_assert_f(ret != 0, "failed with err:%d\n", errno);
		/* currently xe KMD maps memory class system region to
		 * XE_PL_TT thus we need memregion->instance + 1
		 */
		igt_info("total:%ld resident:%ld pre_size:%ld bo_size:%d\n",
			 info.region_mem[memregion->instance + 1].total,
			 info.region_mem[memregion->instance + 1].resident,
			 pre_size, BO_SIZE);
		/* Client memory consumption includes public objects
		 * as well as internal objects hence it should be
		 * >= pre_size + BO_SIZE
		 */
		igt_assert(info.region_mem[memregion->instance + 1].total >=
			   pre_size + BO_SIZE);
		igt_assert(info.region_mem[memregion->instance + 1].resident >=
			   pre_size + BO_SIZE);
		xe_vm_unbind_sync(xe, vm, 0, addr, BO_SIZE);
		gem_close(xe, handle);
	}

	xe_vm_destroy(xe, vm);
}

static void basic(int xe)
{
	struct drm_xe_mem_region *memregion;
	uint64_t memreg = all_memory_regions(xe), region;
	struct drm_client_fdinfo info = { };
	unsigned int ret;

	ret = igt_parse_drm_fdinfo(xe, &info, NULL, 0, NULL, 0);
	igt_assert_f(ret != 0, "failed with err:%d\n", errno);

	igt_assert(!strcmp(info.driver, "xe"));

	xe_for_each_mem_region(xe, memreg, region) {
		memregion = xe_mem_region(xe, region);
		igt_assert(info.region_mem[memregion->instance + 1].total >=
			   0);
		igt_assert(info.region_mem[memregion->instance + 1].shared >=
			   0);
		igt_assert(info.region_mem[memregion->instance + 1].resident >=
			   0);
		igt_assert(info.region_mem[memregion->instance + 1].active >=
			   0);
		if (memregion->instance == 0)
			igt_assert(info.region_mem[memregion->instance].purgeable >=
				   0);
	}
}

igt_main
{
	int xe;

	igt_fixture {
		struct drm_client_fdinfo info = { };

		xe = drm_open_driver(DRIVER_XE);
		igt_require_xe(xe);
		igt_require(igt_parse_drm_fdinfo(xe, &info, NULL, 0, NULL, 0));
	}

	igt_describe("Check if basic fdinfo content is present");
	igt_subtest("basic")
		basic(xe);

	igt_describe("Create and compare total and resident memory consumption by client");
	igt_subtest("drm-total-resident")
		test_total_resident(xe);

	igt_describe("Create and compare shared memory consumption by client");
	igt_subtest("drm-shared")
		test_shared(xe);

	igt_describe("Create and compare active memory consumption by client");
	igt_subtest("drm-active")
		test_active(xe, xe_engine(xe, 0));

	igt_fixture {
		drm_close_driver(xe);
	}
}

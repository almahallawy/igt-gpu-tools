// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

/**
 * TEST: Check bo create ioctl
 * Category: Software building block
 * Sub-category: uapi
 */

#include <string.h>

#include "igt.h"
#include "xe_drm.h"
#include "xe/xe_ioctl.h"
#include "xe/xe_query.h"

#define PAGE_SIZE 0x1000

static int __create_bo(int fd, uint32_t vm, uint64_t size, uint32_t flags,
		       uint32_t *handlep)
{
	struct drm_xe_gem_create create = {
		.vm_id = vm,
		.size = size,
		.flags = flags,
	};
	int ret = 0;

	igt_assert(handlep);

	if (igt_ioctl(fd, DRM_IOCTL_XE_GEM_CREATE, &create)) {
		ret = -errno;
		errno = 0;
	}
	*handlep = create.handle;

	return ret;
}

/**
 * SUBTEST: create-invalid-size
 * Functionality: ioctl
 * Test category: negative test
 * Description: Verifies xe bo create returns expected error code on invalid
 *              buffer sizes.
 */
static void create_invalid_size(int fd)
{
	struct drm_xe_query_mem_region *memregion;
	uint64_t memreg = all_memory_regions(fd), region;
	uint32_t vm;
	uint32_t handle;
	int ret;

	vm = xe_vm_create(fd, DRM_XE_VM_CREATE_FLAG_ASYNC_DEFAULT, 0);

	xe_for_each_mem_region(fd, memreg, region) {
		memregion = xe_mem_region(fd, region);

		/* first try, use half of possible min page size */
		ret = __create_bo(fd, vm, memregion->min_page_size >> 1,
				  region, &handle);
		if (!ret) {
			gem_close(fd, handle);
			xe_vm_destroy(fd, vm);
		}
		igt_assert_eq(ret, -EINVAL);

		/*
		 * second try, add page size to min page size if it is
		 * bigger than page size.
		 */
		if (memregion->min_page_size > PAGE_SIZE) {
			ret = __create_bo(fd, vm,
					  memregion->min_page_size + PAGE_SIZE,
					  region, &handle);
			if (!ret) {
				gem_close(fd, handle);
				xe_vm_destroy(fd, vm);
			}
			igt_assert_eq(ret, -EINVAL);
		}
	}

	xe_vm_destroy(fd, vm);
}

enum exec_queue_destroy {
	NOLEAK,
	LEAK
};

static uint32_t __xe_exec_queue_create(int fd, uint32_t vm,
				   struct drm_xe_engine_class_instance *instance,
				   uint64_t ext,
				   uint32_t *exec_queuep)
{
	struct drm_xe_exec_queue_create create = {
		.extensions = ext,
		.vm_id = vm,
		.width = 1,
		.num_placements = 1,
		.instances = to_user_pointer(instance),
	};
	int err = 0;

	if (igt_ioctl(fd, DRM_IOCTL_XE_EXEC_QUEUE_CREATE, &create) == 0) {
		*exec_queuep = create.exec_queue_id;
	} else {
		igt_warn("Can't create exec_queue, errno: %d\n", errno);
		err = -errno;
		igt_assume(err);
	}
	errno = 0;

	return err;
}

#define MAXEXECQUEUES 2048
#define MAXTIME 5

/**
 * SUBTEST: create-execqueues-%s
 * Functionality: exequeues creation time
 * Description: Check process ability of multiple exec_queues creation
 * Test category: functionality test
 *
 * arg[1]:
 *
 * @noleak:				destroy exec_queues in the code
 * @leak:				destroy exec_queues in close() path
 */
static void create_execqueues(int fd, enum exec_queue_destroy ed)
{
	struct timespec tv = { };
	uint32_t num_engines, exec_queues_per_process, vm;
	int nproc = sysconf(_SC_NPROCESSORS_ONLN), seconds;

	fd = drm_reopen_driver(fd);
	num_engines = xe_number_hw_engines(fd);
	vm = xe_vm_create(fd, DRM_XE_VM_CREATE_FLAG_ASYNC_DEFAULT, 0);

	exec_queues_per_process = max_t(uint32_t, 1, MAXEXECQUEUES / nproc);
	igt_debug("nproc: %u, exec_queues per process: %u\n", nproc, exec_queues_per_process);

	igt_nsec_elapsed(&tv);

	igt_fork(n, nproc) {
		struct drm_xe_engine_class_instance *hwe;
		uint32_t exec_queue, exec_queues[exec_queues_per_process];
		int idx, err, i;

		srandom(n);

		for (i = 0; i < exec_queues_per_process; i++) {
			idx = rand() % num_engines;
			hwe = xe_hw_engine(fd, idx);
			err = __xe_exec_queue_create(fd, vm, hwe, 0, &exec_queue);
			igt_debug("[%2d] Create exec_queue: err=%d, exec_queue=%u [idx = %d]\n",
				  n, err, exec_queue, i);
			if (err)
				break;

			if (ed == NOLEAK)
				exec_queues[i] = exec_queue;
		}

		if (ed == NOLEAK) {
			while (--i >= 0) {
				igt_debug("[%2d] Destroy exec_queue: %u\n", n, exec_queues[i]);
				xe_exec_queue_destroy(fd, exec_queues[i]);
			}
		}
	}
	igt_waitchildren();

	xe_vm_destroy(fd, vm);
	drm_close_driver(fd);

	seconds = igt_seconds_elapsed(&tv);
	igt_assert_f(seconds < MAXTIME,
		     "Creating %d exec_queues tooks too long: %d [limit: %d]\n",
		     MAXEXECQUEUES, seconds, MAXTIME);
}

/**
 * SUBTEST: create-massive-size
 * Functionality: ioctl
 * Test category: functionality test
 * Description: Verifies xe bo create returns expected error code on massive
 *              buffer sizes.
 *
 * SUBTEST: multigpu-create-massive-size
 * Functionality: ioctl
 * Test category: functionality test
 * Feature: multigpu
 * Description: Verifies xe bo create with massive buffer sizes runs correctly
 *		on two or more GPUs.
 */
static void create_massive_size(int fd)
{
	uint64_t memreg = all_memory_regions(fd), region;
	uint32_t vm;
	uint32_t handle;
	int ret;

	vm = xe_vm_create(fd, DRM_XE_VM_CREATE_FLAG_ASYNC_DEFAULT, 0);

	xe_for_each_mem_region(fd, memreg, region) {
		ret = __create_bo(fd, vm, -1ULL << 32, region, &handle);
		igt_assert_eq(ret, -ENOSPC);
	}
}

igt_main
{
	int xe;

	igt_fixture
		xe = drm_open_driver(DRIVER_XE);

	igt_subtest("create-invalid-size") {
		create_invalid_size(xe);
	}

	igt_subtest("create-execqueues-noleak")
		create_execqueues(xe, NOLEAK);

	igt_subtest("create-execqueues-leak")
		create_execqueues(xe, LEAK);

	igt_subtest("create-massive-size") {
		create_massive_size(xe);
	}

	igt_subtest("multigpu-create-massive-size") {
		int gpu_count = drm_prepare_filtered_multigpu(DRIVER_XE);

		igt_require(xe > 0);
		igt_require(gpu_count >= 2);
		igt_multi_fork(child, gpu_count) {
			int gpu_fd;

			gpu_fd = drm_open_filtered_card(child);
			igt_assert_f(gpu_fd > 0, "cannot open gpu-%d, errno=%d\n", child, errno);
			igt_assert(is_xe_device(gpu_fd));

			create_massive_size(gpu_fd);
			drm_close_driver(gpu_fd);
		}
		igt_waitchildren();
	}


	igt_fixture
		drm_close_driver(xe);
}

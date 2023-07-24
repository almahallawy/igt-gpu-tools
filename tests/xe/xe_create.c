// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

/**
 * TEST: Check bo create ioctl
 * Category: Software building block
 * Sub-category: uapi
 * Functionality: device
 * Test category: functionality test
 * Run type: BAT
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

	vm = xe_vm_create(fd, DRM_XE_VM_CREATE_ASYNC_BIND_OPS, 0);

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

enum engine_destroy {
	NOLEAK,
	LEAK
};

static uint32_t __xe_engine_create(int fd, uint32_t vm,
				   struct drm_xe_engine_class_instance *instance,
				   uint64_t ext,
				   uint32_t *enginep)
{
	struct drm_xe_engine_create create = {
		.extensions = ext,
		.vm_id = vm,
		.width = 1,
		.num_placements = 1,
		.instances = to_user_pointer(instance),
	};
	int err = 0;

	if (igt_ioctl(fd, DRM_IOCTL_XE_ENGINE_CREATE, &create) == 0) {
		*enginep = create.engine_id;
	} else {
		igt_warn("Can't create engine, errno: %d\n", errno);
		err = -errno;
		igt_assume(err);
	}
	errno = 0;

	return err;
}

#define MAXENGINES 2048
#define MAXTIME 5

/**
 * SUBTEST: create-engines-%s
 * Description: Check process ability of multiple engines creation
 * Run type: FULL
 *
 * arg[1]:
 *
 * @noleak:				destroy engines in the code
 * @leak:				destroy engines in close() path
 */
static void create_engines(int fd, enum engine_destroy ed)
{
	struct timespec tv = { };
	uint32_t num_engines, engines_per_process, vm;
	int nproc = sysconf(_SC_NPROCESSORS_ONLN), seconds;

	fd = drm_reopen_driver(fd);
	num_engines = xe_number_hw_engines(fd);
	vm = xe_vm_create(fd, DRM_XE_VM_CREATE_ASYNC_BIND_OPS, 0);

	engines_per_process = max_t(uint32_t, 1, MAXENGINES / nproc);
	igt_debug("nproc: %u, engines per process: %u\n", nproc, engines_per_process);

	igt_nsec_elapsed(&tv);

	igt_fork(n, nproc) {
		struct drm_xe_engine_class_instance *hwe;
		uint32_t engine, engines[engines_per_process];
		int idx, err, i;

		srandom(n);

		for (i = 0; i < engines_per_process; i++) {
			idx = rand() % num_engines;
			hwe = xe_hw_engine(fd, idx);
			err = __xe_engine_create(fd, vm, hwe, 0, &engine);
			igt_debug("[%2d] Create engine: err=%d, engine=%u [idx = %d]\n",
				  n, err, engine, i);
			if (err)
				break;

			if (ed == NOLEAK)
				engines[i] = engine;
		}

		if (ed == NOLEAK) {
			while (--i >= 0) {
				igt_debug("[%2d] Destroy engine: %u\n", n, engines[i]);
				xe_engine_destroy(fd, engines[i]);
			}
		}
	}
	igt_waitchildren();

	xe_vm_destroy(fd, vm);
	drm_close_driver(fd);

	seconds = igt_seconds_elapsed(&tv);
	igt_assert_f(seconds < MAXTIME,
		     "Creating %d engines tooks too long: %d [limit: %d]\n",
		     MAXENGINES, seconds, MAXTIME);
}

/**
 * SUBTEST: create-massive-size
 * Description: Verifies xe bo create returns expected error code on massive
 *              buffer sizes.
 */
static void create_massive_size(int fd)
{
	uint64_t memreg = all_memory_regions(fd), region;
	uint32_t vm;
	uint32_t handle;
	int ret;

	vm = xe_vm_create(fd, DRM_XE_VM_CREATE_ASYNC_BIND_OPS, 0);

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

	igt_subtest("create-engines-noleak")
		create_engines(xe, NOLEAK);

	igt_subtest("create-engines-leak")
		create_engines(xe, LEAK);

	igt_subtest("create-massive-size") {
		create_massive_size(xe);
	}

	igt_fixture
		drm_close_driver(xe);
}

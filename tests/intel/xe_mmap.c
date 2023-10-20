// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

/**
 * TEST: Test if the driver is capable of doing mmap on different memory regions
 * Category: Software building block
 * Sub-category: VMA
 * Functionality: mmap
 */

#include "igt.h"

#include "xe_drm.h"
#include "xe/xe_ioctl.h"
#include "xe/xe_query.h"

#include <setjmp.h>
#include <signal.h>
#include <string.h>

/**
 * SUBTEST: system
 * Test category: functionality test
 * Description: Test mmap on system memory
 */

/**
 * SUBTEST: small-bar
 * Description: Sanity check mmap behaviour on small-bar systems
 * GPU requirements: GPU needs to have dedicated VRAM and using small-bar
 * Test category: functionality test
 */

/**
 * SUBTEST: %s
 * Description: Test mmap on %arg[1] memory
 * GPU requirements: GPU needs to have dedicated VRAM
 * Test category: functionality test
 *
 * arg[1]:
 *
 * @vram:		vram
 * @vram-system:	system vram
 */
static void
test_mmap(int fd, uint32_t flags)
{
	uint32_t bo;
	void *map;

	igt_require_f(flags, "Device doesn't support such memory region\n");

	bo = xe_bo_create_flags(fd, 0, 4096, flags);

	map = xe_bo_map(fd, bo, 4096);
	strcpy(map, "Write some data to the BO!");

	munmap(map, 4096);

	gem_close(fd, bo);
}

/**
 * SUBTEST: bad-flags
 * Description: Test mmap offset with bad flags.
 * Test category: negative test
 *
 */
static void test_bad_flags(int fd)
{
	uint64_t size = xe_get_default_alignment(fd);
	struct drm_xe_gem_mmap_offset mmo = {
		.handle = xe_bo_create_flags(fd, 0, size,
					     visible_vram_if_possible(fd, 0)),
		.flags = -1u,
	};

	do_ioctl_err(fd, DRM_IOCTL_XE_GEM_MMAP_OFFSET, &mmo, EINVAL);
	gem_close(fd, mmo.handle);
}

/**
 * SUBTEST: bad-extensions
 * Description: Test mmap offset with bad extensions.
 * Test category: negative test
 *
 */
static void test_bad_extensions(int fd)
{
	uint64_t size = xe_get_default_alignment(fd);
	struct xe_user_extension ext;
	struct drm_xe_gem_mmap_offset mmo = {
		.handle = xe_bo_create_flags(fd, 0, size,
					     visible_vram_if_possible(fd, 0)),
	};

	mmo.extensions = to_user_pointer(&ext);
	ext.name = -1;

	do_ioctl_err(fd, DRM_IOCTL_XE_GEM_MMAP_OFFSET, &mmo, EINVAL);
	gem_close(fd, mmo.handle);
}

/**
 * SUBTEST: bad-object
 * Description: Test mmap offset with bad object.
 * Test category: negative test
 *
 */
static void test_bad_object(int fd)
{
	uint64_t size = xe_get_default_alignment(fd);
	struct drm_xe_gem_mmap_offset mmo = {
		.handle = xe_bo_create_flags(fd, 0, size,
					     visible_vram_if_possible(fd, 0)),
	};

	mmo.handle = 0xdeadbeef;
	do_ioctl_err(fd, DRM_IOCTL_XE_GEM_MMAP_OFFSET, &mmo, ENOENT);
}

static jmp_buf jmp;

__noreturn static void sigtrap(int sig)
{
	siglongjmp(jmp, sig);
}

static void trap_sigbus(uint32_t *ptr)
{
	sighandler_t old_sigbus;

	old_sigbus = signal(SIGBUS, sigtrap);
	switch (sigsetjmp(jmp, SIGBUS)) {
	case SIGBUS:
		break;
	case 0:
		*ptr = 0xdeadbeaf;
	default:
		igt_assert(!"reached");
		break;
	}
	signal(SIGBUS, old_sigbus);
}

/**
 * SUBTEST: small-bar
 * Description: Test mmap behaviour on small-bar systems.
 * Test category: functionality test
 *
 */
static void test_small_bar(int fd)
{
	uint32_t visible_size = xe_visible_vram_size(fd, 0);
	uint32_t bo;
	uint64_t mmo;
	uint32_t *map;

	/* 2BIG invalid case */
	igt_assert_neq(__xe_bo_create_flags(fd, 0, visible_size + 4096,
					    visible_vram_memory(fd, 0), &bo),
		       0);

	/* Normal operation */
	bo = xe_bo_create_flags(fd, 0, visible_size / 4,
				visible_vram_memory(fd, 0));
	mmo = xe_bo_mmap_offset(fd, bo);
	map = mmap(NULL, 4096, PROT_WRITE, MAP_SHARED, fd, mmo);
	igt_assert(map != MAP_FAILED);

	map[0] = 0xdeadbeaf;

	munmap(map, 4096);
	gem_close(fd, bo);

	/* Normal operation with system memory spilling */
	bo = xe_bo_create_flags(fd, 0, visible_size,
				visible_vram_memory(fd, 0) |
				system_memory(fd));
	mmo = xe_bo_mmap_offset(fd, bo);
	map = mmap(NULL, 4096, PROT_WRITE, MAP_SHARED, fd, mmo);
	igt_assert(map != MAP_FAILED);

	map[0] = 0xdeadbeaf;

	munmap(map, 4096);
	gem_close(fd, bo);

	/* Bogus operation with SIGBUS */
	bo = xe_bo_create_flags(fd, 0, visible_size + 4096,
				vram_memory(fd, 0));
	mmo = xe_bo_mmap_offset(fd, bo);
	map = mmap(NULL, 4096, PROT_WRITE, MAP_SHARED, fd, mmo);
	igt_assert(map != MAP_FAILED);

	trap_sigbus(map);
	gem_close(fd, bo);
}

static void assert_caching(int fd, uint64_t flags, uint16_t cpu_caching, bool fail)
{
	uint64_t size = xe_get_default_alignment(fd);
	uint64_t mmo;
	uint32_t handle;
	uint32_t *map;
	bool ret;

	ret = __xe_bo_create_caching(fd, 0, size, flags, cpu_caching, &handle);
	igt_assert(ret == fail);

	if (fail)
		return;

	mmo = xe_bo_mmap_offset(fd, handle);
	map = mmap(NULL, size, PROT_WRITE, MAP_SHARED, fd, mmo);
	igt_assert(map != MAP_FAILED);
	map[0] = 0xdeadbeaf;
	gem_close(fd, handle);
}

/**
 * SUBTEST: cpu-caching
 * Description: Test explicit cpu_caching, including mmap behaviour.
 * Test category: functionality test
 */
static void test_cpu_caching(int fd)
{
	if (vram_memory(fd, 0)) {
		assert_caching(fd, vram_memory(fd, 0),
			       DRM_XE_GEM_CPU_CACHING_WC, false);
		assert_caching(fd, vram_memory(fd, 0) | system_memory(fd),
			       DRM_XE_GEM_CPU_CACHING_WC, false);

		assert_caching(fd, vram_memory(fd, 0),
			       DRM_XE_GEM_CPU_CACHING_WB, true);
		assert_caching(fd, vram_memory(fd, 0) | system_memory(fd),
			       DRM_XE_GEM_CPU_CACHING_WB, true);
	}

	assert_caching(fd, system_memory(fd), DRM_XE_GEM_CPU_CACHING_WB, false);
	assert_caching(fd, system_memory(fd), DRM_XE_GEM_CPU_CACHING_WC, false);

	assert_caching(fd, system_memory(fd), -1, true);
	assert_caching(fd, system_memory(fd), 0, true);
	assert_caching(fd, system_memory(fd), DRM_XE_GEM_CPU_CACHING_WC + 1, true);
}

igt_main
{
	int fd;

	igt_fixture
		fd = drm_open_driver(DRIVER_XE);

	igt_subtest("system")
		test_mmap(fd, system_memory(fd));

	igt_subtest("vram")
		test_mmap(fd, visible_vram_memory(fd, 0));

	igt_subtest("vram-system")
		test_mmap(fd, visible_vram_memory(fd, 0) | system_memory(fd));

	igt_subtest("bad-flags")
		test_bad_flags(fd);

	igt_subtest("bad-extensions")
		test_bad_extensions(fd);

	igt_subtest("bad-object")
		test_bad_object(fd);

	igt_subtest("small-bar") {
		igt_require(xe_visible_vram_size(fd, 0));
		igt_require(xe_visible_vram_size(fd, 0) < xe_vram_size(fd, 0));
		test_small_bar(fd);
	}

	igt_subtest("cpu-caching")
		test_cpu_caching(fd);

	igt_fixture
		drm_close_driver(fd);
}

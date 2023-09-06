// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

/**
 * TEST: Verify physical_vram_size_bytes of each tiles
 * Category: Software building block
 * SUBTEST: physical_vram_size_bytes
 * Functionality: Vram
 * Sub-category: Total vram per tile
 * Run type: FULL
 * Test category: functionality test
 * Description:
 *             Read sysfs entry for physical_vram_size_bytes and compare with
 *             vram size. physical_vram_size_bytes should be more than vram size.
 */

#include <string.h>
#include <sys/time.h>

#include "igt.h"
#include "igt_sysfs.h"

#include "xe_drm.h"
#include "xe/xe_ioctl.h"
#include "xe/xe_query.h"

static void test_vram_physical_vram_size_bytes(int tile_fd, int tile_num, u64 vram_size)
{
	u64 physical_vram_size_bytes;

	igt_assert(igt_sysfs_scanf(tile_fd, "physical_vram_size_bytes",
				   "%lx", &physical_vram_size_bytes) > 0);
	igt_assert_lt_u64(vram_size, physical_vram_size_bytes);
}

igt_main
{
	int fd, tilefd, tile;
	u64 vram_size;

	igt_fixture {
		fd = drm_open_driver(DRIVER_XE);
		xe_device_get(fd);
	}

	igt_subtest("physical_vram_size_bytes") {
		igt_require(xe_has_vram(fd));
		for_each_sysfs_tile_dirfd(fd, tilefd, tile) {
			vram_size = xe_vram_size(fd, tile);
			test_vram_physical_vram_size_bytes(tilefd, tile, vram_size);
		}
	}

	igt_fixture {
		xe_device_put(fd);
		close(fd);
	}
}

// SPDX-License-Identifier: MIT
/*
 * Copyright 2014 Advanced Micro Devices, Inc.
 * Copyright 2022 Advanced Micro Devices, Inc.
 * Copyright 2023 Advanced Micro Devices, Inc.
 */

#include "lib/amdgpu/amd_memory.h"
#include "lib/amdgpu/amd_command_submission.h"
#include "lib/amdgpu/amd_deadlock_helpers.h"

static void
amdgpu_deadlock_gfx(amdgpu_device_handle device_handle)
{
	amdgpu_wait_memory_helper(device_handle, AMDGPU_HW_IP_GFX);
}

static void
amdgpu_deadlock_compute(amdgpu_device_handle device_handle)
{
	amdgpu_wait_memory_helper(device_handle, AMDGPU_HW_IP_COMPUTE);
}

static void
amdgpu_deadlock_sdma(amdgpu_device_handle device_handle)
{
	amdgpu_wait_memory_helper(device_handle, AMDGPU_HW_IP_DMA);
}

static void
amdgpu_gfx_illegal_reg_access(amdgpu_device_handle device_handle)
{
	bad_access_helper(device_handle, 1, AMDGPU_HW_IP_GFX);
}

static void
amdgpu_gfx_illegal_mem_access(amdgpu_device_handle device_handle)
{
	bad_access_helper(device_handle, 0, AMDGPU_HW_IP_GFX);
}

igt_main
{
	amdgpu_device_handle device;
	struct amdgpu_gpu_info gpu_info = {0};
	int fd = -1;
	int r;

	igt_fixture {
		uint32_t major, minor;
		int err;

		fd = drm_open_driver(DRIVER_AMDGPU);

		err = amdgpu_device_initialize(fd, &major, &minor, &device);
		igt_require(err == 0);

		igt_info("Initialized amdgpu, driver version %d.%d\n",
			 major, minor);

		r = amdgpu_query_gpu_info(device, &gpu_info);
		igt_assert_eq(r, 0);
		r = setup_amdgpu_ip_blocks(major, minor, &gpu_info, device);
		igt_assert_eq(r, 0);

	}
	igt_describe("Test-GPU-reset-by-flooding-sdma-ring-with-jobs");
	igt_subtest("amdgpu-deadlock-sdma")
	amdgpu_deadlock_sdma(device);

	igt_describe("Test-GPU-reset-by-access-gfx-illegal-reg");
	igt_subtest("amdgpu-gfx-illegal-reg-access")
	amdgpu_gfx_illegal_reg_access(device);

	igt_describe("Test-GPU-reset-by-access-gfx-illegal-mem-addr");
	igt_subtest("amdgpu-gfx-illegal-mem-access")
	amdgpu_gfx_illegal_mem_access(device);

	igt_describe("Test-GPU-reset-by-flooding-gfx-ring-with-jobs");
	igt_subtest("amdgpu-deadlock-gfx")
	amdgpu_deadlock_gfx(device);

	igt_describe("Test-GPU-reset-by-flooding-compute-ring-with-jobs");
	igt_subtest("amdgpu-deadlock-compute")
	amdgpu_deadlock_compute(device);

	igt_fixture {
		amdgpu_device_deinitialize(device);
		drm_close_driver(fd);
	}
}

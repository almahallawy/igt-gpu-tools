// SPDX-License-Identifier: MIT
/*
 * Copyright 2014 Advanced Micro Devices, Inc.
 * Copyright 2022 Advanced Micro Devices, Inc.
 * Copyright 2023 Advanced Micro Devices, Inc.
 */

#include "lib/amdgpu/amd_memory.h"
#include "lib/amdgpu/amd_command_submission.h"
#include "lib/amdgpu/amd_dispatch.h"

static void
amdgpu_dispatch_hang_slow_gfx(amdgpu_device_handle device_handle)
{
	amdgpu_dispatch_hang_slow_helper(device_handle, AMDGPU_HW_IP_GFX);
}

static void
amdgpu_dispatch_hang_slow_compute(amdgpu_device_handle device_handle)
{
	amdgpu_dispatch_hang_slow_helper(device_handle, AMDGPU_HW_IP_COMPUTE);
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
	igt_describe("Test-GPU-reset-using-a-binary-shader-to-hang-the-job-on-compute-ring");
	igt_subtest("dispatch-hang-slow-compute")
	amdgpu_dispatch_hang_slow_compute(device);

	igt_describe("Test-GPU-reset-using-a-binary-shader-to-hang-the-job-on-gfx-ring");
	igt_subtest("dispatch-hang-slow-gfx")
	amdgpu_dispatch_hang_slow_gfx(device);

	igt_fixture {
		amdgpu_device_deinitialize(device);
		drm_close_driver(fd);
	}
}

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
	bool arr_cap[AMD_IP_MAX] = {0};

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
		asic_rings_readness(device, 1, arr_cap);

	}
	igt_describe("Test-GPU-reset-using-a-binary-shader-to-hang-the-job-on-compute-ring");
	igt_subtest_with_dynamic("amdgpu-dispatch-test-compute-with-IP-COMPUTE") {
		if (arr_cap[AMD_IP_COMPUTE]) {
			igt_dynamic_f("amdgpu-dispatch-test-compute")
			amdgpu_dispatch_hang_slow_compute(device);
		}
	}

	igt_describe("Test-GPU-reset-using-a-binary-shader-to-hang-the-job-on-gfx-ring");
	igt_subtest_with_dynamic("amdgpu-dispatch-test-gfx-with-IP-GFX") {
		if (arr_cap[AMD_IP_GFX]) {
			igt_dynamic_f("amdgpu-dispatch-test-gfx")
			 amdgpu_dispatch_hang_slow_gfx(device);
		}
	}

	igt_fixture {
		amdgpu_device_deinitialize(device);
		drm_close_driver(fd);
	}
}

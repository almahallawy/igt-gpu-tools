// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

/**
 * TEST: Check compute-related functionality
 * Category: Hardware building block
 * Sub-category: compute
 * Test category: functionality test
 * Run type: BAT
 */

#include <string.h>

#include "igt.h"
#include "intel_compute.h"

/**
 * SUBTEST: compute-square
 * GPU requirement: only works on TGL
 * Description:
 *	Run an openCL Kernel that returns output[i] = input[i] * input[i],
 *	for an input dataset..
 * Functionality: compute openCL kernel
 * TODO: extend test to cover other platforms
 */
static void
test_compute_square(int fd)
{
	igt_require_f(run_intel_compute_kernel(fd), "GPU not supported\n");
}

igt_main
{
	int i915;

	igt_fixture
		i915 = drm_open_driver(DRIVER_INTEL);

	igt_subtest("compute-square")
		test_compute_square(i915);

	igt_fixture
		drm_close_driver(i915);
}

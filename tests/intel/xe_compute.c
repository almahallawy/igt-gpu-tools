// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

/**
 * TEST: Check compute-related functionality
 * Category: Hardware building block
 * Sub-category: compute
 * Test category: functionality test
 */

#include <string.h>

#include "igt.h"
#include "intel_compute.h"
#include "xe/xe_query.h"

/**
 * SUBTEST: compute-square
 * GPU requirement: TGL, PVC, LNL
 * Description:
 *	Run an openCL Kernel that returns output[i] = input[i] * input[i],
 *	for an input dataset..
 * Functionality: compute openCL kernel
 */
static void
test_compute_square(int fd)
{
	igt_require_f(run_intel_compute_kernel(fd), "GPU not supported\n");
}

igt_main
{
	int xe;

	igt_fixture
		xe = drm_open_driver(DRIVER_XE);

	igt_subtest("compute-square")
		test_compute_square(xe);

	igt_fixture
		drm_close_driver(xe);
}

/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 *
 * Authors:
 *    Francois Dugast <francois.dugast@intel.com>
 */

#ifndef INTEL_COMPUTE_H
#define INTEL_COMPUTE_H

#include "xe_drm.h"

/*
 * OpenCL Kernels are generated using:
 *
 * GPU=tgllp &&                                                         \
 *      ocloc -file opencl/compute_square_kernel.cl -device $GPU &&     \
 *      xxd -i compute_square_kernel_Gen12LPlp.bin
 *
 * For each GPU model desired. A list of supported models can be obtained with: ocloc compile --help
 */

struct intel_compute_kernels {
	int ip_ver;
	unsigned int size;
	const unsigned char *kernel;
};

extern const struct intel_compute_kernels intel_compute_square_kernels[];

bool run_intel_compute_kernel(int fd);
bool xe_run_intel_compute_kernel_on_engine(int fd, struct drm_xe_engine_class_instance *eci);

#endif	/* INTEL_COMPUTE_H */

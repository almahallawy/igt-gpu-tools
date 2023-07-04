/* SPDX-License-Identifier: MIT
 * Copyright 2023 Advanced Micro Devices, Inc.
 * Copyright 2014 Advanced Micro Devices, Inc.
 */

#include <amdgpu.h>
#include "amdgpu_drm.h"

#include "igt.h"
#include "amd_mmd_decode_messages.h"
#include "amd_mmd_util_math.h"
#include "amd_memory.h"
#include "amd_mmd_frame.h"
#include "amd_mmd_uve_ib.h"


#define UVD_4_0_GPCOM_VCPU_CMD   0x3BC3
#define UVD_4_0_GPCOM_VCPU_DATA0 0x3BC4
#define UVD_4_0_GPCOM_VCPU_DATA1 0x3BC5
#define UVD_4_0__ENGINE_CNTL	 0x3BC6

#define VEGA_20_GPCOM_VCPU_CMD   0x81C3
#define VEGA_20_GPCOM_VCPU_DATA0 0x81C4
#define VEGA_20_GPCOM_VCPU_DATA1 0x81C5
#define VEGA_20_UVD_ENGINE_CNTL 0x81C6

#define IB_SIZE		4096
#define MAX_RESOURCES	16

struct mmd_context {
	uint32_t family_id;
	uint32_t chip_id;
	uint32_t chip_rev;
	uint32_t asic_id;
	amdgpu_context_handle context_handle;
	amdgpu_bo_handle ib_handle;
	amdgpu_va_handle ib_va_handle;
	uint64_t ib_mc_address;
	uint32_t *ib_cpu;

	amdgpu_bo_handle resources[MAX_RESOURCES];
	unsigned int num_resources;

	/* vce */
	uint32_t vce_harvest_config;

	/* vcn */
	uint32_t vcn_ip_version_major;
	uint32_t vcn_ip_version_minor;
	bool vcn_dec_sw_ring;
	bool vcn_unified_ring;
	uint8_t vcn_reg_index;
	bool dec_ring;
	bool enc_ring;
	/* jpeg */
	bool jpeg_direct_reg;
};

struct amdgpu_mmd_bo {
	amdgpu_bo_handle handle;
	amdgpu_va_handle va_handle;
	uint64_t addr;
	uint64_t size;
	uint8_t *ptr;
};

struct amdgpu_uvd_enc {
	unsigned int width;
	unsigned int height;
	struct amdgpu_mmd_bo session;
	struct amdgpu_mmd_bo vbuf;
	struct amdgpu_mmd_bo bs;
	struct amdgpu_mmd_bo fb;
	struct amdgpu_mmd_bo cpb;
};

struct uvd_enc_context {
	struct mmd_context uvd;
	struct amdgpu_uvd_enc enc;

};

bool
is_gfx_pipe_removed(uint32_t family_id, uint32_t chip_id, uint32_t chip_rev);

bool
is_uvd_tests_enable(uint32_t family_id, uint32_t chip_id, uint32_t chip_rev);

bool
amdgpu_is_vega_or_polaris(uint32_t family_id, uint32_t chip_id, uint32_t chip_rev);

int
mmd_context_init(amdgpu_device_handle device_handle, struct mmd_context *context);

void
mmd_context_clean(amdgpu_device_handle device_handle,
		struct mmd_context *context);

int
submit(amdgpu_device_handle device_handle, struct mmd_context *context,
		unsigned int ndw, unsigned int ip);

void
alloc_resource(amdgpu_device_handle device_handle,
		struct amdgpu_mmd_bo *mmd_bo, unsigned int size,
		unsigned int domain);

void
free_resource(struct amdgpu_mmd_bo *mmd_bo);

// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "intel_pat.h"

#include "igt.h"

struct intel_pat_cache {
	uint8_t uc; /* UC + COH_NONE */
	uint8_t wt; /* WT + COH_NONE */
	uint8_t wb; /* WB + COH_AT_LEAST_1WAY */

	uint8_t max_index;
};

static void intel_get_pat_idx(int fd, struct intel_pat_cache *pat)
{
	uint16_t dev_id = intel_get_drm_devid(fd);

	if (intel_get_device_info(dev_id)->graphics_ver == 20) {
		pat->uc = 3;
		pat->wt = 15; /* Compressed + WB-transient */
		pat->wb = 2;
		pat->max_index = 31;
	} else if (IS_METEORLAKE(dev_id)) {
		pat->uc = 2;
		pat->wt = 1;
		pat->wb = 3;
		pat->max_index = 3;
	} else if (IS_PONTEVECCHIO(dev_id)) {
		pat->uc = 0;
		pat->wt = 2;
		pat->wb = 3;
		pat->max_index = 7;
	} else if (intel_graphics_ver(dev_id) <= IP_VER(12, 60)) {
		pat->uc = 3;
		pat->wt = 2;
		pat->wb = 0;
		pat->max_index = 3;
	} else {
		igt_critical("Platform is missing PAT settings for uc/wt/wb\n");
	}
}

uint8_t intel_get_max_pat_index(int fd)
{
	struct intel_pat_cache pat = {};

	intel_get_pat_idx(fd, &pat);
	return pat.max_index;
}

uint8_t intel_get_pat_idx_uc(int fd)
{
	struct intel_pat_cache pat = {};

	intel_get_pat_idx(fd, &pat);
	return pat.uc;
}

uint8_t intel_get_pat_idx_wt(int fd)
{
	struct intel_pat_cache pat = {};

	intel_get_pat_idx(fd, &pat);
	return pat.wt;
}

uint8_t intel_get_pat_idx_wb(int fd)
{
	struct intel_pat_cache pat = {};

	intel_get_pat_idx(fd, &pat);
	return pat.wb;
}

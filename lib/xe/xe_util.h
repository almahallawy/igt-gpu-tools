/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 *
 */

#ifndef XE_UTIL_H
#define XE_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <xe_drm.h>

#define XE_IS_SYSMEM_MEMORY_REGION(fd, region) \
	(xe_region_class(fd, region) == XE_MEM_REGION_CLASS_SYSMEM)
#define XE_IS_VRAM_MEMORY_REGION(fd, region) \
	(xe_region_class(fd, region) == XE_MEM_REGION_CLASS_VRAM)

struct igt_collection *
__xe_get_memory_region_set(int xe, uint32_t *mem_regions_type, int num_regions);

#define xe_get_memory_region_set(regions, mem_region_types...) ({ \
	unsigned int arr__[] = { mem_region_types }; \
	__xe_get_memory_region_set(regions, arr__, ARRAY_SIZE(arr__)); \
})

char *xe_memregion_dynamic_subtest_name(int xe, struct igt_collection *set);

#endif /* XE_UTIL_H */

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
	(xe_region_class(fd, region) == DRM_XE_MEM_REGION_CLASS_SYSMEM)
#define XE_IS_VRAM_MEMORY_REGION(fd, region) \
	(xe_region_class(fd, region) == DRM_XE_MEM_REGION_CLASS_VRAM)

struct igt_collection *
__xe_get_memory_region_set(int xe, uint32_t *mem_regions_type, int num_regions);

#define xe_get_memory_region_set(regions, mem_region_types...) ({ \
	unsigned int arr__[] = { mem_region_types }; \
	__xe_get_memory_region_set(regions, arr__, ARRAY_SIZE(arr__)); \
})

char *xe_memregion_dynamic_subtest_name(int xe, struct igt_collection *set);

enum xe_bind_op {
	XE_OBJECT_BIND,
	XE_OBJECT_UNBIND,
};

struct xe_object {
	uint32_t handle;
	uint64_t offset;
	uint64_t size;
	enum xe_bind_op bind_op;
	struct igt_list_head link;
};

void xe_bind_unbind_async(int fd, uint32_t vm, uint32_t bind_engine,
			  struct igt_list_head *obj_list,
			  uint32_t sync_in, uint32_t sync_out);

bool xe_is_gt_in_c6(int fd, int gt);

#endif /* XE_UTIL_H */

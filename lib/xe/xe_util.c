// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "igt.h"
#include "igt_syncobj.h"
#include "xe/xe_ioctl.h"
#include "xe/xe_query.h"
#include "xe/xe_util.h"

static bool __region_belongs_to_regions_type(struct drm_xe_query_mem_region *region,
					     uint32_t *mem_regions_type,
					     int num_regions)
{
	for (int i = 0; i < num_regions; i++)
		if (mem_regions_type[i] == region->mem_class)
			return true;
	return false;
}

struct igt_collection *
__xe_get_memory_region_set(int xe, uint32_t *mem_regions_type, int num_regions)
{
	struct drm_xe_query_mem_region *memregion;
	struct igt_collection *set = NULL;
	uint64_t memreg = all_memory_regions(xe), region;
	int count = 0, pos = 0;

	xe_for_each_mem_region(xe, memreg, region) {
		memregion = xe_mem_region(xe, region);
		if (__region_belongs_to_regions_type(memregion,
						     mem_regions_type,
						     num_regions))
			count++;
	}

	set = igt_collection_create(count);

	xe_for_each_mem_region(xe, memreg, region) {
		memregion = xe_mem_region(xe, region);
		igt_assert(region < (1ull << 31));
		if (__region_belongs_to_regions_type(memregion,
						     mem_regions_type,
						     num_regions)) {
			igt_collection_set_value(set, pos++, (int)region);
		}
	}

	igt_assert(count == pos);

	return set;
}

/**
 * xe_memregion_dynamic_subtest_name:
 * @xe: drm fd of Xe device
 * @igt_collection: memory region collection
 *
 * Function iterates over all memory regions inside the collection (keeped
 * in the value field) and generates the name which can be used during dynamic
 * subtest creation.
 *
 * Returns: newly allocated string, has to be freed by caller. Asserts if
 * caller tries to create a name using empty collection.
 */
char *xe_memregion_dynamic_subtest_name(int xe, struct igt_collection *set)
{
	struct igt_collection_data *data;
	char *name, *p;
	uint32_t region, len;

	igt_assert(set && set->size);
	/* enough for "name%d-" * n */
	len = set->size * 8;
	p = name = malloc(len);
	igt_assert(name);

	for_each_collection_data(data, set) {
		struct drm_xe_query_mem_region *memreg;
		int r;

		region = data->value;
		memreg = xe_mem_region(xe, region);

		if (XE_IS_CLASS_VRAM(memreg))
			r = snprintf(p, len, "%s%d-",
				     xe_region_name(region),
				     memreg->instance);
		else
			r = snprintf(p, len, "%s-",
				     xe_region_name(region));

		igt_assert(r > 0);
		p += r;
		len -= r;
	}

	/* remove last '-' */
	*(p - 1) = 0;

	return name;
}

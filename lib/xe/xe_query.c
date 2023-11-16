// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 *
 * Authors:
 *    Matthew Brost <matthew.brost@intel.com>
 */

#include <stdlib.h>
#include <pthread.h>

#include "drmtest.h"
#include "ioctl_wrappers.h"
#include "igt_map.h"

#include "xe_query.h"
#include "xe_ioctl.h"

static struct drm_xe_query_config *xe_query_config_new(int fd)
{
	struct drm_xe_query_config *config;
	struct drm_xe_device_query query = {
		.extensions = 0,
		.query = DRM_XE_DEVICE_QUERY_CONFIG,
		.size = 0,
		.data = 0,
	};

	igt_assert_eq(igt_ioctl(fd, DRM_IOCTL_XE_DEVICE_QUERY, &query), 0);

	config = malloc(query.size);
	igt_assert(config);

	query.data = to_user_pointer(config);
	igt_assert_eq(igt_ioctl(fd, DRM_IOCTL_XE_DEVICE_QUERY, &query), 0);

	igt_assert(config->num_params > 0);

	return config;
}

static struct drm_xe_query_gt_list *xe_query_gt_list_new(int fd)
{
	struct drm_xe_query_gt_list *gt_list;
	struct drm_xe_device_query query = {
		.extensions = 0,
		.query = DRM_XE_DEVICE_QUERY_GT_LIST,
		.size = 0,
		.data = 0,
	};

	igt_assert_eq(igt_ioctl(fd, DRM_IOCTL_XE_DEVICE_QUERY, &query), 0);

	gt_list = malloc(query.size);
	igt_assert(gt_list);

	query.data = to_user_pointer(gt_list);
	igt_assert_eq(igt_ioctl(fd, DRM_IOCTL_XE_DEVICE_QUERY, &query), 0);

	return gt_list;
}

static uint64_t __memory_regions(const struct drm_xe_query_gt_list *gt_list)
{
	uint64_t regions = 0;
	int i;

	for (i = 0; i < gt_list->num_gt; i++)
		regions |= gt_list->gt_list[i].near_mem_regions |
			   gt_list->gt_list[i].far_mem_regions;

	return regions;
}

static struct drm_xe_engine_class_instance *
xe_query_engines_new(int fd, unsigned int *num_engines)
{
	struct drm_xe_engine_class_instance *engines;
	struct drm_xe_device_query query = {
		.extensions = 0,
		.query = DRM_XE_DEVICE_QUERY_ENGINES,
		.size = 0,
		.data = 0,
	};

	igt_assert(num_engines);
	igt_assert_eq(igt_ioctl(fd, DRM_IOCTL_XE_DEVICE_QUERY, &query), 0);

	engines = malloc(query.size);
	igt_assert(engines);

	query.data = to_user_pointer(engines);
	igt_assert_eq(igt_ioctl(fd, DRM_IOCTL_XE_DEVICE_QUERY, &query), 0);

	*num_engines = query.size / sizeof(*engines);

	return engines;
}

static struct drm_xe_query_mem_regions *xe_query_mem_regions_new(int fd)
{
	struct drm_xe_query_mem_regions *mem_regions;
	struct drm_xe_device_query query = {
		.extensions = 0,
		.query = DRM_XE_DEVICE_QUERY_MEM_REGIONS,
		.size = 0,
		.data = 0,
	};

	igt_assert_eq(igt_ioctl(fd, DRM_IOCTL_XE_DEVICE_QUERY, &query), 0);

	mem_regions = malloc(query.size);
	igt_assert(mem_regions);

	query.data = to_user_pointer(mem_regions);
	igt_assert_eq(igt_ioctl(fd, DRM_IOCTL_XE_DEVICE_QUERY, &query), 0);

	return mem_regions;
}

static uint64_t native_region_for_gt(const struct drm_xe_query_gt_list *gt_list, int gt)
{
	uint64_t region;

	igt_assert(gt_list->num_gt > gt);
	region = gt_list->gt_list[gt].near_mem_regions;
	igt_assert(region);

	return region;
}

static uint64_t gt_vram_size(const struct drm_xe_query_mem_regions *mem_regions,
			     const struct drm_xe_query_gt_list *gt_list, int gt)
{
	int region_idx = ffs(native_region_for_gt(gt_list, gt)) - 1;

	if (XE_IS_CLASS_VRAM(&mem_regions->regions[region_idx]))
		return mem_regions->regions[region_idx].total_size;

	return 0;
}

static uint64_t gt_visible_vram_size(const struct drm_xe_query_mem_regions *mem_regions,
				     const struct drm_xe_query_gt_list *gt_list, int gt)
{
	int region_idx = ffs(native_region_for_gt(gt_list, gt)) - 1;

	if (XE_IS_CLASS_VRAM(&mem_regions->regions[region_idx]))
		return mem_regions->regions[region_idx].cpu_visible_size;

	return 0;
}

static bool __mem_has_vram(struct drm_xe_query_mem_regions *mem_regions)
{
	for (int i = 0; i < mem_regions->num_regions; i++)
		if (XE_IS_CLASS_VRAM(&mem_regions->regions[i]))
			return true;

	return false;
}

static uint32_t __mem_default_alignment(struct drm_xe_query_mem_regions *mem_regions)
{
	uint32_t alignment = XE_DEFAULT_ALIGNMENT;

	for (int i = 0; i < mem_regions->num_regions; i++)
		if (alignment < mem_regions->regions[i].min_page_size)
			alignment = mem_regions->regions[i].min_page_size;

	return alignment;
}

/**
 * xe_engine_class_string:
 * @engine_class: engine class
 *
 * Returns engine class name or 'unknown class engine' otherwise.
 */
const char *xe_engine_class_string(uint32_t engine_class)
{
	switch (engine_class) {
		case DRM_XE_ENGINE_CLASS_RENDER:
			return "DRM_XE_ENGINE_CLASS_RENDER";
		case DRM_XE_ENGINE_CLASS_COPY:
			return "DRM_XE_ENGINE_CLASS_COPY";
		case DRM_XE_ENGINE_CLASS_VIDEO_DECODE:
			return "DRM_XE_ENGINE_CLASS_VIDEO_DECODE";
		case DRM_XE_ENGINE_CLASS_VIDEO_ENHANCE:
			return "DRM_XE_ENGINE_CLASS_VIDEO_ENHANCE";
		case DRM_XE_ENGINE_CLASS_COMPUTE:
			return "DRM_XE_ENGINE_CLASS_COMPUTE";
		default:
			igt_warn("Engine class 0x%x unknown\n", engine_class);
			return "unknown engine class";
	}
}

static struct xe_device_cache {
	pthread_mutex_t cache_mutex;
	struct igt_map *map;
} cache;

static struct xe_device *find_in_cache_unlocked(int fd)
{
	return igt_map_search(cache.map, &fd);
}

static struct xe_device *find_in_cache(int fd)
{
	struct xe_device *xe_dev;

	pthread_mutex_lock(&cache.cache_mutex);
	xe_dev = find_in_cache_unlocked(fd);
	pthread_mutex_unlock(&cache.cache_mutex);

	return xe_dev;
}

static void xe_device_free(struct xe_device *xe_dev)
{
	free(xe_dev->config);
	free(xe_dev->gt_list);
	free(xe_dev->engines);
	free(xe_dev->mem_regions);
	free(xe_dev->vram_size);
	free(xe_dev);
}

/**
 * xe_device_get:
 * @fd: xe device fd
 *
 * Function creates and caches xe_device struct which contains configuration
 * data returned in few queries. Subsequent calls returns previously
 * created xe_device. To remove this from cache xe_device_put() must be
 * called.
 */
struct xe_device *xe_device_get(int fd)
{
	struct xe_device *xe_dev, *prev;

	xe_dev = find_in_cache(fd);
	if (xe_dev)
		return xe_dev;

	xe_dev = calloc(1, sizeof(*xe_dev));
	igt_assert(xe_dev);

	xe_dev->fd = fd;
	xe_dev->config = xe_query_config_new(fd);
	xe_dev->va_bits = xe_dev->config->info[DRM_XE_QUERY_CONFIG_VA_BITS];
	xe_dev->dev_id = xe_dev->config->info[DRM_XE_QUERY_CONFIG_REV_AND_DEVICE_ID] & 0xffff;
	xe_dev->gt_list = xe_query_gt_list_new(fd);
	xe_dev->memory_regions = __memory_regions(xe_dev->gt_list);
	xe_dev->engines = xe_query_engines_new(fd, &xe_dev->number_engines);
	xe_dev->mem_regions = xe_query_mem_regions_new(fd);
	xe_dev->vram_size = calloc(xe_dev->gt_list->num_gt, sizeof(*xe_dev->vram_size));
	xe_dev->visible_vram_size = calloc(xe_dev->gt_list->num_gt, sizeof(*xe_dev->visible_vram_size));
	for (int gt = 0; gt < xe_dev->gt_list->num_gt; gt++) {
		xe_dev->vram_size[gt] = gt_vram_size(xe_dev->mem_regions,
						     xe_dev->gt_list, gt);
		xe_dev->visible_vram_size[gt] =
			gt_visible_vram_size(xe_dev->mem_regions,
					     xe_dev->gt_list, gt);
	}
	xe_dev->default_alignment = __mem_default_alignment(xe_dev->mem_regions);
	xe_dev->has_vram = __mem_has_vram(xe_dev->mem_regions);

	/* We may get here from multiple threads, use first cached xe_dev */
	pthread_mutex_lock(&cache.cache_mutex);
	prev = find_in_cache_unlocked(fd);
	if (!prev) {
		igt_map_insert(cache.map, &xe_dev->fd, xe_dev);
	} else {
		xe_device_free(xe_dev);
		xe_dev = prev;
	}
	pthread_mutex_unlock(&cache.cache_mutex);

	return xe_dev;
}

static void delete_in_cache(struct igt_map_entry *entry)
{
	xe_device_free((struct xe_device *)entry->data);
}

/**
 * xe_device_put:
 * @fd: xe device fd
 *
 * Remove previously allocated and cached xe_device (if any).
 */
void xe_device_put(int fd)
{
	pthread_mutex_lock(&cache.cache_mutex);
	if (find_in_cache_unlocked(fd))
		igt_map_remove(cache.map, &fd, delete_in_cache);
	pthread_mutex_unlock(&cache.cache_mutex);
}

/**
 * xe_supports_faults:
 * @fd: xe device fd
 *
 * Returns true if xe device @fd allows creating vm in fault mode otherwise
 * false.
 *
 * NOTE: This function temporarily creates a VM in fault mode. Hence, while
 * this function is executing, no non-fault mode VMs can be created.
 */
bool xe_supports_faults(int fd)
{
	bool supports_faults;

	struct drm_xe_vm_create create = {
		.flags = DRM_XE_VM_CREATE_FLAG_ASYNC_DEFAULT |
			 DRM_XE_VM_CREATE_FLAG_FAULT_MODE,
	};

	supports_faults = !igt_ioctl(fd, DRM_IOCTL_XE_VM_CREATE, &create);

	if (supports_faults)
		xe_vm_destroy(fd, create.vm_id);

	return supports_faults;
}

static void xe_device_destroy_cache(void)
{
	pthread_mutex_lock(&cache.cache_mutex);
	igt_map_destroy(cache.map, delete_in_cache);
	pthread_mutex_unlock(&cache.cache_mutex);
}

static void xe_device_cache_init(void)
{
	pthread_mutex_init(&cache.cache_mutex, NULL);
	xe_device_destroy_cache();
	cache.map = igt_map_create(igt_map_hash_32, igt_map_equal_32);
}

#define xe_dev_FN(_NAME, _FIELD, _TYPE) \
_TYPE _NAME(int fd)			\
{					\
	struct xe_device *xe_dev;	\
					\
	xe_dev = find_in_cache(fd);	\
	igt_assert(xe_dev);		\
	return xe_dev->_FIELD;		\
}

/**
 * xe_number_gt:
 * @fd: xe device fd
 *
 * Return number of gt_list for xe device fd.
 */
xe_dev_FN(xe_number_gt, gt_list->num_gt, unsigned int);

/**
 * all_memory_regions:
 * @fd: xe device fd
 *
 * Returns memory regions bitmask for xe device @fd.
 */
xe_dev_FN(all_memory_regions, memory_regions, uint64_t);

/**
 * system_memory:
 * @fd: xe device fd
 *
 * Returns system memory bitmask for xe device @fd.
 */
uint64_t system_memory(int fd)
{
	uint64_t regions = all_memory_regions(fd);

	return regions & 0x1;
}

/**
 * vram_memory:
 * @fd: xe device fd
 * @gt: gt id
 *
 * Returns vram memory bitmask for xe device @fd and @gt id.
 */
uint64_t vram_memory(int fd, int gt)
{
	struct xe_device *xe_dev;

	xe_dev = find_in_cache(fd);
	igt_assert(xe_dev);
	igt_assert(gt >= 0 && gt < xe_dev->gt_list->num_gt);

	return xe_has_vram(fd) ? native_region_for_gt(xe_dev->gt_list, gt) : 0;
}

static uint64_t __xe_visible_vram_size(int fd, int gt)
{
	struct xe_device *xe_dev;

	xe_dev = find_in_cache(fd);
	igt_assert(xe_dev);

	return xe_dev->visible_vram_size[gt];
}

/**
 * vram_if_possible:
 * @fd: xe device fd
 * @gt: gt id
 *
 * Returns vram memory bitmask for xe device @fd and @gt id or system memory
 * if there's no vram memory available for @gt.
 */
uint64_t vram_if_possible(int fd, int gt)
{
	return vram_memory(fd, gt) ?: system_memory(fd);
}

/**
 * xe_engines:
 * @fd: xe device fd
 *
 * Returns engines array of xe device @fd.
 */
xe_dev_FN(xe_engines, engines, struct drm_xe_engine_class_instance *);

/**
 * xe_engine:
 * @fd: xe device fd
 * @idx: engine index
 *
 * Returns engine instance of xe device @fd and @idx.
 */
struct drm_xe_engine_class_instance *xe_engine(int fd, int idx)
{
	struct xe_device *xe_dev;

	xe_dev = find_in_cache(fd);
	igt_assert(xe_dev);
	igt_assert(idx >= 0 && idx < xe_dev->number_engines);

	return &xe_dev->engines[idx];
}

/**
 * xe_mem_region:
 * @fd: xe device fd
 * @region: region mask
 *
 * Returns memory region structure for @region mask.
 */
struct drm_xe_query_mem_region *xe_mem_region(int fd, uint64_t region)
{
	struct xe_device *xe_dev;
	int region_idx = ffs(region) - 1;

	xe_dev = find_in_cache(fd);
	igt_assert(xe_dev);
	igt_assert(xe_dev->mem_regions->num_regions > region_idx);

	return &xe_dev->mem_regions->regions[region_idx];
}

/**
 * xe_region_name:
 * @region: region mask
 *
 * Returns region string like "system" or "vramN" where N=0...62.
 */
const char *xe_region_name(uint64_t region)
{
	static char **vrams;
	int region_idx = ffs(region) - 1;

	/* Populate the array */
	if (!vrams) {
		vrams = calloc(64, sizeof(char *));
		for (int i = 0; i < 64; i++) {
			if (i != 0)
				asprintf(&vrams[i], "vram%d", i - 1);
			else
				asprintf(&vrams[i], "system");
			igt_assert(vrams[i]);
		}
	}

	return vrams[region_idx];
}

/**
 * xe_region_class:
 * @fd: xe device fd
 * @region: region mask
 *
 * Returns class of memory region structure for @region mask.
 */
uint16_t xe_region_class(int fd, uint64_t region)
{
	struct drm_xe_query_mem_region *memreg;

	memreg = xe_mem_region(fd, region);

	return memreg->mem_class;
}

/**
 * xe_min_page_size:
 * @fd: xe device fd
 * @region: region mask
 *
 * Returns minimum page size for @region.
 */
uint32_t xe_min_page_size(int fd, uint64_t region)
{
	return xe_mem_region(fd, region)->min_page_size;
}

/**
 * xe_config:
 * @fd: xe device fd
 *
 * Returns xe configuration of xe device @fd.
 */
xe_dev_FN(xe_config, config, struct drm_xe_query_config *);

/**
 * xe_number_engine:
 * @fd: xe device fd
 *
 * Returns number of hw engines of xe device @fd.
 */
xe_dev_FN(xe_number_engines, number_engines, unsigned int);

/**
 * xe_has_vram:
 * @fd: xe device fd
 *
 * Returns true if xe device @fd has vram otherwise false.
 */
xe_dev_FN(xe_has_vram, has_vram, bool);

/**
 * xe_vram_size:
 * @fd: xe device fd
 * @gt: gt
 *
 * Returns size of vram of xe device @fd.
 */
uint64_t xe_vram_size(int fd, int gt)
{
	struct xe_device *xe_dev;

	xe_dev = find_in_cache(fd);
	igt_assert(xe_dev);

	return xe_dev->vram_size[gt];
}

/**
 * xe_visible_vram_size:
 * @fd: xe device fd
 * @gt: gt
 *
 * Returns size of visible vram of xe device @fd.
 */
uint64_t xe_visible_vram_size(int fd, int gt)
{
	uint64_t visible_size;

	/*
	 * TODO: Keep it backwards compat for now. Fixup once the kernel side
	 * has landed.
	 */
	visible_size = __xe_visible_vram_size(fd, gt);
	if (!visible_size) /* older kernel */
		visible_size = xe_vram_size(fd, gt);

	return visible_size;
}
/**
 * xe_vram_available:
 * @fd: xe device fd
 * @gt: gt
 *
 * Returns available vram of xe device @fd and @gt.
 */
uint64_t xe_vram_available(int fd, int gt)
{
	struct xe_device *xe_dev;
	int region_idx;
	struct drm_xe_query_mem_region *mem_region;
	struct drm_xe_query_mem_regions *mem_regions;

	xe_dev = find_in_cache(fd);
	igt_assert(xe_dev);

	region_idx = ffs(native_region_for_gt(xe_dev->gt_list, gt)) - 1;
	mem_region = &xe_dev->mem_regions->regions[region_idx];

	if (XE_IS_CLASS_VRAM(mem_region)) {
		uint64_t available_vram;

		mem_regions = xe_query_mem_regions_new(fd);
		pthread_mutex_lock(&cache.cache_mutex);
		mem_region->used = mem_regions->regions[region_idx].used;
		available_vram = mem_region->total_size - mem_region->used;
		pthread_mutex_unlock(&cache.cache_mutex);
		free(mem_regions);

		return available_vram;
	}

	return 0;
}

/**
 * xe_get_default_alignment:
 * @fd: xe device fd
 *
 * Returns default alignment of objects for xe device @fd.
 */
xe_dev_FN(xe_get_default_alignment, default_alignment, uint32_t);

/**
 * xe_va_bits:
 * @fd: xe device fd
 *
 * Returns number of virtual address bits used in xe device @fd.
 */
xe_dev_FN(xe_va_bits, va_bits, uint32_t);


/**
 * xe_dev_id:
 * @fd: xe device fd
 *
 * Returns Device id of xe device @fd.
 */
xe_dev_FN(xe_dev_id, dev_id, uint16_t);

/**
 * xe_has_engine_class:
 * @fd: xe device fd
 * @engine_class: engine class
 *
 * Returns true if device @fd has hardware engine @class otherwise false.
 */
bool xe_has_engine_class(int fd, uint16_t engine_class)
{
	struct xe_device *xe_dev;

	xe_dev = find_in_cache(fd);
	igt_assert(xe_dev);

	for (int i = 0; i < xe_dev->number_engines; i++)
		if (xe_dev->engines[i].engine_class == engine_class)
			return true;

	return false;
}

igt_constructor
{
	xe_device_cache_init();
}

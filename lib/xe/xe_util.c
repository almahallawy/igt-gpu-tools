// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "igt.h"
#include "igt_syncobj.h"
#include "igt_sysfs.h"
#include "intel_pat.h"
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

#ifdef XEBINDDBG
#define bind_info igt_info
#define bind_debug igt_debug
#else
#define bind_info(...) {}
#define bind_debug(...) {}
#endif

static struct drm_xe_vm_bind_op *xe_alloc_bind_ops(int xe,
						   struct igt_list_head *obj_list,
						   uint32_t *num_ops)
{
	struct drm_xe_vm_bind_op *bind_ops, *ops;
	struct xe_object *obj;
	uint32_t num_objects = 0, i = 0, op, flags;

	igt_list_for_each_entry(obj, obj_list, link)
		num_objects++;

	*num_ops = num_objects;
	if (!num_objects) {
		bind_info(" [nothing to bind]\n");
		return NULL;
	}

	bind_ops = calloc(num_objects, sizeof(*bind_ops));
	igt_assert(bind_ops);

	igt_list_for_each_entry(obj, obj_list, link) {
		ops = &bind_ops[i];

		if (obj->bind_op == XE_OBJECT_BIND) {
			op = DRM_XE_VM_BIND_OP_MAP;
			flags = DRM_XE_VM_BIND_FLAG_ASYNC;
			ops->obj = obj->handle;
		} else {
			op = DRM_XE_VM_BIND_OP_UNMAP;
			flags = DRM_XE_VM_BIND_FLAG_ASYNC;
		}

		ops->op = op;
		ops->flags = flags;
		ops->obj_offset = 0;
		ops->addr = obj->offset;
		ops->range = obj->size;
		ops->prefetch_mem_region_instance = 0;
		if (obj->pat_index == DEFAULT_PAT_INDEX)
			ops->pat_index = intel_get_pat_idx_wb(xe);
		else
			ops->pat_index = obj->pat_index;

		bind_info("  [%d]: [%6s] handle: %u, offset: %llx, size: %llx\n",
			  i, obj->bind_op == XE_OBJECT_BIND ? "BIND" : "UNBIND",
			  ops->obj, (long long)ops->addr, (long long)ops->range);
		i++;
	}

	return bind_ops;
}

/**
 * xe_bind_unbind_async:
 * @xe: drm fd of Xe device
 * @vm: vm to bind/unbind objects to/from
 * @bind_engine: bind engine, 0 if default
 * @obj_list: list of xe_object
 * @sync_in: sync object (fence-in), 0 if there's no input dependency
 * @sync_out: sync object (fence-out) to signal on bind/unbind completion,
 *            if 0 wait for bind/unbind completion.
 *
 * Function iterates over xe_object @obj_list, prepares binding operation
 * and does bind/unbind in one step. Providing sync_in / sync_out allows
 * working in pipelined mode. With sync_in and sync_out set to 0 function
 * waits until binding operation is complete.
 */
void xe_bind_unbind_async(int xe, uint32_t vm, uint32_t bind_engine,
			  struct igt_list_head *obj_list,
			  uint32_t sync_in, uint32_t sync_out)
{
	struct drm_xe_vm_bind_op *bind_ops;
	struct drm_xe_sync tabsyncs[2] = {
		{ .flags = DRM_XE_SYNC_FLAG_SYNCOBJ, .handle = sync_in },
		{ .flags = DRM_XE_SYNC_FLAG_SYNCOBJ | DRM_XE_SYNC_FLAG_SIGNAL, .handle = sync_out },
	};
	struct drm_xe_sync *syncs;
	uint32_t num_binds = 0;
	int num_syncs;

	bind_info("[Binding to vm: %u]\n", vm);
	bind_ops = xe_alloc_bind_ops(xe, obj_list, &num_binds);

	if (!num_binds) {
		if (sync_out)
			syncobj_signal(xe, &sync_out, 1);
		return;
	}

	if (sync_in) {
		syncs = tabsyncs;
		num_syncs = 2;
	} else {
		syncs = &tabsyncs[1];
		num_syncs = 1;
	}

	/* User didn't pass sync out, create it and wait for completion */
	if (!sync_out)
		tabsyncs[1].handle = syncobj_create(xe, 0);

	bind_info("[Binding syncobjs: (in: %u, out: %u)]\n",
		  tabsyncs[0].handle, tabsyncs[1].handle);

	if (num_binds == 1) {
		if ((bind_ops[0].op & 0xffff) == DRM_XE_VM_BIND_OP_MAP)
			xe_vm_bind_async(xe, vm, bind_engine, bind_ops[0].obj, 0,
					 bind_ops[0].addr, bind_ops[0].range,
					 syncs, num_syncs);
		else
			xe_vm_unbind_async(xe, vm, bind_engine, 0,
					   bind_ops[0].addr, bind_ops[0].range,
					   syncs, num_syncs);
	} else {
		xe_vm_bind_array(xe, vm, bind_engine, bind_ops,
				 num_binds, syncs, num_syncs);
	}

	if (!sync_out) {
		igt_assert_eq(syncobj_wait_err(xe, &tabsyncs[1].handle, 1, INT64_MAX, 0), 0);
		syncobj_destroy(xe, tabsyncs[1].handle);
	}

	free(bind_ops);
}

/**
 * xe_is_gt_in_c6:
 * @fd: pointer to xe drm fd
 * @gt: gt number
 *
 * Check if GT is in C6 state
 */
bool xe_is_gt_in_c6(int fd, int gt)
{
	char gt_c_state[16];
	int gt_fd;

	gt_fd = xe_sysfs_gt_open(fd, gt);
	igt_assert(gt_fd >= 0);
	igt_assert(igt_sysfs_scanf(gt_fd, "gtidle/idle_status", "%s", gt_c_state) == 1);
	close(gt_fd);

	return strcmp(gt_c_state, "gt-c6") == 0;
}

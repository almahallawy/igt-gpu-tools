/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 *
 * Authors:
 *    Francois Dugast <francois.dugast@intel.com>
 */

#include <stdint.h>

#include "i915/gem_create.h"
#include "igt.h"
#include "gen7_media.h"
#include "gen8_media.h"
#include "gen9_media.h"
#include "intel_compute.h"
#include "lib/igt_syncobj.h"
#include "lib/intel_reg.h"
#include "xe_drm.h"
#include "xe/xe_ioctl.h"
#include "xe/xe_query.h"
#include "xehp_media.h"

#define PIPE_CONTROL			0x7a000004
#define MEDIA_STATE_FLUSH		0x0
#define MAX(X, Y)			(((X) > (Y)) ? (X) : (Y))
#define SIZE_DATA			64
#define SIZE_BATCH			0x1000
#define SIZE_BUFFER_INPUT		MAX(sizeof(float) * SIZE_DATA, 0x1000)
#define SIZE_BUFFER_OUTPUT		MAX(sizeof(float) * SIZE_DATA, 0x1000)
#define ADDR_BATCH			0x100000UL
#define ADDR_INPUT			0x200000UL
#define ADDR_OUTPUT			0x300000UL
#define ADDR_SURFACE_STATE_BASE		0x400000UL
#define ADDR_DYNAMIC_STATE_BASE		0x500000UL
#define ADDR_INDIRECT_OBJECT_BASE	0x100000000
#define OFFSET_INDIRECT_DATA_START	0xFFFD0000
#define OFFSET_KERNEL			0xFFFE0000

#define XEHP_ADDR_GENERAL_STATE_BASE		0x80000000UL
#define XEHP_ADDR_INSTRUCTION_STATE_BASE	0x90000000UL
#define XEHP_OFFSET_BINDING_TABLE		0x1000

struct bo_dict_entry {
	uint64_t addr;
	uint32_t size;
	void *data;
	const char *name;
	uint32_t handle;
};

struct bo_execenv {
	int fd;
	enum intel_driver driver;

	/* Xe part */
	uint32_t vm;
	uint32_t exec_queue;

	/* i915 part */
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 *obj;
};

static void bo_execenv_create(int fd, struct bo_execenv *execenv)
{
	igt_assert(execenv);

	memset(execenv, 0, sizeof(*execenv));
	execenv->fd = fd;
	execenv->driver = get_intel_driver(fd);

	if (execenv->driver == INTEL_DRIVER_XE) {
		uint16_t engine_class;
		uint32_t devid = intel_get_drm_devid(fd);
		const struct intel_device_info *info = intel_get_device_info(devid);

		if (info->graphics_ver >= 12 && info->graphics_rel < 60)
			engine_class = DRM_XE_ENGINE_CLASS_RENDER;
		else
			engine_class = DRM_XE_ENGINE_CLASS_COMPUTE;

		execenv->vm = xe_vm_create(fd, DRM_XE_VM_CREATE_FLAG_ASYNC_DEFAULT, 0);
		execenv->exec_queue = xe_exec_queue_create_class(fd, execenv->vm,
								 engine_class);
	}
}

static void bo_execenv_destroy(struct bo_execenv *execenv)
{
	igt_assert(execenv);

	if (execenv->driver == INTEL_DRIVER_XE) {
		xe_vm_destroy(execenv->fd, execenv->vm);
		xe_exec_queue_destroy(execenv->fd, execenv->exec_queue);
	}
}

static void bo_execenv_bind(struct bo_execenv *execenv,
			    struct bo_dict_entry *bo_dict, int entries)
{
	int fd = execenv->fd;

	if (execenv->driver == INTEL_DRIVER_XE) {
		uint32_t vm = execenv->vm;
		uint64_t alignment = xe_get_default_alignment(fd);
		struct drm_xe_sync sync = { 0 };

		sync.flags = DRM_XE_SYNC_FLAG_SYNCOBJ | DRM_XE_SYNC_FLAG_SIGNAL;
		sync.handle = syncobj_create(fd, 0);

		for (int i = 0; i < entries; i++) {
			bo_dict[i].data = aligned_alloc(alignment, bo_dict[i].size);
			xe_vm_bind_userptr_async(fd, vm, 0, to_user_pointer(bo_dict[i].data),
						 bo_dict[i].addr, bo_dict[i].size, &sync, 1);
			syncobj_wait(fd, &sync.handle, 1, INT64_MAX, 0, NULL);
			memset(bo_dict[i].data, 0, bo_dict[i].size);

			igt_debug("[i: %2d name: %20s] data: %p, addr: %16llx, size: %llx\n",
				  i, bo_dict[i].name, bo_dict[i].data,
				  (long long)bo_dict[i].addr,
				  (long long)bo_dict[i].size);
		}

		syncobj_destroy(fd, sync.handle);
	} else {
		struct drm_i915_gem_execbuffer2 *execbuf = &execenv->execbuf;
		struct drm_i915_gem_exec_object2 *obj;

		obj = calloc(entries, sizeof(*obj));
		execenv->obj = obj;

		for (int i = 0; i < entries; i++) {
			bo_dict[i].handle = gem_create(fd, bo_dict[i].size);
			bo_dict[i].data = gem_mmap__device_coherent(fd, bo_dict[i].handle,
								    0, bo_dict[i].size,
								    PROT_READ | PROT_WRITE);
			igt_debug("[i: %2d name: %20s] handle: %u, data: %p, addr: %16llx, size: %llx\n",
				  i, bo_dict[i].name,
				  bo_dict[i].handle, bo_dict[i].data,
				  (long long)bo_dict[i].addr,
				  (long long)bo_dict[i].size);

			obj[i].handle = bo_dict[i].handle;
			obj[i].offset = CANONICAL(bo_dict[i].addr);
			obj[i].flags = EXEC_OBJECT_PINNED | EXEC_OBJECT_SUPPORTS_48B_ADDRESS;
			if (bo_dict[i].addr == ADDR_OUTPUT)
				obj[i].flags |= EXEC_OBJECT_WRITE;
		}

		execbuf->buffers_ptr = to_user_pointer(obj);
		execbuf->buffer_count = entries;
	}
}

static void bo_execenv_unbind(struct bo_execenv *execenv,
			      struct bo_dict_entry *bo_dict, int entries)
{
	int fd = execenv->fd;

	if (execenv->driver == INTEL_DRIVER_XE) {
		uint32_t vm = execenv->vm;
		struct drm_xe_sync sync = { 0 };

		sync.flags = DRM_XE_SYNC_FLAG_SYNCOBJ | DRM_XE_SYNC_FLAG_SIGNAL;
		sync.handle = syncobj_create(fd, 0);

		for (int i = 0; i < entries; i++) {
			xe_vm_unbind_async(fd, vm, 0, 0, bo_dict[i].addr, bo_dict[i].size, &sync, 1);
			syncobj_wait(fd, &sync.handle, 1, INT64_MAX, 0, NULL);
			free(bo_dict[i].data);
		}

		syncobj_destroy(fd, sync.handle);
	} else {
		for (int i = 0; i < entries; i++) {
			gem_close(fd, bo_dict[i].handle);
			munmap(bo_dict[i].data, bo_dict[i].size);
		}
		free(execenv->obj);
	}
}

static void bo_execenv_exec(struct bo_execenv *execenv, uint64_t start_addr)
{
	int fd = execenv->fd;

	if (execenv->driver == INTEL_DRIVER_XE) {
		xe_exec_wait(fd, execenv->exec_queue, start_addr);
	} else {
		struct drm_i915_gem_execbuffer2 *execbuf = &execenv->execbuf;
		struct drm_i915_gem_exec_object2 *obj = execenv->obj;
		int num_objects = execbuf->buffer_count;

		execbuf->flags = I915_EXEC_RENDER;
		gem_execbuf(fd, execbuf);
		gem_sync(fd, obj[num_objects - 1].handle); /* batch handle */
	}
}

/*
 * TGL compatible batch
 */

/**
 * create_indirect_data:
 * @addr_bo_buffer_batch: pointer to batch buffer
 * @addr_input: input buffer gpu offset
 * @addr_output: output buffer gpu offset
 *
 * Prepares indirect data for compute pipeline.
 */
static void create_indirect_data(uint32_t *addr_bo_buffer_batch,
				 uint64_t addr_input,
				 uint64_t addr_output,
				 uint32_t end_value)
{
	uint32_t val = 0;
	int b = 0, curr = 0;

	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000200;

	addr_bo_buffer_batch[b++] = 0x00000001;
	addr_bo_buffer_batch[b++] = 0x00000001;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = addr_input & 0xffffffff;
	addr_bo_buffer_batch[b++] = addr_input >> 32;
	addr_bo_buffer_batch[b++] = addr_output & 0xffffffff;
	addr_bo_buffer_batch[b++] = addr_output >> 32;

	addr_bo_buffer_batch[b++] = 0x00000400;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = 0x00000200;
	addr_bo_buffer_batch[b++] = 0x00000001;
	addr_bo_buffer_batch[b++] = 0x00000001;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;

	/*
	 * Runtime prepares 32 16-bit incremented values packed to single dword.
	 * Then it lefts 32 dword gap filled with zeroes. Pattern looks the
	 * same for tgl and dg1 (apart of number of values).
	 */
	while (val < end_value) {
		addr_bo_buffer_batch[b++] = val | ((val + 1) << 16);
		val += 2;
		if (++curr % 16 == 0)
			b += 32;
	}
}

/**
 * create_surface_state:
 * @addr_bo_buffer_batch: pointer to batch buffer
 * @addr_input: input buffer gpu offset
 * @addr_output: output buffer gpu offset
 *
 * Prepares surface state for compute pipeline.
 */
static void create_surface_state(uint32_t *addr_bo_buffer_batch,
				 uint64_t addr_input,
				 uint64_t addr_output)
{
	int b = 0;

	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x87FD4000;
	addr_bo_buffer_batch[b++] = 0x04000000;
	addr_bo_buffer_batch[b++] = 0x001F007F;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00004000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = addr_input & 0xffffffff;
	addr_bo_buffer_batch[b++] = addr_input >> 32;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x87FD4000;
	addr_bo_buffer_batch[b++] = 0x04000000;
	addr_bo_buffer_batch[b++] = 0x001F007F;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00004000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = addr_output & 0xffffffff;
	addr_bo_buffer_batch[b++] = addr_output >> 32;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000040;
	addr_bo_buffer_batch[b++] = 0x00000080;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
}

/**
 * create_dynamic_state:
 * @addr_bo_buffer_batch: pointer to batch buffer
 * @offset_kernel: gpu offset of the shader
 *
 * Prepares dynamic state for compute pipeline.
 */
static void create_dynamic_state(uint32_t *addr_bo_buffer_batch,
				 uint64_t offset_kernel)
{
	int b = 0;

	addr_bo_buffer_batch[b++] = offset_kernel;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00180000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x000000C0;
	addr_bo_buffer_batch[b++] = 0x00060000;
	addr_bo_buffer_batch[b++] = 0x00000010;
	addr_bo_buffer_batch[b++] = 0x00000003;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
}

/**
 * tgllp_compute_exec_compute:
 * @addr_bo_buffer_batch: pointer to batch buffer
 * @addr_surface_state_base: gpu offset of surface state data
 * @addr_dynamic_state_base: gpu offset of dynamic state data
 * @addr_indirect_object_base: gpu offset of indirect object data
 * @offset_indirect_data_start: gpu offset of indirect data start
 *
 * Prepares compute pipeline.
 */
static void tgllp_compute_exec_compute(uint32_t *addr_bo_buffer_batch,
				       uint64_t addr_surface_state_base,
				       uint64_t addr_dynamic_state_base,
				       uint64_t addr_indirect_object_base,
				       uint64_t offset_indirect_data_start)
{
	int b = 0;

	addr_bo_buffer_batch[b++] = MI_LOAD_REGISTER_IMM(1);
	addr_bo_buffer_batch[b++] = 0x00002580;
	addr_bo_buffer_batch[b++] = 0x00060002;
	addr_bo_buffer_batch[b++] = PIPELINE_SELECT;
	addr_bo_buffer_batch[b++] = MI_LOAD_REGISTER_IMM(1);
	addr_bo_buffer_batch[b++] = 0x00007034;
	addr_bo_buffer_batch[b++] = 0x60000321;
	addr_bo_buffer_batch[b++] = PIPE_CONTROL;
	addr_bo_buffer_batch[b++] = 0x00100000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = MI_LOAD_REGISTER_IMM(1);
	addr_bo_buffer_batch[b++] = 0x0000E404;
	addr_bo_buffer_batch[b++] = 0x00000100;
	addr_bo_buffer_batch[b++] = PIPE_CONTROL;
	addr_bo_buffer_batch[b++] = 0x00101021;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = MEDIA_VFE_STATE | (9 - 2);
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00A70100;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x07820000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = PIPE_CONTROL;
	addr_bo_buffer_batch[b++] = 0x00100420;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = STATE_BASE_ADDRESS | (16 - 2);
	addr_bo_buffer_batch[b++] = 0x00000001;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00040000;
	addr_bo_buffer_batch[b++] = (addr_surface_state_base & 0xffffffff) | 0x1;
	addr_bo_buffer_batch[b++] = addr_surface_state_base >> 32;
	addr_bo_buffer_batch[b++] = (addr_dynamic_state_base & 0xffffffff) | 0x1;
	addr_bo_buffer_batch[b++] = addr_dynamic_state_base >> 32;
	addr_bo_buffer_batch[b++] = (addr_indirect_object_base & 0xffffffff) | 0x1;
	addr_bo_buffer_batch[b++] = (addr_indirect_object_base >> 32) | 0xffff0000;
	addr_bo_buffer_batch[b++] = (addr_indirect_object_base & 0xffffffff) | 0x41;
	addr_bo_buffer_batch[b++] = addr_indirect_object_base >> 32;
	addr_bo_buffer_batch[b++] = 0xFFFFF001;
	addr_bo_buffer_batch[b++] = 0x00010001;
	addr_bo_buffer_batch[b++] = 0xFFFFF001;
	addr_bo_buffer_batch[b++] = 0xFFFFF001;
	addr_bo_buffer_batch[b++] = (addr_surface_state_base & 0xffffffff) | 0x1;
	addr_bo_buffer_batch[b++] = addr_surface_state_base >> 32;
	addr_bo_buffer_batch[b++] = 0x003BF000;
	addr_bo_buffer_batch[b++] = 0x00000041;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = PIPE_CONTROL;
	addr_bo_buffer_batch[b++] = 0x00100000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = PIPE_CONTROL;
	addr_bo_buffer_batch[b++] = 0x00100000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = MEDIA_STATE_FLUSH;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = MEDIA_INTERFACE_DESCRIPTOR_LOAD | (4 - 2);
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000020;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = GPGPU_WALKER | 13;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000c80;
	addr_bo_buffer_batch[b++] = offset_indirect_data_start;
	addr_bo_buffer_batch[b++] = 0x8000000f;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000002;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000001;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000001;
	addr_bo_buffer_batch[b++] = 0xffffffff;
	addr_bo_buffer_batch[b++] = 0xffffffff;
	addr_bo_buffer_batch[b++] = MEDIA_STATE_FLUSH;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = PIPE_CONTROL;
	addr_bo_buffer_batch[b++] = 0x00100000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = PIPE_CONTROL;
	addr_bo_buffer_batch[b++] = 0x00100120;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = MI_BATCH_BUFFER_END;
}

/**
 * dg1_compute_exec_compute:
 * @addr_bo_buffer_batch: pointer to batch buffer
 * @addr_surface_state_base: gpu offset of surface state data
 * @addr_dynamic_state_base: gpu offset of dynamic state data
 * @addr_indirect_object_base: gpu offset of indirect object data
 * @offset_indirect_data_start: gpu offset of indirect data start
 *
 * Prepares compute pipeline.
 */
static void dg1_compute_exec_compute(uint32_t *addr_bo_buffer_batch,
				     uint64_t addr_surface_state_base,
				     uint64_t addr_dynamic_state_base,
				     uint64_t addr_indirect_object_base,
				     uint64_t offset_indirect_data_start)
{
	int b = 0;

	addr_bo_buffer_batch[b++] = XEHP_STATE_COMPUTE_MODE;
	addr_bo_buffer_batch[b++] = 0x00180010;

	addr_bo_buffer_batch[b++] = MEDIA_VFE_STATE | (9 - 2);
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x02FF0100;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x04000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = MI_LOAD_REGISTER_IMM(1);
	addr_bo_buffer_batch[b++] = 0x00002580;
	addr_bo_buffer_batch[b++] = 0x00060002;

	addr_bo_buffer_batch[b++] = STATE_BASE_ADDRESS | 0x14;
	addr_bo_buffer_batch[b++] = 0x00000001;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x000A0000;
	addr_bo_buffer_batch[b++] = (addr_surface_state_base & 0xffffffff) | 0x1;
	addr_bo_buffer_batch[b++] = addr_surface_state_base >> 32;
	addr_bo_buffer_batch[b++] = (addr_dynamic_state_base & 0xffffffff) | 0x1;
	addr_bo_buffer_batch[b++] = addr_dynamic_state_base >> 32;
	addr_bo_buffer_batch[b++] = (addr_indirect_object_base & 0xffffffff) | 0x1;
	addr_bo_buffer_batch[b++] = (addr_indirect_object_base >> 32) | 0xffff0000;
	addr_bo_buffer_batch[b++] = (addr_indirect_object_base & 0xffffffff) | 0xA1;
	addr_bo_buffer_batch[b++] = addr_indirect_object_base >> 32;
	addr_bo_buffer_batch[b++] = 0xFFFFF001;
	addr_bo_buffer_batch[b++] = 0x00010001;
	addr_bo_buffer_batch[b++] = 0xFFFFF001;
	addr_bo_buffer_batch[b++] = 0xFFFFF001;
	addr_bo_buffer_batch[b++] = (addr_surface_state_base & 0xffffffff) | 0xA1;
	addr_bo_buffer_batch[b++] = addr_surface_state_base >> 32;
	addr_bo_buffer_batch[b++] = 0x003BF000;
	addr_bo_buffer_batch[b++] = 0x000000A1;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = MEDIA_INTERFACE_DESCRIPTOR_LOAD | (4 - 2);
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000020;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = GPGPU_WALKER | 13;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000c80;
	addr_bo_buffer_batch[b++] = offset_indirect_data_start;
	addr_bo_buffer_batch[b++] = 0x8000000f;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000002;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000001;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000001;
	addr_bo_buffer_batch[b++] = 0xffffffff;
	addr_bo_buffer_batch[b++] = 0xffffffff;

	addr_bo_buffer_batch[b++] = MEDIA_STATE_FLUSH;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = MI_BATCH_BUFFER_END;
}

/**
 * compute_exec - run a pipeline compatible with Tiger Lake and DG1
 *
 * @fd: file descriptor of the opened DRM device
 * @kernel: GPU Kernel binary to be executed
 * @size: size of @kernel.
 */
static void compute_exec(int fd, const unsigned char *kernel,
			 unsigned int size)
{
#define BO_DICT_ENTRIES 7
	struct bo_dict_entry bo_dict[BO_DICT_ENTRIES] = {
		{ .addr = ADDR_INDIRECT_OBJECT_BASE + OFFSET_KERNEL,
		  .name = "kernel" },
		{ .addr = ADDR_DYNAMIC_STATE_BASE,
		  .size =  0x1000,
		  .name = "dynamic state base" },
		{ .addr = ADDR_SURFACE_STATE_BASE,
		  .size =  0x1000,
		  .name = "surface state base" },
		{ .addr = ADDR_INDIRECT_OBJECT_BASE + OFFSET_INDIRECT_DATA_START,
		  .size =  0x10000,
		  .name = "indirect data start" },
		{ .addr = ADDR_INPUT,
		  .size = SIZE_BUFFER_INPUT,
		  .name = "input" },
		{ .addr = ADDR_OUTPUT,
		  .size = SIZE_BUFFER_OUTPUT,
		  .name = "output" },
		{ .addr = ADDR_BATCH,
		  .size = SIZE_BATCH,
		  .name = "batch" },
	};
	struct bo_execenv execenv;
	float *dinput;
	uint16_t devid = intel_get_drm_devid(fd);

	bo_execenv_create(fd, &execenv);

	/* Sets Kernel size */
	bo_dict[0].size = ALIGN(size, 0x1000);

	bo_execenv_bind(&execenv, bo_dict, BO_DICT_ENTRIES);

	memcpy(bo_dict[0].data, kernel, size);
	create_dynamic_state(bo_dict[1].data, OFFSET_KERNEL);
	create_surface_state(bo_dict[2].data, ADDR_INPUT, ADDR_OUTPUT);
	create_indirect_data(bo_dict[3].data, ADDR_INPUT, ADDR_OUTPUT,
			     IS_DG1(devid) ? 0x200 : 0x40);

	dinput = (float *)bo_dict[4].data;
	srand(time(NULL));
	for (int i = 0; i < SIZE_DATA; i++)
		((float *)dinput)[i] = rand() / (float)RAND_MAX;

	if (IS_DG1(devid))
		dg1_compute_exec_compute(bo_dict[6].data,
					 ADDR_SURFACE_STATE_BASE,
					 ADDR_DYNAMIC_STATE_BASE,
					 ADDR_INDIRECT_OBJECT_BASE,
					 OFFSET_INDIRECT_DATA_START);
	else
		tgllp_compute_exec_compute(bo_dict[6].data,
					   ADDR_SURFACE_STATE_BASE,
					   ADDR_DYNAMIC_STATE_BASE,
					   ADDR_INDIRECT_OBJECT_BASE,
					   OFFSET_INDIRECT_DATA_START);

	bo_execenv_exec(&execenv, ADDR_BATCH);

	for (int i = 0; i < SIZE_DATA; i++) {
		float f1, f2;

		f1 = ((float *) bo_dict[5].data)[i];
		f2 = ((float *) bo_dict[4].data)[i];
		if (f1 != f2 * f2)
			igt_debug("[%4d] f1: %f != %f\n", i, f1, f2 * f2);
		igt_assert(f1 == f2 * f2);
	}

	bo_execenv_unbind(&execenv, bo_dict, BO_DICT_ENTRIES);
	bo_execenv_destroy(&execenv);
}

static void xehp_create_indirect_data(uint32_t *addr_bo_buffer_batch,
				      uint64_t addr_input,
				      uint64_t addr_output)
{
	int b = 0;

	addr_bo_buffer_batch[b++] = addr_input & 0xffffffff;
	addr_bo_buffer_batch[b++] = addr_input >> 32;
	addr_bo_buffer_batch[b++] = addr_output & 0xffffffff;
	addr_bo_buffer_batch[b++] = addr_output >> 32;
	addr_bo_buffer_batch[b++] = 0x00000400;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000400;
	addr_bo_buffer_batch[b++] = 0x00000001;
	addr_bo_buffer_batch[b++] = 0x00000001;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
}

static void xehp_create_surface_state(uint32_t *addr_bo_buffer_batch,
				      uint64_t addr_input,
				      uint64_t addr_output)
{
	int b = 0;

	addr_bo_buffer_batch[b++] = 0x87FDC000;
	addr_bo_buffer_batch[b++] = 0x06000000;
	addr_bo_buffer_batch[b++] = 0x001F007F;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00002000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = addr_input & 0xffffffff;
	addr_bo_buffer_batch[b++] = addr_input >> 32;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = 0x87FDC000;
	addr_bo_buffer_batch[b++] = 0x06000000;
	addr_bo_buffer_batch[b++] = 0x001F007F;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00002000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = addr_output & 0xffffffff;
	addr_bo_buffer_batch[b++] = addr_output >> 32;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = 0x00001000;
	addr_bo_buffer_batch[b++] = 0x00001040;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
}

static void xehp_compute_exec_compute(uint32_t *addr_bo_buffer_batch,
				      uint64_t addr_general_state_base,
				      uint64_t addr_surface_state_base,
				      uint64_t addr_dynamic_state_base,
				      uint64_t addr_instruction_state_base,
				      uint64_t offset_indirect_data_start,
				      uint64_t kernel_start_pointer)
{
	int b = 0;

	igt_debug("general   state base: %lx\n", addr_general_state_base);
	igt_debug("surface   state base: %lx\n", addr_surface_state_base);
	igt_debug("dynamic   state base: %lx\n", addr_dynamic_state_base);
	igt_debug("instruct   base addr: %lx\n", addr_instruction_state_base);
	igt_debug("bindless   base addr: %lx\n", addr_surface_state_base);
	igt_debug("offset indirect addr: %lx\n", offset_indirect_data_start);
	igt_debug("kernel start pointer: %lx\n", kernel_start_pointer);

	addr_bo_buffer_batch[b++] = GEN7_PIPELINE_SELECT | GEN9_PIPELINE_SELECTION_MASK |
				    PIPELINE_SELECT_GPGPU;

	addr_bo_buffer_batch[b++] = XEHP_STATE_COMPUTE_MODE;
	addr_bo_buffer_batch[b++] = 0x80180010;

	addr_bo_buffer_batch[b++] = XEHP_CFE_STATE;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x0c008800;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = MI_LOAD_REGISTER_IMM(1);
	addr_bo_buffer_batch[b++] = 0x00002580;
	addr_bo_buffer_batch[b++] = 0x00060002;

	addr_bo_buffer_batch[b++] = STATE_BASE_ADDRESS | 0x14;
	addr_bo_buffer_batch[b++] = (addr_general_state_base & 0xffffffff) | 0x61;
	addr_bo_buffer_batch[b++] = addr_general_state_base >> 32;
	addr_bo_buffer_batch[b++] = 0x0106c000;
	addr_bo_buffer_batch[b++] = (addr_surface_state_base & 0xffffffff) | 0x61;
	addr_bo_buffer_batch[b++] = addr_surface_state_base >> 32;
	addr_bo_buffer_batch[b++] = (addr_dynamic_state_base & 0xffffffff) | 0x61;
	addr_bo_buffer_batch[b++] = addr_dynamic_state_base >> 32;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = (addr_instruction_state_base & 0xffffffff) | 0x61;
	addr_bo_buffer_batch[b++] = addr_instruction_state_base >> 32;
	addr_bo_buffer_batch[b++] = 0xfffff001;
	addr_bo_buffer_batch[b++] = 0x00010001;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0xfffff001;
	addr_bo_buffer_batch[b++] = (addr_surface_state_base & 0xffffffff) | 0x61;
	addr_bo_buffer_batch[b++] = addr_surface_state_base >> 32;
	addr_bo_buffer_batch[b++] = 0x00007fbf;
	addr_bo_buffer_batch[b++] = 0x00000061;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = GEN8_3DSTATE_BINDING_TABLE_POOL_ALLOC | 2;
	addr_bo_buffer_batch[b++] = (addr_surface_state_base & 0xffffffff) | 0x6;
	addr_bo_buffer_batch[b++] = addr_surface_state_base >> 32;
	addr_bo_buffer_batch[b++] = 0x00002000;
	addr_bo_buffer_batch[b++] = 0x001ff000;

	addr_bo_buffer_batch[b++] = XEHP_COMPUTE_WALKER | 0x25;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000040;
	addr_bo_buffer_batch[b++] = offset_indirect_data_start;
	addr_bo_buffer_batch[b++] = 0xbe040000;
	addr_bo_buffer_batch[b++] = 0xffffffff;
	addr_bo_buffer_batch[b++] = 0x0000003f;
	addr_bo_buffer_batch[b++] = 0x00000010;

	addr_bo_buffer_batch[b++] = 0x00000001;
	addr_bo_buffer_batch[b++] = 0x00000001;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = kernel_start_pointer;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00180000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00001080;
	addr_bo_buffer_batch[b++] = 0x0c000002;

	addr_bo_buffer_batch[b++] = 0x00000008;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00001027;
	addr_bo_buffer_batch[b++] = ADDR_BATCH;
	addr_bo_buffer_batch[b++] = ADDR_BATCH >> 32;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000040;
	addr_bo_buffer_batch[b++] = 0x00000001;
	addr_bo_buffer_batch[b++] = 0x00000001;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = MI_BATCH_BUFFER_END;
}

/**
 * xehp_compute_exec - run a pipeline compatible with XEHP
 *
 * @fd: file descriptor of the opened DRM device
 * @kernel: GPU Kernel binary to be executed
 * @size: size of @kernel.
 */
static void xehp_compute_exec(int fd, const unsigned char *kernel,
			     unsigned int size)
{
#define XEHP_BO_DICT_ENTRIES 9
	struct bo_dict_entry bo_dict[XEHP_BO_DICT_ENTRIES] = {
		{ .addr = XEHP_ADDR_INSTRUCTION_STATE_BASE + OFFSET_KERNEL,
		  .name = "instr state base"},
		{ .addr = ADDR_DYNAMIC_STATE_BASE,
		  .size = 0x100000,
		  .name = "dynamic state base"},
		{ .addr = ADDR_SURFACE_STATE_BASE,
		  .size = 0x1000,
		  .name = "surface state base"},
		{ .addr = XEHP_ADDR_GENERAL_STATE_BASE + OFFSET_INDIRECT_DATA_START,
		  .size =  0x1000,
		  .name = "indirect object base"},
		{ .addr = ADDR_INPUT, .size = SIZE_BUFFER_INPUT,
		  .name = "addr input"},
		{ .addr = ADDR_OUTPUT, .size = SIZE_BUFFER_OUTPUT,
		  .name = "addr output" },
		{ .addr = XEHP_ADDR_GENERAL_STATE_BASE, .size = 0x100000,
		  .name = "general state base" },
		{ .addr = ADDR_SURFACE_STATE_BASE + XEHP_OFFSET_BINDING_TABLE,
		  .size = 0x1000,
		  .name = "binding table" },
		{ .addr = ADDR_BATCH, .size = SIZE_BATCH,
		  .name = "batch" },
	};
	struct bo_execenv execenv;
	float *dinput;

	bo_execenv_create(fd, &execenv);

	/* Sets Kernel size */
	bo_dict[0].size = ALIGN(size, 0x1000);

	bo_execenv_bind(&execenv, bo_dict, XEHP_BO_DICT_ENTRIES);

	memcpy(bo_dict[0].data, kernel, size);
	create_dynamic_state(bo_dict[1].data, OFFSET_KERNEL);
	xehp_create_surface_state(bo_dict[2].data, ADDR_INPUT, ADDR_OUTPUT);
	xehp_create_indirect_data(bo_dict[3].data, ADDR_INPUT, ADDR_OUTPUT);
	xehp_create_surface_state(bo_dict[7].data, ADDR_INPUT, ADDR_OUTPUT);

	dinput = (float *)bo_dict[4].data;
	srand(time(NULL));
	for (int i = 0; i < SIZE_DATA; i++)
		((float *)dinput)[i] = rand() / (float)RAND_MAX;

	xehp_compute_exec_compute(bo_dict[8].data,
				  XEHP_ADDR_GENERAL_STATE_BASE,
				  ADDR_SURFACE_STATE_BASE,
				  ADDR_DYNAMIC_STATE_BASE,
				  XEHP_ADDR_INSTRUCTION_STATE_BASE,
				  OFFSET_INDIRECT_DATA_START,
				  OFFSET_KERNEL);

	bo_execenv_exec(&execenv, ADDR_BATCH);

	for (int i = 0; i < SIZE_DATA; i++) {
		float f1, f2;

		f1 = ((float *) bo_dict[5].data)[i];
		f2 = ((float *) bo_dict[4].data)[i];
		if (f1 != f2 * f2)
			igt_debug("[%4d] f1: %f != %f\n", i, f1, f2 * f2);
		igt_assert(f1 == f2 * f2);
	}

	bo_execenv_unbind(&execenv, bo_dict, XEHP_BO_DICT_ENTRIES);
	bo_execenv_destroy(&execenv);
}

static void xehpc_create_indirect_data(uint32_t *addr_bo_buffer_batch,
				       uint64_t addr_input,
				       uint64_t addr_output)
{
	int b = 0;

	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000400;
	addr_bo_buffer_batch[b++] = 0x00000001;
	addr_bo_buffer_batch[b++] = 0x00000001;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = addr_input & 0xffffffff;
	addr_bo_buffer_batch[b++] = addr_input >> 32;
	addr_bo_buffer_batch[b++] = addr_output & 0xffffffff;
	addr_bo_buffer_batch[b++] = addr_output >> 32;
	addr_bo_buffer_batch[b++] = 0x00000400;
	addr_bo_buffer_batch[b++] = 0x00000400;
	addr_bo_buffer_batch[b++] = 0x00000001;
	addr_bo_buffer_batch[b++] = 0x00000001;
}

static void xehpc_compute_exec_compute(uint32_t *addr_bo_buffer_batch,
				       uint64_t addr_general_state_base,
				       uint64_t addr_surface_state_base,
				       uint64_t addr_dynamic_state_base,
				       uint64_t addr_instruction_state_base,
				       uint64_t offset_indirect_data_start,
				       uint64_t kernel_start_pointer)
{
	int b = 0;

	igt_debug("general   state base: %lx\n", addr_general_state_base);
	igt_debug("surface   state base: %lx\n", addr_surface_state_base);
	igt_debug("dynamic   state base: %lx\n", addr_dynamic_state_base);
	igt_debug("instruct   base addr: %lx\n", addr_instruction_state_base);
	igt_debug("bindless   base addr: %lx\n", addr_surface_state_base);
	igt_debug("offset indirect addr: %lx\n", offset_indirect_data_start);
	igt_debug("kernel start pointer: %lx\n", kernel_start_pointer);

	addr_bo_buffer_batch[b++] = GEN7_PIPELINE_SELECT | GEN9_PIPELINE_SELECTION_MASK |
				    PIPELINE_SELECT_GPGPU;

	addr_bo_buffer_batch[b++] = XEHP_STATE_COMPUTE_MODE;
	addr_bo_buffer_batch[b++] = 0xE0186010;

	addr_bo_buffer_batch[b++] = XEHP_CFE_STATE | 0x4;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x10008800;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = MI_LOAD_REGISTER_IMM(1);
	addr_bo_buffer_batch[b++] = 0x00002580;
	addr_bo_buffer_batch[b++] = 0x00060002;

	addr_bo_buffer_batch[b++] = STATE_BASE_ADDRESS | 0x14;
	addr_bo_buffer_batch[b++] = (addr_general_state_base & 0xffffffff) | 0x41;
	addr_bo_buffer_batch[b++] = addr_general_state_base >> 32;
	addr_bo_buffer_batch[b++] = 0x00044000;
	addr_bo_buffer_batch[b++] = (addr_surface_state_base & 0xffffffff) | 0x41;
	addr_bo_buffer_batch[b++] = addr_surface_state_base >> 32;
	addr_bo_buffer_batch[b++] = (addr_dynamic_state_base & 0xffffffff) | 0x41;
	addr_bo_buffer_batch[b++] = addr_dynamic_state_base >> 32;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = (addr_instruction_state_base & 0xffffffff) | 0x41;
	addr_bo_buffer_batch[b++] = addr_instruction_state_base >> 32;
	addr_bo_buffer_batch[b++] = 0xfffff001;
	addr_bo_buffer_batch[b++] = 0x00010001;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0xfffff001;
	addr_bo_buffer_batch[b++] = (addr_surface_state_base & 0xffffffff) | 0x41;
	addr_bo_buffer_batch[b++] = addr_surface_state_base >> 32;
	addr_bo_buffer_batch[b++] = 0x00007fbf;
	addr_bo_buffer_batch[b++] = 0x00000041;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = GEN8_3DSTATE_BINDING_TABLE_POOL_ALLOC | 2;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = XEHP_COMPUTE_WALKER | 0x25;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000040;
	addr_bo_buffer_batch[b++] = offset_indirect_data_start;
	addr_bo_buffer_batch[b++] = 0xbe040000;
	addr_bo_buffer_batch[b++] = 0xffffffff;
	addr_bo_buffer_batch[b++] = 0x0000003f;
	addr_bo_buffer_batch[b++] = 0x00000010;

	addr_bo_buffer_batch[b++] = 0x00000001;
	addr_bo_buffer_batch[b++] = 0x00000001;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = kernel_start_pointer;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00180000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x0c000020;

	addr_bo_buffer_batch[b++] = 0x00000008;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00001047;
	addr_bo_buffer_batch[b++] = ADDR_BATCH;
	addr_bo_buffer_batch[b++] = ADDR_BATCH >> 32;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000040;
	addr_bo_buffer_batch[b++] = 0x00000001;
	addr_bo_buffer_batch[b++] = 0x00000001;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = MI_BATCH_BUFFER_END;
}

/**
 * xehpc_compute_exec - run a pipeline compatible with XEHP
 *
 * @fd: file descriptor of the opened DRM device
 * @kernel: GPU Kernel binary to be executed
 * @size: size of @kernel.
 */
static void xehpc_compute_exec(int fd, const unsigned char *kernel,
			       unsigned int size)
{
#define XEHPC_BO_DICT_ENTRIES 6
	struct bo_dict_entry bo_dict[XEHPC_BO_DICT_ENTRIES] = {
		{ .addr = XEHP_ADDR_INSTRUCTION_STATE_BASE + OFFSET_KERNEL,
		  .name = "instr state base"},
		{ .addr = XEHP_ADDR_GENERAL_STATE_BASE + OFFSET_INDIRECT_DATA_START,
		  .size =  0x10000,
		  .name = "indirect object base"},
		{ .addr = ADDR_INPUT, .size = SIZE_BUFFER_INPUT,
		  .name = "addr input"},
		{ .addr = ADDR_OUTPUT, .size = SIZE_BUFFER_OUTPUT,
		  .name = "addr output" },
		{ .addr = XEHP_ADDR_GENERAL_STATE_BASE, .size = 0x10000,
		  .name = "general state base" },
		{ .addr = ADDR_BATCH, .size = SIZE_BATCH,
		  .name = "batch" },
	};
	struct bo_execenv execenv;
	float *dinput;

	bo_execenv_create(fd, &execenv);

	/* Sets Kernel size */
	bo_dict[0].size = ALIGN(size, 0x1000);

	bo_execenv_bind(&execenv, bo_dict, XEHPC_BO_DICT_ENTRIES);

	memcpy(bo_dict[0].data, kernel, size);
	xehpc_create_indirect_data(bo_dict[1].data, ADDR_INPUT, ADDR_OUTPUT);

	dinput = (float *)bo_dict[2].data;
	srand(time(NULL));
	for (int i = 0; i < SIZE_DATA; i++)
		((float *)dinput)[i] = rand() / (float)RAND_MAX;

	xehpc_compute_exec_compute(bo_dict[5].data,
				   XEHP_ADDR_GENERAL_STATE_BASE,
				   ADDR_SURFACE_STATE_BASE,
				   ADDR_DYNAMIC_STATE_BASE,
				   XEHP_ADDR_INSTRUCTION_STATE_BASE,
				   OFFSET_INDIRECT_DATA_START,
				   OFFSET_KERNEL);

	bo_execenv_exec(&execenv, ADDR_BATCH);

	for (int i = 0; i < SIZE_DATA; i++) {
		float f1, f2;

		f1 = ((float *) bo_dict[3].data)[i];
		f2 = ((float *) bo_dict[2].data)[i];
		if (f1 != f2 * f2)
			igt_debug("[%4d] f1: %f != %f\n", i, f1, f2 * f2);
		igt_assert(f1 == f2 * f2);
	}

	bo_execenv_unbind(&execenv, bo_dict, XEHPC_BO_DICT_ENTRIES);
	bo_execenv_destroy(&execenv);
}

/*
 * Compatibility flags.
 *
 * There will be some time period in which both drivers (i915 and xe)
 * will support compute runtime tests. Lets define compat flags to allow
 * the code to be shared between two drivers allowing disabling this in
 * the future.
 */
#define COMPAT_DRIVER_FLAG(f) (1 << (f))
#define COMPAT_DRIVER_I915 COMPAT_DRIVER_FLAG(INTEL_DRIVER_I915)
#define COMPAT_DRIVER_XE   COMPAT_DRIVER_FLAG(INTEL_DRIVER_XE)

static const struct {
	unsigned int ip_ver;
	void (*compute_exec)(int fd, const unsigned char *kernel,
			     unsigned int size);
	uint32_t compat;
} intel_compute_batches[] = {
	{
		.ip_ver = IP_VER(12, 0),
		.compute_exec = compute_exec,
		.compat = COMPAT_DRIVER_I915 | COMPAT_DRIVER_XE,
	},
	{
		.ip_ver = IP_VER(12, 10),
		.compute_exec = compute_exec,
		.compat = COMPAT_DRIVER_I915,
	},
	{
		.ip_ver = IP_VER(12, 55),
		.compute_exec = xehp_compute_exec,
		.compat = COMPAT_DRIVER_I915,
	},
	{
		.ip_ver = IP_VER(12, 60),
		.compute_exec = xehpc_compute_exec,
		.compat = COMPAT_DRIVER_XE,
	},
};

bool run_intel_compute_kernel(int fd)
{
	unsigned int ip_ver = intel_graphics_ver(intel_get_drm_devid(fd));
	unsigned int batch;
	const struct intel_compute_kernels *kernels = intel_compute_square_kernels;
	enum intel_driver driver = get_intel_driver(fd);

	for (batch = 0; batch < ARRAY_SIZE(intel_compute_batches); batch++) {
		if (ip_ver == intel_compute_batches[batch].ip_ver)
			break;
	}
	if (batch == ARRAY_SIZE(intel_compute_batches))
		return false;

	if (!(COMPAT_DRIVER_FLAG(driver) & intel_compute_batches[batch].compat)) {
		igt_debug("Driver is not supported: flags %x & %x\n",
			  COMPAT_DRIVER_FLAG(driver),
			  intel_compute_batches[batch].compat);
		return false;
	}

	while (kernels->kernel) {
		if (ip_ver == kernels->ip_ver)
			break;
		kernels++;
	}
	if (!kernels->kernel)
		return 1;

	intel_compute_batches[batch].compute_exec(fd, kernels->kernel,
						  kernels->size);

	return true;
}

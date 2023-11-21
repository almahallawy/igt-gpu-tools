// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 *
 * Authors:
 *    Matthew Brost <matthew.brost@intel.com>
 */

#include <string.h>

#include "drmtest.h"
#include "igt.h"
#include "igt_core.h"
#include "igt_syncobj.h"
#include "intel_reg.h"
#include "xe_ioctl.h"
#include "xe_spin.h"

static uint32_t read_timestamp_frequency(int fd, int gt_id)
{
	struct xe_device *dev = xe_device_get(fd);

	igt_assert(dev && dev->gt_list && dev->gt_list->num_gt);
	igt_assert(gt_id >= 0 && gt_id <= dev->gt_list->num_gt);

	return dev->gt_list->gt_list[gt_id].clock_freq;
}

static uint64_t div64_u64_round_up(const uint64_t x, const uint64_t y)
{
	igt_assert(y > 0);
	igt_assert_lte_u64(x, UINT64_MAX - (y - 1));

	return (x + y - 1) / y;
}

/**
 * duration_to_ctx_ticks:
 * @fd: opened device
 * @gt_id: tile id
 * @duration_ns: duration in nanoseconds to be converted to context timestamp ticks
 * @return: duration converted to context timestamp ticks.
 */
uint32_t duration_to_ctx_ticks(int fd, int gt_id, uint64_t duration_ns)
{
	uint32_t f = read_timestamp_frequency(fd, gt_id);
	uint64_t ctx_ticks = div64_u64_round_up(duration_ns * f, NSEC_PER_SEC);

	igt_assert_lt_u64(ctx_ticks, XE_SPIN_MAX_CTX_TICKS);

	return ctx_ticks;
}

#define MI_SRM_CS_MMIO				(1 << 19)
#define MI_LRI_CS_MMIO				(1 << 19)
#define MI_LRR_DST_CS_MMIO			(1 << 19)
#define MI_LRR_SRC_CS_MMIO			(1 << 18)
#define CTX_TIMESTAMP 0x3a8
#define CS_GPR(x) (0x600 + 8 * (x))

enum { START_TS, NOW_TS };

/**
 * xe_spin_init:
 * @spin: pointer to mapped bo in which spinner code will be written
 * @opts: pointer to spinner initialization options
 */
void xe_spin_init(struct xe_spin *spin, struct xe_spin_opts *opts)
{
	uint64_t loop_addr;
	uint64_t start_addr = opts->addr + offsetof(struct xe_spin, start);
	uint64_t end_addr = opts->addr + offsetof(struct xe_spin, end);
	uint64_t ticks_delta_addr = opts->addr + offsetof(struct xe_spin, ticks_delta);
	uint64_t pad_addr = opts->addr + offsetof(struct xe_spin, pad);
	int b = 0;

	spin->start = 0;
	spin->end = 0xffffffff;
	spin->ticks_delta = 0;

	if (opts->ctx_ticks) {
		/* store start timestamp */
		spin->batch[b++] = MI_LOAD_REGISTER_IMM(1) | MI_LRI_CS_MMIO;
		spin->batch[b++] = CS_GPR(START_TS) + 4;
		spin->batch[b++] = 0;
		spin->batch[b++] = MI_LOAD_REGISTER_REG | MI_LRR_DST_CS_MMIO | MI_LRR_SRC_CS_MMIO;
		spin->batch[b++] = CTX_TIMESTAMP;
		spin->batch[b++] = CS_GPR(START_TS);
	}

	loop_addr = opts->addr + b * sizeof(uint32_t);

	spin->batch[b++] = MI_STORE_DWORD_IMM_GEN4;
	spin->batch[b++] = start_addr;
	spin->batch[b++] = start_addr >> 32;
	spin->batch[b++] = 0xc0ffee;

	if (opts->preempt)
		spin->batch[b++] = (0x5 << 23);

	if (opts->ctx_ticks) {
		spin->batch[b++] = MI_LOAD_REGISTER_IMM(1) | MI_LRI_CS_MMIO;
		spin->batch[b++] = CS_GPR(NOW_TS) + 4;
		spin->batch[b++] = 0;
		spin->batch[b++] = MI_LOAD_REGISTER_REG | MI_LRR_DST_CS_MMIO | MI_LRR_SRC_CS_MMIO;
		spin->batch[b++] = CTX_TIMESTAMP;
		spin->batch[b++] = CS_GPR(NOW_TS);

		/* delta = now - start; inverted to match COND_BBE */
		spin->batch[b++] = MI_MATH(4);
		spin->batch[b++] = MI_MATH_LOAD(MI_MATH_REG_SRCA, MI_MATH_REG(NOW_TS));
		spin->batch[b++] = MI_MATH_LOAD(MI_MATH_REG_SRCB, MI_MATH_REG(START_TS));
		spin->batch[b++] = MI_MATH_SUB;
		spin->batch[b++] = MI_MATH_STOREINV(MI_MATH_REG(NOW_TS), MI_MATH_REG_ACCU);

		/* Save delta for reading by COND_BBE */
		spin->batch[b++] = MI_STORE_REGISTER_MEM | MI_SRM_CS_MMIO | 2;
		spin->batch[b++] = CS_GPR(NOW_TS);
		spin->batch[b++] = ticks_delta_addr;
		spin->batch[b++] = ticks_delta_addr >> 32;

		/* Delay between SRM and COND_BBE to post the writes */
		for (int n = 0; n < 8; n++) {
			spin->batch[b++] = MI_STORE_DWORD_IMM_GEN4;
			spin->batch[b++] = pad_addr;
			spin->batch[b++] = pad_addr >> 32;
			spin->batch[b++] = 0xc0ffee;
		}

		/* Break if delta [time elapsed] > ns */
		spin->batch[b++] = MI_COND_BATCH_BUFFER_END | MI_DO_COMPARE | 2;
		spin->batch[b++] = ~(opts->ctx_ticks);
		spin->batch[b++] = ticks_delta_addr;
		spin->batch[b++] = ticks_delta_addr >> 32;
	}

	spin->batch[b++] = MI_COND_BATCH_BUFFER_END | MI_DO_COMPARE | 2;
	spin->batch[b++] = 0;
	spin->batch[b++] = end_addr;
	spin->batch[b++] = end_addr >> 32;

	spin->batch[b++] = MI_BATCH_BUFFER_START | 1 << 8 | 1;
	spin->batch[b++] = loop_addr;
	spin->batch[b++] = loop_addr >> 32;

	igt_assert(b <= ARRAY_SIZE(spin->batch));
}

/**
 * xe_spin_started:
 * @spin: pointer to spinner mapped bo
 *
 * Returns: true if spinner is running, othwerwise false.
 */
bool xe_spin_started(struct xe_spin *spin)
{
	return spin->start != 0;
}

/**
 * xe_spin_wait_started:
 * @spin: pointer to spinner mapped bo
 *
 * Wait in userspace code until spinner won't start.
 */
void xe_spin_wait_started(struct xe_spin *spin)
{
	while (!xe_spin_started(spin))
		;
}

void xe_spin_end(struct xe_spin *spin)
{
	spin->end = 0;
}

/**
 * xe_spin_create:
 *@opt: controlling options such as allocator handle, exec_queue, vm etc
 *
 * igt_spin_new for xe, xe_spin_create submits a batch using xe_spin_init
 * which wraps around vm bind and unbinding the object associated to it.
 * This returs a spinner after submitting a dummy load.
 *
 */
igt_spin_t *
xe_spin_create(int fd, const struct igt_spin_factory *opt)
{
	size_t bo_size = xe_get_default_alignment(fd);
	uint64_t ahnd = opt->ahnd, addr;
	struct igt_spin *spin;
	struct xe_spin *xe_spin;
	struct drm_xe_sync sync = {
		.type = DRM_XE_SYNC_TYPE_SYNCOBJ, .flags = DRM_XE_SYNC_FLAG_SIGNAL,
	};
	struct drm_xe_exec exec = {
		.num_batch_buffer = 1,
		.num_syncs = 1,
		.syncs = to_user_pointer(&sync),
	};

	igt_assert(ahnd);
	spin = calloc(1, sizeof(struct igt_spin));
	igt_assert(spin);

	spin->driver = INTEL_DRIVER_XE;
	spin->syncobj = syncobj_create(fd, 0);
	spin->vm = opt->vm;
	spin->engine = opt->engine;
	spin->timerfd = -1;

	if (!spin->vm)
		spin->vm = xe_vm_create(fd, 0, 0);

	if (!spin->engine) {
		if (opt->hwe)
			spin->engine = xe_exec_queue_create(fd, spin->vm, opt->hwe, 0);
		else
			spin->engine = xe_exec_queue_create_class(fd, spin->vm, DRM_XE_ENGINE_CLASS_COPY);
	}

	spin->handle = xe_bo_create(fd, spin->vm, bo_size,
				    vram_if_possible(fd, 0),
				    DRM_XE_GEM_CREATE_FLAG_NEEDS_VISIBLE_VRAM);
	xe_spin = xe_bo_map(fd, spin->handle, bo_size);
	addr = intel_allocator_alloc_with_strategy(ahnd, spin->handle, bo_size, 0, ALLOC_STRATEGY_LOW_TO_HIGH);
	xe_vm_bind_sync(fd, spin->vm, spin->handle, 0, addr, bo_size);

	xe_spin_init_opts(xe_spin, .addr = addr, .preempt = !(opt->flags & IGT_SPIN_NO_PREEMPTION));
	exec.exec_queue_id = spin->engine;
	exec.address = addr;
	sync.handle = spin->syncobj;
	igt_assert_eq(igt_ioctl(fd, DRM_IOCTL_XE_EXEC, &exec), 0);
	xe_spin_wait_started(xe_spin);

	spin->bo_size = bo_size;
	spin->address = addr;
	spin->xe_spin = xe_spin;
	spin->opts = *opt;

	return spin;
}

void xe_spin_sync_wait(int fd, struct igt_spin *spin)
{
	igt_assert(syncobj_wait(fd, &spin->syncobj, 1, INT64_MAX, 0, NULL));
}

/*
 * xe_spin_free:
 *@spin: spin state from igt_spin_new()
 *
 * Wrapper to free spinner whhich is triggered by xe_spin_create.
 * which distroys vm, exec_queue and unbinds the vm which is binded to
 * the exec_queue and bo.
 *
 */
void xe_spin_free(int fd, struct igt_spin *spin)
{
	igt_assert(spin->driver == INTEL_DRIVER_XE);

	if (spin->timerfd >= 0) {
		pthread_cancel(spin->timer_thread);
		igt_assert(pthread_join(spin->timer_thread, NULL) == 0);
		close(spin->timerfd);
	}

	xe_spin_end(spin->xe_spin);
	xe_spin_sync_wait(fd, spin);
	xe_vm_unbind_sync(fd, spin->vm, 0, spin->address, spin->bo_size);
	syncobj_destroy(fd, spin->syncobj);
	gem_munmap(spin->xe_spin, spin->bo_size);
	gem_close(fd, spin->handle);

	if (!spin->opts.engine)
		xe_exec_queue_destroy(fd, spin->engine);

	if (!spin->opts.vm)
		xe_vm_destroy(fd, spin->vm);

	free(spin);
}

void xe_cork_init(int fd, struct drm_xe_engine_class_instance *hwe,
		  struct xe_cork *cork)
{
	uint64_t addr = xe_get_default_alignment(fd);
	size_t bo_size = xe_get_default_alignment(fd);
	uint32_t vm, bo, exec_queue, syncobj;
	struct xe_spin *spin;
	struct drm_xe_sync sync = {
		.type = DRM_XE_SYNC_TYPE_SYNCOBJ, .flags = DRM_XE_SYNC_FLAG_SIGNAL,
	};
	struct drm_xe_exec exec = {
		.num_batch_buffer = 1,
		.num_syncs = 1,
		.syncs = to_user_pointer(&sync),
	};

	vm = xe_vm_create(fd, 0, 0);

	bo = xe_bo_create(fd, vm, bo_size, vram_if_possible(fd, hwe->gt_id),
			  DRM_XE_GEM_CREATE_FLAG_NEEDS_VISIBLE_VRAM);
	spin = xe_bo_map(fd, bo, 0x1000);

	xe_vm_bind_sync(fd, vm, bo, 0, addr, bo_size);

	exec_queue = xe_exec_queue_create(fd, vm, hwe, 0);
	syncobj = syncobj_create(fd, 0);

	xe_spin_init_opts(spin, .addr = addr, .preempt = true);
	exec.exec_queue_id = exec_queue;
	exec.address = addr;
	sync.handle = syncobj;
	igt_assert_eq(igt_ioctl(fd, DRM_IOCTL_XE_EXEC, &exec), 0);

	cork->spin = spin;
	cork->fd = fd;
	cork->vm = vm;
	cork->bo = bo;
	cork->exec_queue = exec_queue;
	cork->syncobj = syncobj;
}

bool xe_cork_started(struct xe_cork *cork)
{
	return xe_spin_started(cork->spin);
}

void xe_cork_wait_started(struct xe_cork *cork)
{
	xe_spin_wait_started(cork->spin);
}

void xe_cork_end(struct xe_cork *cork)
{
	xe_spin_end(cork->spin);
}

void xe_cork_wait_done(struct xe_cork *cork)
{
	igt_assert(syncobj_wait(cork->fd, &cork->syncobj, 1, INT64_MAX, 0,
				NULL));
}

void xe_cork_fini(struct xe_cork *cork)
{
	syncobj_destroy(cork->fd, cork->syncobj);
	xe_exec_queue_destroy(cork->fd, cork->exec_queue);
	xe_vm_destroy(cork->fd, cork->vm);
	gem_close(cork->fd, cork->bo);
}

uint32_t xe_cork_sync_handle(struct xe_cork *cork)
{
	return cork->syncobj;
}

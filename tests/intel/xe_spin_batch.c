#include "igt.h"
#include "igt_syncobj.h"
#include "lib/intel_reg.h"
#include "xe_drm.h"
#include "xe/xe_ioctl.h"
#include "xe/xe_query.h"
#include "xe/xe_spin.h"

/**
 * TEST: Tests for spin batch submissons.
 * Category: Software building block
 * Sub-category: spin
 * Functionality: parallel execution
 * Test category: functionality test
 */

#define MAX_INSTANCE 9

/**
 * SUBTEST: spin-basic
 * Description: Basic test to submit spin batch submissons on copy engine.
 */

static void spin_basic(int fd)
{
	uint64_t ahnd;
	igt_spin_t *spin;

	ahnd = intel_allocator_open(fd, 0, INTEL_ALLOCATOR_RELOC);
	spin = igt_spin_new(fd, .ahnd = ahnd);

	igt_spin_free(fd, spin);
	put_ahnd(ahnd);
}

/**
 * SUBTEST: spin-batch
 * Description: Create vm and engine of hwe class and run the spinner on it.
 */

static void spin(int fd, struct drm_xe_engine_class_instance *hwe)
{
	uint64_t ahnd;
	unsigned int exec_queue;
	uint32_t vm;
	igt_spin_t *spin;

	vm = xe_vm_create(fd, 0, 0);
	exec_queue = xe_exec_queue_create(fd, vm, hwe, 0);
	ahnd = intel_allocator_open(fd, 0, INTEL_ALLOCATOR_RELOC);

	spin = igt_spin_new(fd, .ahnd = ahnd, .engine = exec_queue, .vm = vm);

	igt_spin_free(fd, spin);
	xe_exec_queue_destroy(fd, exec_queue);
	xe_vm_destroy(fd, vm);

	put_ahnd(ahnd);
}

/**
 * SUBTEST: spin-basic-all
 * Description: Basic test which validates the functionality of spinner on all hwe.
 */
static void spin_basic_all(int fd)
{
	struct drm_xe_engine_class_instance *hwe;
	uint64_t ahnd;
	uint32_t vm;
	igt_spin_t **spin;
	int i = 0;

	vm = xe_vm_create(fd, 0, 0);
	ahnd = intel_allocator_open(fd, vm, INTEL_ALLOCATOR_RELOC);
	spin = malloc(sizeof(*spin) * xe_number_engines(fd));
	xe_for_each_engine(fd, hwe) {
		igt_debug("Run on engine: %s:%d\n",
			  xe_engine_class_string(hwe->engine_class), hwe->engine_instance);
		spin[i] = igt_spin_new(fd, .ahnd = ahnd, .vm = vm, .hwe = hwe);
		i++;
	}

	while (--i >= 0)
		igt_spin_free(fd, spin[i]);

	put_ahnd(ahnd);
	xe_vm_destroy(fd, vm);
	free(spin);
}

/**
 * SUBTEST: spin-all
 * Description: Spinner test to run on all the engines!
 */

static void spin_all(int fd, int gt, int class)
{
	uint64_t ahnd;
	uint32_t exec_queues[MAX_INSTANCE], vm;
	int i, num_placements = 0;
	struct drm_xe_engine_class_instance eci[MAX_INSTANCE];
	igt_spin_t *spin[MAX_INSTANCE];
	struct drm_xe_engine_class_instance *hwe;

	ahnd = intel_allocator_open(fd, 0, INTEL_ALLOCATOR_RELOC);

	xe_for_each_engine(fd, hwe) {
		if (hwe->engine_class != class || hwe->gt_id != gt)
			continue;
		eci[num_placements++] = *hwe;
	}
	if (num_placements < 2)
		return;
	vm = xe_vm_create(fd, 0, 0);

	for (i = 0; i < num_placements; i++) {
		struct drm_xe_exec_queue_create create = {
			.vm_id = vm,
			.width = 1,
			.num_placements = num_placements,
			.instances = to_user_pointer(eci),
		};

		igt_assert_eq(igt_ioctl(fd, DRM_IOCTL_XE_EXEC_QUEUE_CREATE,
					&create), 0);
		exec_queues[i] = create.exec_queue_id;
		spin[i] = igt_spin_new(fd, .ahnd = ahnd, .engine = exec_queues[i], .vm = vm);
	}

	for (i = 0; i < num_placements; i++) {
		igt_spin_free(fd, spin[i]);
		xe_exec_queue_destroy(fd, exec_queues[i]);
	}

	put_ahnd(ahnd);
	xe_vm_destroy(fd, vm);
}

struct data {
	uint32_t batch[16];
	uint64_t pad;
	uint32_t data;
	uint64_t addr;
};

static void store_dword_batch(struct data *data, uint64_t addr, int value)
{
	int b;
	uint64_t batch_offset = (char *)&(data->batch) - (char *)data;
	uint64_t batch_addr = addr + batch_offset;
	uint64_t sdi_offset = (char *)&(data->data) - (char *)data;
	uint64_t sdi_addr = addr + sdi_offset;

	b = 0;
	data->batch[b++] = MI_STORE_DWORD_IMM_GEN4;
	data->batch[b++] = sdi_addr;
	data->batch[b++] = sdi_addr >> 32;
	data->batch[b++] = value;
	data->batch[b++] = MI_BATCH_BUFFER_END;
	igt_assert(b <= ARRAY_SIZE(data->batch));

	data->addr = batch_addr;
}

static void preempter(int fd, struct drm_xe_engine_class_instance *hwe)
{
	struct drm_xe_sync sync = {
		.flags = DRM_XE_SYNC_FLAG_SYNCOBJ | DRM_XE_SYNC_FLAG_SIGNAL
	};
	struct drm_xe_exec exec = {
		.num_batch_buffer = 1,
		.num_syncs = 1,
		.syncs = to_user_pointer(&sync),
	};
	struct drm_xe_ext_set_property ext = {
		.base.next_extension = 0,
		.base.name = DRM_XE_EXEC_QUEUE_EXTENSION_SET_PROPERTY,
		.property = DRM_XE_EXEC_QUEUE_SET_PROPERTY_PRIORITY,
		.value = 2, /* High priority */
	};
	struct data *data;
	uint32_t vm;
	uint32_t exec_queue;
	uint32_t syncobj;
	size_t bo_size;
	int value = 0x123456;
	uint64_t addr = 0x100000;
	uint32_t bo = 0;

	syncobj = syncobj_create(fd, 0);
	sync.handle = syncobj;

	vm = xe_vm_create(fd, DRM_XE_VM_CREATE_FLAG_ASYNC_DEFAULT, 0);
	bo_size = sizeof(*data);
	bo_size = ALIGN(bo_size + xe_cs_prefetch_size(fd),
			xe_get_default_alignment(fd));

	bo = xe_bo_create_flags(fd, vm, bo_size,
				visible_vram_if_possible(fd, hwe->gt_id));

	xe_vm_bind_async(fd, vm, hwe->gt_id, bo, 0, addr, bo_size, &sync, 1);
	data = xe_bo_map(fd, bo, bo_size);
	store_dword_batch(data, addr, value);

	exec_queue = xe_exec_queue_create(fd, vm, hwe, to_user_pointer(&ext));
	exec.exec_queue_id = exec_queue;
	exec.address = data->addr;
	sync.flags &= DRM_XE_SYNC_FLAG_SIGNAL;
	xe_exec(fd, &exec);

	igt_assert(syncobj_wait(fd, &syncobj, 1, INT64_MAX, 0, NULL));
	igt_assert_eq(data->data, value);

	syncobj_destroy(fd, syncobj);
	munmap(data, bo_size);
	gem_close(fd, bo);

	xe_exec_queue_destroy(fd, exec_queue);
	xe_vm_destroy(fd, vm);
}

#define SPIN_FIX_DURATION_NORMAL		0
#define SPIN_FIX_DURATION_PREEMPT		1
/**
 * SUBTEST: spin-fixed-duration
 * Description: Basic test which validates the functionality of xe_spin with fixed duration.
 * Run type: FULL
 */
/**
 * SUBTEST: spin-fixed-duration-with-preempter
 * Description: Basic test which validates the functionality of xe_spin preemption which gets preempted with a short duration high-priority task.
 * Run type: FULL
 */
static void xe_spin_fixed_duration(int fd, int gt, int class, int flags)
{
	struct drm_xe_sync sync = {
		.handle = syncobj_create(fd, 0),
		.type = DRM_XE_SYNC_TYPE_SYNCOBJ,
		.flags = DRM_XE_SYNC_FLAG_SIGNAL,
	};
	struct drm_xe_exec exec = {
		.num_batch_buffer = 1,
		.num_syncs = 1,
		.syncs = to_user_pointer(&sync),
	};
	struct drm_xe_ext_set_property ext_prio = {
		.base.next_extension = 0,
		.base.name = DRM_XE_EXEC_QUEUE_EXTENSION_SET_PROPERTY,
		.property = DRM_XE_EXEC_QUEUE_SET_PROPERTY_PRIORITY,
		.value = 0, /* Low priority */
	};
	struct drm_xe_engine_class_instance *hwe = NULL, *_hwe;
	const uint64_t duration_ns = NSEC_PER_SEC / 10; /* 100ms */
	uint64_t spin_addr;
	uint64_t ahnd;
	uint32_t exec_queue;
	uint32_t vm;
	uint32_t bo;
	uint64_t ext = 0;
	size_t bo_size;
	struct xe_spin *spin;
	struct timespec tv;
	double elapsed_ms;
	igt_stats_t stats;
	int i;

	if (flags & SPIN_FIX_DURATION_PREEMPT)
		ext = to_user_pointer(&ext_prio);

	xe_for_each_hw_engine(fd, _hwe)
		if (_hwe->engine_class == class && _hwe->gt_id == gt)
			hwe = _hwe;

	if (!hwe)
		return;

	vm = xe_vm_create(fd, 0, 0);
	exec_queue = xe_exec_queue_create(fd, vm, hwe, ext);
	ahnd = intel_allocator_open(fd, 0, INTEL_ALLOCATOR_RELOC);
	bo_size = ALIGN(sizeof(*spin) + xe_cs_prefetch_size(fd), xe_get_default_alignment(fd));
	bo = xe_bo_create(fd, vm, bo_size, vram_if_possible(fd, 0), 0);
	spin = xe_bo_map(fd, bo, bo_size);
	spin_addr = intel_allocator_alloc_with_strategy(ahnd, bo, bo_size, 0,
							ALLOC_STRATEGY_LOW_TO_HIGH);
	xe_vm_bind_sync(fd, vm, bo, 0, spin_addr, bo_size);
	xe_spin_init_opts(spin, .addr = spin_addr,
				.preempt = true,
				.ctx_ticks = duration_to_ctx_ticks(fd, 0, duration_ns));
	exec.address = spin_addr;
	exec.exec_queue_id = exec_queue;

#define NSAMPLES 5
	igt_stats_init_with_size(&stats, NSAMPLES);
	for (i = 0; i < NSAMPLES; ++i) {
		igt_gettime(&tv);
		xe_exec(fd, &exec);
		xe_spin_wait_started(spin);
		if (flags & SPIN_FIX_DURATION_PREEMPT)
			preempter(fd, hwe);

		igt_assert(syncobj_wait(fd, &sync.handle, 1, INT64_MAX, 0, NULL));
		igt_stats_push_float(&stats, igt_nsec_elapsed(&tv) * 1e-6);
		syncobj_reset(fd, &sync.handle, 1);
		igt_debug("i=%d %.2fms\n", i, stats.values_f[i]);
	}
	elapsed_ms = igt_stats_get_median(&stats);
	igt_info("%s: %.0fms spin took %.2fms (median)\n", xe_engine_class_string(hwe->engine_class),
		 duration_ns * 1e-6, elapsed_ms);
	igt_assert(elapsed_ms < duration_ns * 1.5e-6 && elapsed_ms > duration_ns * 0.5e-6);

	xe_vm_unbind_sync(fd, vm, 0, spin_addr, bo_size);
	syncobj_destroy(fd, sync.handle);
	gem_munmap(spin, bo_size);
	gem_close(fd, bo);
	xe_exec_queue_destroy(fd, exec_queue);
	xe_vm_destroy(fd, vm);
	put_ahnd(ahnd);
}

igt_main
{
	struct drm_xe_engine_class_instance *hwe;
	int fd;
	int gt, class;

	igt_fixture
		fd = drm_open_driver(DRIVER_XE);

	igt_subtest("spin-basic")
		spin_basic(fd);

	igt_subtest("spin-batch")
		xe_for_each_engine(fd, hwe)
			spin(fd, hwe);

	igt_subtest("spin-basic-all")
		spin_basic_all(fd);

	igt_subtest("spin-all") {
		xe_for_each_gt(fd, gt)
			xe_for_each_engine_class(class)
				spin_all(fd, gt, class);
	}

	igt_subtest("spin-fixed-duration")
		xe_spin_fixed_duration(fd, 0, DRM_XE_ENGINE_CLASS_COPY, SPIN_FIX_DURATION_NORMAL);


	igt_subtest("spin-fixed-duration-with-preempter")
		xe_for_each_gt(fd, gt)
			xe_for_each_hw_engine_class(class)
				xe_spin_fixed_duration(fd, gt, class, SPIN_FIX_DURATION_PREEMPT);

	igt_fixture
		drm_close_driver(fd);
}

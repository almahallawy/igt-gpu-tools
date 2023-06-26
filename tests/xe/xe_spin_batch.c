#include "igt.h"
#include "lib/intel_reg.h"
#include "xe_drm.h"
#include "xe/xe_ioctl.h"
#include "xe/xe_query.h"

/**
 * TEST: Tests for spin batch submissons.
 * Category: Software building block
 * Sub-category: spin
 * Functionality: intel-bb
 * Test category: functionality test
 */

#define MAX_INSTANCE 9

/**
 * SUBTEST: spin-basic
 * Description: Basic test to submit spin batch submissons on copy engine.
 * Run type: FULL
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
 * Run type: FULL
 */

static void spin(int fd, struct drm_xe_engine_class_instance *hwe)
{
	uint64_t ahnd;
	unsigned int engine;
	uint32_t vm;
	igt_spin_t *spin;

	vm = xe_vm_create(fd, 0, 0);
	engine = xe_engine_create(fd, vm, hwe, 0);
	ahnd = intel_allocator_open(fd, 0, INTEL_ALLOCATOR_RELOC);

	spin = igt_spin_new(fd, .ahnd = ahnd, .engine = engine, .vm = vm);

	igt_spin_free(fd, spin);
	xe_engine_destroy(fd, engine);
	xe_vm_destroy(fd, vm);

	put_ahnd(ahnd);
}

/**
 * SUBTEST: spin-basic-all
 * Description: Basic test which validates the functionality of spinner on all hwe.
 * Run type: FULL
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
	spin = malloc(sizeof(*spin) * xe_number_hw_engines(fd));
	xe_for_each_hw_engine(fd, hwe) {
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
 * Run type: FULL
 */

static void spin_all(int fd, int gt, int class)
{
	uint64_t ahnd;
	uint32_t engines[MAX_INSTANCE], vm;
	int i, num_placements = 0;
	struct drm_xe_engine_class_instance eci[MAX_INSTANCE];
	igt_spin_t *spin[MAX_INSTANCE];
	struct drm_xe_engine_class_instance *hwe;

	ahnd = intel_allocator_open(fd, 0, INTEL_ALLOCATOR_RELOC);

	xe_for_each_hw_engine(fd, hwe) {
		if (hwe->engine_class != class || hwe->gt_id != gt)
			continue;
		eci[num_placements++] = *hwe;
	}
	if (num_placements < 2)
		return;
	vm = xe_vm_create(fd, 0, 0);

	for (i = 0; i < num_placements; i++) {
		struct drm_xe_engine_create create = {
			.vm_id = vm,
			.width = 1,
			.num_placements = num_placements,
			.instances = to_user_pointer(eci),
		};

		igt_assert_eq(igt_ioctl(fd, DRM_IOCTL_XE_ENGINE_CREATE,
					&create), 0);
		engines[i] = create.engine_id;
		spin[i] = igt_spin_new(fd, .ahnd = ahnd, .engine = engines[i], .vm = vm);
	}

	for (i = 0; i < num_placements; i++) {
		igt_spin_free(fd, spin[i]);
		xe_engine_destroy(fd, engines[i]);
	}

	put_ahnd(ahnd);
	xe_vm_destroy(fd, vm);
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
		xe_for_each_hw_engine(fd, hwe)
			spin(fd, hwe);

	igt_subtest("spin-basic-all")
		spin_basic_all(fd);

	igt_subtest("spin-all") {
		xe_for_each_gt(fd, gt)
			xe_for_each_hw_engine_class(class)
				spin_all(fd, gt, class);
	}

	igt_fixture
		drm_close_driver(fd);
}

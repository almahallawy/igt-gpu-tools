#include "igt.h"
#include "igt_kmod.h"

/**
 * TEST: Xe driver live kunit tests
 * Description: Xe driver live dmabuf unit tests
 * Category: Software building block
 * Sub-category: kunit
 * Functionality: kunit
 * Test category: functionality test
 *
 * SUBTEST: bo
 * Description:
 *	Kernel dynamic selftests to check if GPU buffer objects are
 *	being handled properly.
 * Functionality: bo
 *
 * SUBTEST: dmabuf
 * Description: Kernel dynamic selftests for dmabuf functionality.
 * Functionality: dmabuf
 *
 * SUBTEST: migrate
 * Description:
 *	Kernel dynamic selftests to check if page table migrations
 *	are working properly.
 * Functionality: migrate
 */

struct kunit_tests {
	const char *kunit;
	const char *name;
};

static const struct kunit_tests live_tests[] = {
	{ "xe_bo_test",		"bo" },
	{ "xe_dma_buf_test",	"dmabuf" },
	{ "xe_migrate_test",	"migrate" },
};

igt_main
{
	int i;

	for (i = 0; i < ARRAY_SIZE(live_tests); i++)
		igt_kunit(live_tests[i].kunit, live_tests[i].name, NULL);
}

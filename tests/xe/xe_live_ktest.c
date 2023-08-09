#include "igt.h"
#include "igt_kmod.h"

/**
 * TEST: Xe driver live kunit tests
 * Description: Xe driver live dmabuf unit tests
 * Category: Software building block
 * Sub-category: kunit
 * Functionality: kunit
 * Test category: functionality test
 * Run type: BAT, kunit
 *
 * SUBTEST: bo
 * Functionality: bo
 *
 * SUBTEST: dmabuf
 * Functionality: dmabuf
 *
 * SUBTEST: migrate
 * Functionality: migrate
 *
 * SUBTEST: pci
 * Functionality: pci
 *
 * SUBTEST: rtp
 * Functionality: rtp
 *
 * SUBTEST: wa
 * Functionality: workarounds
 */

struct kunit_tests {
	const char *kunit;
	const char *name;
};

static const struct kunit_tests live_tests[] = {
	{ "xe_bo_test",		"bo" },
	{ "xe_dma_buf_test",	"dmabuf" },
	{ "xe_migrate_test",	"migrate" },
	{ "xe_pci_test",	"pci" },
	{ "xe_rtp_test",	"rtp" },
};

igt_main
{
	int i;

	for (i = 0; i < ARRAY_SIZE(live_tests); i++)
		igt_kunit(live_tests[i].kunit, live_tests[i].name, NULL);
}

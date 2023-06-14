/*
 * Copyright © 2018 VMware, Inc., Palo Alto, CA., USA
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "igt.h"
#include "igt_kmod.h"

IGT_TEST_DESCRIPTION("Basic sanity check of KMS selftests.");

struct kms_kunittests {
	const char *kunit;
	const char *name;
};

igt_main
{
	static const struct kms_kunittests kunit_subtests[] = {
		{ "drm_cmdline_parser_test",	"drm_cmdline" },
		{ "drm_damage_helper_test",	"drm_damage" },
		{ "drm_dp_mst_helper_test",	"drm_dp_mst" },
		{ "drm_format_helper_test",	"drm_format_helper" },
		{ "drm_format_test",		"drm_format" },
		{ "drm_framebuffer_test",	"framebuffer" },
		{ "drm_plane_helper_test",	"drm_plane" },
		{ NULL, NULL}
	};

	for (int i = 0; kunit_subtests[i].kunit != NULL; i++)
		igt_kunit(kunit_subtests[i].kunit, kunit_subtests[i].name, NULL);
}

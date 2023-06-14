/*
 * Copyright © 2016 Intel Corporation
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
/**
 * TEST: drm mm
 * Description: Basic sanity check of DRM's range manager (struct drm_mm)
 * Feature: mapping
 * Run type: FULL
 *
 * SUBTEST: drm_mm_test
 *
 * SUBTEST: drm_mm_test@align
 * Category: Infrastructure
 * Description: drm_mm range manager SW validation
 * Functionality: DRM memory mangemnt
 * Test category: GEM_Legacy
 *
 * SUBTEST: drm_mm_test@align32
 * Category: Infrastructure
 * Description: drm_mm range manager SW validation
 * Functionality: DRM memory mangemnt
 * Test category: GEM_Legacy
 *
 * SUBTEST: drm_mm_test@align64
 * Category: Infrastructure
 * Description: drm_mm range manager SW validation
 * Functionality: DRM memory mangemnt
 * Test category: GEM_Legacy
 *
 * SUBTEST: drm_mm_test@bottomup
 * Category: Infrastructure
 * Description: drm_mm range manager SW validation
 * Functionality: DRM memory mangemnt
 * Test category: GEM_Legacy
 *
 * SUBTEST: drm_mm_test@color
 * Category: Infrastructure
 * Description: drm_mm range manager SW validation
 * Functionality: DRM memory mangemnt
 * Test category: GEM_Legacy
 *
 * SUBTEST: drm_mm_test@color_evict
 * Category: Infrastructure
 * Description: drm_mm range manager SW validation
 * Functionality: DRM memory mangemnt
 * Test category: GEM_Legacy
 *
 * SUBTEST: drm_mm_test@color_evict_range
 * Category: Infrastructure
 * Description: drm_mm range manager SW validation
 * Functionality: DRM memory mangemnt
 * Test category: GEM_Legacy
 *
 * SUBTEST: drm_mm_test@debug
 * Category: Infrastructure
 * Description: drm_mm range manager SW validation
 * Functionality: DRM memory mangemnt
 * Test category: GEM_Legacy
 *
 * SUBTEST: drm_mm_test@evict
 * Category: Infrastructure
 * Description: drm_mm range manager SW validation
 * Functionality: DRM memory mangemnt
 * Test category: GEM_Legacy
 *
 * SUBTEST: drm_mm_test@evict_range
 * Category: Infrastructure
 * Description: drm_mm range manager SW validation
 * Functionality: DRM memory mangemnt
 * Test category: GEM_Legacy
 *
 * SUBTEST: drm_mm_test@frag
 * Category: Infrastructure
 * Description: drm_mm range manager SW validation
 * Functionality: DRM memory mangemnt
 * Test category: GEM_Legacy
 *
 * SUBTEST: drm_mm_test@highest
 * Category: Infrastructure
 * Description: drm_mm range manager SW validation
 * Functionality: DRM memory mangemnt
 * Test category: GEM_Legacy
 *
 * SUBTEST: drm_mm_test@init
 * Category: Infrastructure
 * Description: drm_mm range manager SW validation
 * Functionality: DRM memory mangemnt
 * Test category: GEM_Legacy
 *
 * SUBTEST: drm_mm_test@insert
 * Category: Infrastructure
 * Description: drm_mm range manager SW validation
 * Functionality: DRM memory mangemnt
 * Test category: GEM_Legacy
 *
 * SUBTEST: drm_mm_test@insert_range
 * Category: Infrastructure
 * Description: drm_mm range manager SW validation
 * Functionality: DRM memory mangemnt
 * Test category: GEM_Legacy
 *
 * SUBTEST: drm_mm_test@lowest
 * Category: Infrastructure
 * Description: drm_mm range manager SW validation
 * Functionality: DRM memory mangemnt
 * Test category: GEM_Legacy
 *
 * SUBTEST: drm_mm_test@replace
 * Category: Infrastructure
 * Description: drm_mm range manager SW validation
 * Functionality: DRM memory mangemnt
 * Test category: GEM_Legacy
 *
 * SUBTEST: drm_mm_test@reserve
 * Category: Infrastructure
 * Description: drm_mm range manager SW validation
 * Functionality: DRM memory mangemnt
 * Test category: GEM_Legacy
 *
 * SUBTEST: drm_mm_test@sanitycheck
 * Category: Infrastructure
 * Description: drm_mm range manager SW validation
 * Functionality: DRM memory mangemnt
 * Test category: GEM_Legacy
 *
 * SUBTEST: drm_mm_test@topdown
 * Category: Infrastructure
 * Description: drm_mm range manager SW validation
 * Functionality: DRM memory mangemnt
 * Test category: GEM_Legacy
 */

IGT_TEST_DESCRIPTION("Basic sanity check of DRM's range manager (struct drm_mm)");

igt_main
{
	igt_kunit("drm_mm_test", NULL, NULL);
}

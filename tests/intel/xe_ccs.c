// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <errno.h>
#include <glib.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <malloc.h>
#include "drm.h"
#include "igt.h"
#include "igt_syncobj.h"
#include "intel_blt.h"
#include "intel_mocs.h"
#include "intel_pat.h"
#include "xe/xe_ioctl.h"
#include "xe/xe_query.h"
#include "xe/xe_util.h"
/**
 * TEST: xe ccs
 * Category: Hardware building block
 * Sub-category: blitter
 * Functionality: flat_ccs
 * Description: Exercise gen12 blitter with and without flatccs compression on Xe
 * Test category: functionality test
 *
 * SUBTEST: block-copy-compressed
 * Description: Check block-copy flatccs compressed blit
 *
 * SUBTEST: block-copy-uncompressed
 * Description: Check block-copy uncompressed blit
 *
 * SUBTEST: block-multicopy-compressed
 * Description: Check block-multicopy flatccs compressed blit
 *
 * SUBTEST: block-multicopy-inplace
 * Description: Check block-multicopy flatccs inplace decompression blit
 *
 * SUBTEST: ctrl-surf-copy
 * Description: Check flatccs data can be copied from/to surface
 *
 * SUBTEST: ctrl-surf-copy-new-ctx
 * Description: Check flatccs data are physically tagged and visible in vm
 *
 * SUBTEST: suspend-resume
 * Description: Check flatccs data persists after suspend / resume (S0)
 */

IGT_TEST_DESCRIPTION("Exercise gen12 blitter with and without flatccs compression on Xe");

static struct param {
	int compression_format;
	int tiling;
	bool write_png;
	bool print_bb;
	bool print_surface_info;
	int width;
	int height;
} param = {
	.compression_format = 0,
	.tiling = -1,
	.write_png = false,
	.print_bb = false,
	.print_surface_info = false,
	.width = 512,
	.height = 512,
};

struct test_config {
	bool compression;
	bool inplace;
	bool surfcopy;
	bool new_ctx;
	bool suspend_resume;
};

#define PRINT_SURFACE_INFO(name, obj) do { \
	if (param.print_surface_info) \
		blt_surface_info((name), (obj)); } while (0)

#define WRITE_PNG(fd, id, name, obj, w, h) do { \
	if (param.write_png) \
		blt_surface_to_png((fd), (id), (name), (obj), (w), (h)); } while (0)

static void surf_copy(int xe,
		      intel_ctx_t *ctx,
		      uint64_t ahnd,
		      const struct blt_copy_object *src,
		      const struct blt_copy_object *mid,
		      const struct blt_copy_object *dst,
		      int run_id, bool suspend_resume)
{
	struct blt_copy_data blt = {};
	struct blt_block_copy_data_ext ext = {};
	struct blt_ctrl_surf_copy_data surf = {};
	uint32_t bb1, bb2, ccs, ccs2, *ccsmap, *ccsmap2;
	uint64_t bb_size, ccssize = mid->size / CCS_RATIO;
	uint32_t *ccscopy;
	uint8_t uc_mocs = intel_get_uc_mocs_index(xe);
	uint32_t sysmem = system_memory(xe);
	int result;

	igt_assert(mid->compression);
	ccscopy = (uint32_t *) malloc(ccssize);
	ccs = xe_bo_create_flags(xe, 0, ccssize, sysmem);
	ccs2 = xe_bo_create_flags(xe, 0, ccssize, sysmem);

	blt_ctrl_surf_copy_init(xe, &surf);
	surf.print_bb = param.print_bb;
	blt_set_ctrl_surf_object(&surf.src, mid->handle, mid->region, mid->size,
				 uc_mocs, DEFAULT_PAT_INDEX, BLT_INDIRECT_ACCESS);
	blt_set_ctrl_surf_object(&surf.dst, ccs, sysmem, ccssize, uc_mocs,
				 DEFAULT_PAT_INDEX, DIRECT_ACCESS);
	bb_size = xe_get_default_alignment(xe);
	bb1 = xe_bo_create_flags(xe, 0, bb_size, sysmem);
	blt_set_batch(&surf.bb, bb1, bb_size, sysmem);
	blt_ctrl_surf_copy(xe, ctx, NULL, ahnd, &surf);
	intel_ctx_xe_sync(ctx, true);

	ccsmap = xe_bo_map(xe, ccs, surf.dst.size);
	memcpy(ccscopy, ccsmap, ccssize);

	if (suspend_resume) {
		char *orig, *orig2, *newsum, *newsum2;

		orig = g_compute_checksum_for_data(G_CHECKSUM_SHA1,
						   (void *)ccsmap, surf.dst.size);
		orig2 = g_compute_checksum_for_data(G_CHECKSUM_SHA1,
						    (void *)mid->ptr, mid->size);

		igt_system_suspend_autoresume(SUSPEND_STATE_FREEZE, SUSPEND_TEST_NONE);

		blt_set_ctrl_surf_object(&surf.dst, ccs2, system_memory(xe), ccssize,
					 0, DEFAULT_PAT_INDEX, DIRECT_ACCESS);
		blt_ctrl_surf_copy(xe, ctx, NULL, ahnd, &surf);
		intel_ctx_xe_sync(ctx, true);

		ccsmap2 = xe_bo_map(xe, ccs2, surf.dst.size);
		newsum = g_compute_checksum_for_data(G_CHECKSUM_SHA1,
						     (void *)ccsmap2, surf.dst.size);
		newsum2 = g_compute_checksum_for_data(G_CHECKSUM_SHA1,
						      (void *)mid->ptr, mid->size);

		munmap(ccsmap2, ccssize);
		igt_assert(!strcmp(orig, newsum));
		igt_assert(!strcmp(orig2, newsum2));
		g_free(orig);
		g_free(orig2);
		g_free(newsum);
		g_free(newsum2);
	}

	/* corrupt ccs */
	for (int i = 0; i < surf.dst.size / sizeof(uint32_t); i++)
		ccsmap[i] = i;
	blt_set_ctrl_surf_object(&surf.src, ccs, sysmem, ccssize,
				 uc_mocs, DEFAULT_PAT_INDEX, DIRECT_ACCESS);
	blt_set_ctrl_surf_object(&surf.dst, mid->handle, mid->region, mid->size,
				 uc_mocs, DEFAULT_PAT_INDEX, INDIRECT_ACCESS);
	blt_ctrl_surf_copy(xe, ctx, NULL, ahnd, &surf);
	intel_ctx_xe_sync(ctx, true);

	blt_copy_init(xe, &blt);
	blt.color_depth = CD_32bit;
	blt.print_bb = param.print_bb;
	blt_set_copy_object(&blt.src, mid);
	blt_set_copy_object(&blt.dst, dst);
	blt_set_object_ext(&ext.src, mid->compression_type, mid->x2, mid->y2, SURFACE_TYPE_2D);
	blt_set_object_ext(&ext.dst, 0, dst->x2, dst->y2, SURFACE_TYPE_2D);
	bb2 = xe_bo_create_flags(xe, 0, bb_size, sysmem);
	blt_set_batch(&blt.bb, bb2, bb_size, sysmem);
	blt_block_copy(xe, ctx, NULL, ahnd, &blt, &ext);
	intel_ctx_xe_sync(ctx, true);
	WRITE_PNG(xe, run_id, "corrupted", &blt.dst, dst->x2, dst->y2);
	result = memcmp(src->ptr, dst->ptr, src->size);
	igt_assert(result != 0);

	/* retrieve back ccs */
	memcpy(ccsmap, ccscopy, ccssize);
	blt_ctrl_surf_copy(xe, ctx, NULL, ahnd, &surf);

	blt_block_copy(xe, ctx, NULL, ahnd, &blt, &ext);
	intel_ctx_xe_sync(ctx, true);
	WRITE_PNG(xe, run_id, "corrected", &blt.dst, dst->x2, dst->y2);
	result = memcmp(src->ptr, dst->ptr, src->size);
	if (result)
		blt_dump_corruption_info_32b(src, dst);

	munmap(ccsmap, ccssize);
	gem_close(xe, ccs);
	gem_close(xe, ccs2);
	gem_close(xe, bb1);
	gem_close(xe, bb2);

	igt_assert_f(result == 0,
		     "Source and destination surfaces are different after "
		     "restoring source ccs data\n");
}

struct blt_copy3_data {
	int xe;
	struct blt_copy_object src;
	struct blt_copy_object mid;
	struct blt_copy_object dst;
	struct blt_copy_object final;
	struct blt_copy_batch bb;
	enum blt_color_depth color_depth;

	/* debug stuff */
	bool print_bb;
};

struct blt_block_copy3_data_ext {
	struct blt_block_copy_object_ext src;
	struct blt_block_copy_object_ext mid;
	struct blt_block_copy_object_ext dst;
	struct blt_block_copy_object_ext final;
};

static int blt_block_copy3(int xe,
			   const intel_ctx_t *ctx,
			   uint64_t ahnd,
			   const struct blt_copy3_data *blt3,
			   const struct blt_block_copy3_data_ext *ext3)
{
	struct blt_copy_data blt0;
	struct blt_block_copy_data_ext ext0;
	uint64_t bb_offset, alignment;
	uint64_t bb_pos = 0;
	int ret;

	igt_assert_f(ahnd, "block-copy3 supports softpin only\n");
	igt_assert_f(blt3, "block-copy3 requires data to do blit\n");

	alignment = xe_get_default_alignment(xe);
	get_offset(ahnd, blt3->src.handle, blt3->src.size, alignment);
	get_offset(ahnd, blt3->mid.handle, blt3->mid.size, alignment);
	get_offset(ahnd, blt3->dst.handle, blt3->dst.size, alignment);
	get_offset(ahnd, blt3->final.handle, blt3->final.size, alignment);
	bb_offset = get_offset(ahnd, blt3->bb.handle, blt3->bb.size, alignment);

	/* First blit src -> mid */
	blt_copy_init(xe, &blt0);
	blt0.src = blt3->src;
	blt0.dst = blt3->mid;
	blt0.bb = blt3->bb;
	blt0.color_depth = blt3->color_depth;
	blt0.print_bb = blt3->print_bb;
	ext0.src = ext3->src;
	ext0.dst = ext3->mid;
	bb_pos = emit_blt_block_copy(xe, ahnd, &blt0, &ext0, bb_pos, false);

	/* Second blit mid -> dst */
	blt_copy_init(xe, &blt0);
	blt0.src = blt3->mid;
	blt0.dst = blt3->dst;
	blt0.bb = blt3->bb;
	blt0.color_depth = blt3->color_depth;
	blt0.print_bb = blt3->print_bb;
	ext0.src = ext3->mid;
	ext0.dst = ext3->dst;
	bb_pos = emit_blt_block_copy(xe, ahnd, &blt0, &ext0, bb_pos, false);

	/* Third blit dst -> final */
	blt_copy_init(xe, &blt0);
	blt0.src = blt3->dst;
	blt0.dst = blt3->final;
	blt0.bb = blt3->bb;
	blt0.color_depth = blt3->color_depth;
	blt0.print_bb = blt3->print_bb;
	ext0.src = ext3->dst;
	ext0.dst = ext3->final;
	bb_pos = emit_blt_block_copy(xe, ahnd, &blt0, &ext0, bb_pos, true);

	intel_ctx_xe_exec(ctx, ahnd, bb_offset);

	return ret;
}

static void block_copy(int xe,
		       intel_ctx_t *ctx,
		       uint32_t region1, uint32_t region2,
		       enum blt_tiling_type mid_tiling,
		       const struct test_config *config)
{
	struct blt_copy_data blt = {};
	struct blt_block_copy_data_ext ext = {}, *pext = &ext;
	struct blt_copy_object *src, *mid, *dst;
	const uint32_t bpp = 32;
	uint64_t bb_size = xe_get_default_alignment(xe);
	uint64_t ahnd = intel_allocator_open(xe, ctx->vm, INTEL_ALLOCATOR_RELOC);
	uint32_t run_id = mid_tiling;
	uint32_t mid_region = region2, bb;
	uint32_t width = param.width, height = param.height;
	enum blt_compression mid_compression = config->compression;
	int mid_compression_format = param.compression_format;
	enum blt_compression_type comp_type = COMPRESSION_TYPE_3D;
	uint8_t uc_mocs = intel_get_uc_mocs_index(xe);
	int result;

	bb = xe_bo_create_flags(xe, 0, bb_size, region1);

	if (!blt_uses_extended_block_copy(xe))
		pext = NULL;

	blt_copy_init(xe, &blt);

	src = blt_create_object(&blt, region1, width, height, bpp, uc_mocs,
				T_LINEAR, COMPRESSION_DISABLED, comp_type, true);
	mid = blt_create_object(&blt, mid_region, width, height, bpp, uc_mocs,
				mid_tiling, mid_compression, comp_type, true);
	dst = blt_create_object(&blt, region1, width, height, bpp, uc_mocs,
				T_LINEAR, COMPRESSION_DISABLED, comp_type, true);
	igt_assert(src->size == dst->size);
	PRINT_SURFACE_INFO("src", src);
	PRINT_SURFACE_INFO("mid", mid);
	PRINT_SURFACE_INFO("dst", dst);

	blt_surface_fill_rect(xe, src, width, height);
	WRITE_PNG(xe, run_id, "src", src, width, height);

	blt.color_depth = CD_32bit;
	blt.print_bb = param.print_bb;
	blt_set_copy_object(&blt.src, src);
	blt_set_copy_object(&blt.dst, mid);
	blt_set_object_ext(&ext.src, 0, width, height, SURFACE_TYPE_2D);
	blt_set_object_ext(&ext.dst, mid_compression_format, width, height, SURFACE_TYPE_2D);
	blt_set_batch(&blt.bb, bb, bb_size, region1);
	blt_block_copy(xe, ctx, NULL, ahnd, &blt, pext);
	intel_ctx_xe_sync(ctx, true);

	/* We expect mid != src if there's compression */
	if (mid->compression)
		igt_assert(memcmp(src->ptr, mid->ptr, src->size) != 0);

	WRITE_PNG(xe, run_id, "mid", &blt.dst, width, height);

	if (config->surfcopy && pext) {
		struct drm_xe_engine_class_instance inst = {
			.engine_class = DRM_XE_ENGINE_CLASS_COPY,
		};
		intel_ctx_t *surf_ctx = ctx;
		uint64_t surf_ahnd = ahnd;
		uint32_t vm, exec_queue;

		if (config->new_ctx) {
			vm = xe_vm_create(xe, DRM_XE_VM_CREATE_FLAG_ASYNC_DEFAULT, 0);
			exec_queue = xe_exec_queue_create(xe, vm, &inst, 0);
			surf_ctx = intel_ctx_xe(xe, vm, exec_queue, 0, 0, 0);
			surf_ahnd = intel_allocator_open(xe, surf_ctx->vm,
							 INTEL_ALLOCATOR_RELOC);
		}
		surf_copy(xe, surf_ctx, surf_ahnd, src, mid, dst, run_id,
			  config->suspend_resume);

		if (surf_ctx != ctx) {
			xe_exec_queue_destroy(xe, exec_queue);
			xe_vm_destroy(xe, vm);
			free(surf_ctx);
			put_ahnd(surf_ahnd);
		}
	}

	blt_copy_init(xe, &blt);
	blt.color_depth = CD_32bit;
	blt.print_bb = param.print_bb;
	blt_set_copy_object(&blt.src, mid);
	blt_set_copy_object(&blt.dst, dst);
	blt_set_object_ext(&ext.src, mid_compression_format, width, height, SURFACE_TYPE_2D);
	blt_set_object_ext(&ext.dst, 0, width, height, SURFACE_TYPE_2D);
	if (config->inplace) {
		blt_set_object(&blt.dst, mid->handle, dst->size, mid->region, 0,
			       DEFAULT_PAT_INDEX, T_LINEAR, COMPRESSION_DISABLED,
			       comp_type);
		blt.dst.ptr = mid->ptr;
	}

	blt_set_batch(&blt.bb, bb, bb_size, region1);
	blt_block_copy(xe, ctx, NULL, ahnd, &blt, pext);
	intel_ctx_xe_sync(ctx, true);

	WRITE_PNG(xe, run_id, "dst", &blt.dst, width, height);

	result = memcmp(src->ptr, blt.dst.ptr, src->size);

	/* Politely clean vm */
	put_offset(ahnd, src->handle);
	put_offset(ahnd, mid->handle);
	put_offset(ahnd, dst->handle);
	put_offset(ahnd, bb);
	intel_allocator_bind(ahnd, 0, 0);
	blt_destroy_object(xe, src);
	blt_destroy_object(xe, mid);
	blt_destroy_object(xe, dst);
	gem_close(xe, bb);
	put_ahnd(ahnd);

	igt_assert_f(!result, "source and destination surfaces differs!\n");
}

static void block_multicopy(int xe,
			    intel_ctx_t *ctx,
			    uint32_t region1, uint32_t region2,
			    enum blt_tiling_type mid_tiling,
			    const struct test_config *config)
{
	struct blt_copy3_data blt3 = {};
	struct blt_copy_data blt = {};
	struct blt_block_copy3_data_ext ext3 = {}, *pext3 = &ext3;
	struct blt_copy_object *src, *mid, *dst, *final;
	const uint32_t bpp = 32;
	uint64_t bb_size = xe_get_default_alignment(xe);
	uint64_t ahnd = intel_allocator_open(xe, ctx->vm, INTEL_ALLOCATOR_RELOC);
	uint32_t run_id = mid_tiling;
	uint32_t mid_region = region2, bb;
	uint32_t width = param.width, height = param.height;
	enum blt_compression mid_compression = config->compression;
	int mid_compression_format = param.compression_format;
	enum blt_compression_type comp_type = COMPRESSION_TYPE_3D;
	uint8_t uc_mocs = intel_get_uc_mocs_index(xe);
	int result;

	bb = xe_bo_create_flags(xe, 0, bb_size, region1);

	if (!blt_uses_extended_block_copy(xe))
		pext3 = NULL;

	blt_copy_init(xe, &blt);

	src = blt_create_object(&blt, region1, width, height, bpp, uc_mocs,
				T_LINEAR, COMPRESSION_DISABLED, comp_type, true);
	mid = blt_create_object(&blt, mid_region, width, height, bpp, uc_mocs,
				mid_tiling, mid_compression, comp_type, true);
	dst = blt_create_object(&blt, region1, width, height, bpp, uc_mocs,
				mid_tiling, COMPRESSION_DISABLED, comp_type, true);
	final = blt_create_object(&blt, region1, width, height, bpp, uc_mocs,
				  T_LINEAR, COMPRESSION_DISABLED, comp_type, true);
	igt_assert(src->size == dst->size);
	PRINT_SURFACE_INFO("src", src);
	PRINT_SURFACE_INFO("mid", mid);
	PRINT_SURFACE_INFO("dst", dst);
	PRINT_SURFACE_INFO("final", final);

	blt_surface_fill_rect(xe, src, width, height);

	blt3.color_depth = CD_32bit;
	blt3.print_bb = param.print_bb;
	blt_set_copy_object(&blt3.src, src);
	blt_set_copy_object(&blt3.mid, mid);
	blt_set_copy_object(&blt3.dst, dst);
	blt_set_copy_object(&blt3.final, final);

	if (config->inplace) {
		blt_set_object(&blt3.dst, mid->handle, dst->size, mid->region,
			       mid->mocs_index, DEFAULT_PAT_INDEX, mid_tiling,
			       COMPRESSION_DISABLED, comp_type);
		blt3.dst.ptr = mid->ptr;
	}

	blt_set_object_ext(&ext3.src, 0, width, height, SURFACE_TYPE_2D);
	blt_set_object_ext(&ext3.mid, mid_compression_format, width, height, SURFACE_TYPE_2D);
	blt_set_object_ext(&ext3.dst, 0, width, height, SURFACE_TYPE_2D);
	blt_set_object_ext(&ext3.final, 0, width, height, SURFACE_TYPE_2D);
	blt_set_batch(&blt3.bb, bb, bb_size, region1);

	blt_block_copy3(xe, ctx, ahnd, &blt3, pext3);
	intel_ctx_xe_sync(ctx, true);

	WRITE_PNG(xe, run_id, "src", &blt3.src, width, height);
	if (!config->inplace)
		WRITE_PNG(xe, run_id, "mid", &blt3.mid, width, height);
	WRITE_PNG(xe, run_id, "dst", &blt3.dst, width, height);
	WRITE_PNG(xe, run_id, "final", &blt3.final, width, height);

	result = memcmp(src->ptr, blt3.final.ptr, src->size);

	put_offset(ahnd, src->handle);
	put_offset(ahnd, mid->handle);
	put_offset(ahnd, dst->handle);
	put_offset(ahnd, final->handle);
	put_offset(ahnd, bb);
	intel_allocator_bind(ahnd, 0, 0);
	blt_destroy_object(xe, src);
	blt_destroy_object(xe, mid);
	blt_destroy_object(xe, dst);
	blt_destroy_object(xe, final);
	gem_close(xe, bb);
	put_ahnd(ahnd);

	igt_assert_f(!result, "source and destination surfaces differs!\n");
}

enum copy_func {
	BLOCK_COPY,
	BLOCK_MULTICOPY,
};

static const struct {
	const char *suffix;
	void (*copyfn)(int fd,
		       intel_ctx_t *ctx,
		       uint32_t region1, uint32_t region2,
		       enum blt_tiling_type btype,
		       const struct test_config *config);
} copyfns[] = {
	[BLOCK_COPY] = { "", block_copy },
	[BLOCK_MULTICOPY] = { "-multicopy", block_multicopy },
};

static void block_copy_test(int xe,
			    const struct test_config *config,
			    struct igt_collection *set,
			    enum copy_func copy_function)
{
	struct drm_xe_engine_class_instance inst = {
		.engine_class = DRM_XE_ENGINE_CLASS_COPY,
	};
	intel_ctx_t *ctx;
	struct igt_collection *regions;
	uint32_t vm, exec_queue;
	int tiling;

	if (config->compression && !blt_block_copy_supports_compression(xe))
		return;

	if (config->inplace && !config->compression)
		return;

	for_each_tiling(tiling) {
		if (!blt_block_copy_supports_tiling(xe, tiling) ||
		    (param.tiling >= 0 && param.tiling != tiling))
			continue;

		for_each_variation_r(regions, 2, set) {
			uint32_t region1, region2;
			char *regtxt;

			region1 = igt_collection_get_value(regions, 0);
			region2 = igt_collection_get_value(regions, 1);

			/* Compressed surface must be in device memory */
			if (config->compression && !XE_IS_VRAM_MEMORY_REGION(xe, region2))
				continue;

			regtxt = xe_memregion_dynamic_subtest_name(xe, regions);

			igt_dynamic_f("%s-%s-compfmt%d-%s%s",
				      blt_tiling_name(tiling),
				      config->compression ?
					      "compressed" : "uncompressed",
				      param.compression_format, regtxt,
				      copyfns[copy_function].suffix) {
				uint32_t sync_bind, sync_out;

				vm = xe_vm_create(xe, DRM_XE_VM_CREATE_FLAG_ASYNC_DEFAULT, 0);
				exec_queue = xe_exec_queue_create(xe, vm, &inst, 0);
				sync_bind = syncobj_create(xe, 0);
				sync_out = syncobj_create(xe, 0);
				ctx = intel_ctx_xe(xe, vm, exec_queue,
						   0, sync_bind, sync_out);

				copyfns[copy_function].copyfn(xe, ctx,
							      region1, region2,
							      tiling, config);

				xe_exec_queue_destroy(xe, exec_queue);
				xe_vm_destroy(xe, vm);
				syncobj_destroy(xe, sync_bind);
				syncobj_destroy(xe, sync_out);
				free(ctx);
			}

			free(regtxt);
		}
	}
}

static int opt_handler(int opt, int opt_index, void *data)
{
	switch (opt) {
	case 'b':
		param.print_bb = true;
		igt_debug("Print bb: %d\n", param.print_bb);
		break;
	case 'f':
		param.compression_format = atoi(optarg);
		igt_debug("Compression format: %d\n", param.compression_format);
		igt_assert((param.compression_format & ~0x1f) == 0);
		break;
	case 'p':
		param.write_png = true;
		igt_debug("Write png: %d\n", param.write_png);
		break;
	case 's':
		param.print_surface_info = true;
		igt_debug("Print surface info: %d\n", param.print_surface_info);
		break;
	case 't':
		param.tiling = atoi(optarg);
		igt_debug("Tiling: %d\n", param.tiling);
		break;
	case 'W':
		param.width = atoi(optarg);
		igt_debug("Width: %d\n", param.width);
		break;
	case 'H':
		param.height = atoi(optarg);
		igt_debug("Height: %d\n", param.height);
		break;
	default:
		return IGT_OPT_HANDLER_ERROR;
	}

	return IGT_OPT_HANDLER_SUCCESS;
}

const char *help_str =
	"  -b\tPrint bb\n"
	"  -f\tCompression format (0-31)\n"
	"  -p\tWrite PNG\n"
	"  -s\tPrint surface info\n"
	"  -t\tTiling format (0 - linear, 1 - XMAJOR, 2 - YMAJOR, 3 - TILE4, 4 - TILE64)\n"
	"  -W\tWidth (default 512)\n"
	"  -H\tHeight (default 512)"
	;

igt_main_args("bf:pst:W:H:", NULL, help_str, opt_handler, NULL)
{
	struct igt_collection *set;
	int xe;

	igt_fixture {
		xe = drm_open_driver(DRIVER_XE);
		igt_require(blt_has_block_copy(xe));

		xe_device_get(xe);

		set = xe_get_memory_region_set(xe,
					       DRM_XE_MEM_REGION_CLASS_SYSMEM,
					       DRM_XE_MEM_REGION_CLASS_VRAM);
	}

	igt_describe("Check block-copy uncompressed blit");
	igt_subtest_with_dynamic("block-copy-uncompressed") {
		struct test_config config = {};

		block_copy_test(xe, &config, set, BLOCK_COPY);
	}

	igt_describe("Check block-copy flatccs compressed blit");
	igt_subtest_with_dynamic("block-copy-compressed") {
		struct test_config config = { .compression = true };

		block_copy_test(xe, &config, set, BLOCK_COPY);
	}

	igt_describe("Check block-multicopy flatccs compressed blit");
	igt_subtest_with_dynamic("block-multicopy-compressed") {
		struct test_config config = { .compression = true };

		block_copy_test(xe, &config, set, BLOCK_MULTICOPY);
	}

	igt_describe("Check block-multicopy flatccs inplace decompression blit");
	igt_subtest_with_dynamic("block-multicopy-inplace") {
		struct test_config config = { .compression = true,
					      .inplace = true };

		block_copy_test(xe, &config, set, BLOCK_MULTICOPY);
	}

	igt_describe("Check flatccs data can be copied from/to surface");
	igt_subtest_with_dynamic("ctrl-surf-copy") {
		struct test_config config = { .compression = true,
					      .surfcopy = true };

		block_copy_test(xe, &config, set, BLOCK_COPY);
	}

	igt_describe("Check flatccs data are physically tagged and visible"
		     " in different contexts");
	igt_subtest_with_dynamic("ctrl-surf-copy-new-ctx") {
		struct test_config config = { .compression = true,
					      .surfcopy = true,
					      .new_ctx = true };

		block_copy_test(xe, &config, set, BLOCK_COPY);
	}

	igt_describe("Check flatccs data persists after suspend / resume (S0)");
	igt_subtest_with_dynamic("suspend-resume") {
		struct test_config config = { .compression = true,
					      .surfcopy = true,
					      .suspend_resume = true };

		block_copy_test(xe, &config, set, BLOCK_COPY);
	}

	igt_fixture {
		xe_device_put(xe);
		close(xe);
	}
}

// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include <errno.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <malloc.h>
#include <cairo.h>
#include "drm.h"
#include "i915/gem_create.h"
#include "igt.h"
#include "igt_syncobj.h"
#include "intel_blt.h"
#include "xe/xe_ioctl.h"
#include "xe/xe_query.h"
#include "xe/xe_util.h"

#define BITRANGE(start, end) (end - start + 1)
#define GET_CMDS_INFO(__fd) intel_get_cmds_info(intel_get_drm_devid(__fd))

/* Blitter tiling definitions sanity checks */
static_assert(T_LINEAR == I915_TILING_NONE, "Linear definitions have to match");
static_assert(T_XMAJOR == I915_TILING_X, "TileX definitions have to match");
static_assert(T_YMAJOR == I915_TILING_Y, "TileY definitions have to match");
static_assert(T_YFMAJOR == I915_TILING_Yf, "TileYf definitions have to match");

enum blt_special_mode {
	SM_NONE,
	SM_FULL_RESOLVE,
	SM_PARTIAL_RESOLVE,
	SM_RESERVED,
};

enum blt_aux_mode {
	AM_AUX_NONE,
	AM_AUX_CCS_E = 5,
};

enum blt_target_mem {
	TM_LOCAL_MEM,
	TM_SYSTEM_MEM,
};

struct gen12_block_copy_data {
	struct {
		uint32_t length:			BITRANGE(0, 7);
		uint32_t rsvd1:				BITRANGE(8, 8);
		uint32_t multisamples:			BITRANGE(9, 11);
		uint32_t special_mode:			BITRANGE(12, 13);
		uint32_t rsvd0:				BITRANGE(14, 18);
		uint32_t color_depth:			BITRANGE(19, 21);
		uint32_t opcode:			BITRANGE(22, 28);
		uint32_t client:			BITRANGE(29, 31);
	} dw00;

	union {
		struct {
			uint32_t dst_pitch:			BITRANGE(0, 17);
			uint32_t dst_aux_mode:			BITRANGE(18, 20);
			uint32_t pxp:				BITRANGE(21, 21);
			uint32_t dst_mocs_index:		BITRANGE(22, 27);
			uint32_t dst_ctrl_surface_type:		BITRANGE(28, 28);
			uint32_t dst_compression:		BITRANGE(29, 29);
			uint32_t dst_tiling:			BITRANGE(30, 31);
		} dw01;
		struct {
			uint32_t dst_pitch:			BITRANGE(0, 17);
			uint32_t dst_aux_mode:			BITRANGE(18, 20);
			uint32_t pxp:				BITRANGE(21, 21);
			uint32_t dst_mocs_index:		BITRANGE(22, 27);
			uint32_t dst_ctrl_surface_type:		BITRANGE(28, 28);
			uint32_t dst_compression:		BITRANGE(29, 29);
			uint32_t dst_tiling:			BITRANGE(30, 31);
		} dw01_xe2;
	};

	struct {
		int32_t dst_x1:				BITRANGE(0, 15);
		int32_t dst_y1:				BITRANGE(16, 31);
	} dw02;

	struct {
		int32_t dst_x2:				BITRANGE(0, 15);
		int32_t dst_y2:				BITRANGE(16, 31);
	} dw03;

	struct {
		uint32_t dst_address_lo;
	} dw04;

	struct {
		uint32_t dst_address_hi;
	} dw05;

	struct {
		uint32_t dst_x_offset:			BITRANGE(0, 13);
		uint32_t rsvd1:				BITRANGE(14, 15);
		uint32_t dst_y_offset:			BITRANGE(16, 29);
		uint32_t rsvd0:				BITRANGE(30, 30);
		uint32_t dst_target_memory:		BITRANGE(31, 31);
	} dw06;

	struct {
		int32_t src_x1:				BITRANGE(0, 15);
		int32_t src_y1:				BITRANGE(16, 31);
	} dw07;

	union {
		struct {
			uint32_t src_pitch:			BITRANGE(0, 17);
			uint32_t src_aux_mode:			BITRANGE(18, 20);
			uint32_t pxp:				BITRANGE(21, 21);
			uint32_t src_mocs_index:		BITRANGE(22, 27);
			uint32_t src_ctrl_surface_type:		BITRANGE(28, 28);
			uint32_t src_compression:		BITRANGE(29, 29);
			uint32_t src_tiling:			BITRANGE(30, 31);
		} dw08;
		struct {
			uint32_t src_pitch:			BITRANGE(0, 17);
			uint32_t pad0:				BITRANGE(18, 20);
			uint32_t pxp:				BITRANGE(21, 21);
			uint32_t pad1:				BITRANGE(22, 23);
			uint32_t src_mocs_index:		BITRANGE(24, 27);
			uint32_t pad2:				BITRANGE(28, 29);
			uint32_t src_tiling:			BITRANGE(30, 31);
		} dw08_xe2;
	};

	struct {
		uint32_t src_address_lo;
	} dw09;

	struct {
		uint32_t src_address_hi;
	} dw10;

	struct {
		uint32_t src_x_offset:			BITRANGE(0, 13);
		uint32_t rsvd1:				BITRANGE(14, 15);
		uint32_t src_y_offset:			BITRANGE(16, 29);
		uint32_t rsvd0:				BITRANGE(30, 30);
		uint32_t src_target_memory:		BITRANGE(31, 31);
	} dw11;
};

struct gen12_block_copy_data_ext {
	struct {
		uint32_t src_compression_format:	BITRANGE(0, 4);
		uint32_t src_clear_value_enable:	BITRANGE(5, 5);
		uint32_t src_clear_address_low:		BITRANGE(6, 31);
	} dw12;

	union {
		/* DG2, XEHP */
		uint32_t src_clear_address_hi0;
		/* Others */
		uint32_t src_clear_address_hi1;
	} dw13;

	struct {
		uint32_t dst_compression_format:	BITRANGE(0, 4);
		uint32_t dst_clear_value_enable:	BITRANGE(5, 5);
		uint32_t dst_clear_address_low:		BITRANGE(6, 31);
	} dw14;

	union {
		/* DG2, XEHP */
		uint32_t dst_clear_address_hi0;
		/* Others */
		uint32_t dst_clear_address_hi1;
	} dw15;

	struct {
		uint32_t dst_surface_height:		BITRANGE(0, 13);
		uint32_t dst_surface_width:		BITRANGE(14, 27);
		uint32_t rsvd0:				BITRANGE(28, 28);
		uint32_t dst_surface_type:		BITRANGE(29, 31);
	} dw16;

	struct {
		uint32_t dst_lod:			BITRANGE(0, 3);
		uint32_t dst_surface_qpitch:		BITRANGE(4, 18);
		uint32_t rsvd0:				BITRANGE(19, 20);
		uint32_t dst_surface_depth:		BITRANGE(21, 31);
	} dw17;

	struct {
		uint32_t dst_horizontal_align:		BITRANGE(0, 1);
		uint32_t rsvd0:				BITRANGE(2, 2);
		uint32_t dst_vertical_align:		BITRANGE(3, 4);
		uint32_t rsvd1:				BITRANGE(5, 7);
		uint32_t dst_mip_tail_start_lod:	BITRANGE(8, 11);
		uint32_t rsvd2:				BITRANGE(12, 17);
		uint32_t dst_depth_stencil_resource:	BITRANGE(18, 18);
		uint32_t rsvd3:				BITRANGE(19, 20);
		uint32_t dst_array_index:		BITRANGE(21, 31);
	} dw18;

	struct {
		uint32_t src_surface_height:		BITRANGE(0, 13);
		uint32_t src_surface_width:		BITRANGE(14, 27);
		uint32_t rsvd0:				BITRANGE(28, 28);
		uint32_t src_surface_type:		BITRANGE(29, 31);
	} dw19;

	struct {
		uint32_t src_lod:			BITRANGE(0, 3);
		uint32_t src_surface_qpitch:		BITRANGE(4, 18);
		uint32_t rsvd0:				BITRANGE(19, 20);
		uint32_t src_surface_depth:		BITRANGE(21, 31);
	} dw20;

	struct {
		uint32_t src_horizontal_align:		BITRANGE(0, 1);
		uint32_t rsvd0:				BITRANGE(2, 2);
		uint32_t src_vertical_align:		BITRANGE(3, 4);
		uint32_t rsvd1:				BITRANGE(5, 7);
		uint32_t src_mip_tail_start_lod:	BITRANGE(8, 11);
		uint32_t rsvd2:				BITRANGE(12, 17);
		uint32_t src_depth_stencil_resource:	BITRANGE(18, 18);
		uint32_t rsvd3:				BITRANGE(19, 20);
		uint32_t src_array_index:		BITRANGE(21, 31);
	} dw21;
};

/**
 * blt_supports_command:
 * @cmds_info: Copy commands description struct
 * @cmd: Blitter command enum
 *
 * Checks if @cmds_info has an entry of supported tiling formats for @cmd command.
 *
 * Returns: true if it does, false otherwise
 */
bool blt_supports_command(const struct intel_cmds_info *cmds_info,
			  enum blt_cmd_type cmd)
{
	igt_require_f(cmds_info, "No config found for the platform\n");

	return blt_get_cmd_info(cmds_info, cmd);
}

/**
 * blt_cmd_supports_tiling:
 * @cmds_info: Copy commands description struct
 * @cmd: Blitter command enum
 * @tiling: tiling format enum
 *
 * Checks if a @cmd entry of @cmds_info lists @tiling. It also returns false if
 * no information about the command is stored.
 *
 * Returns: true if it does, false otherwise
 */
bool blt_cmd_supports_tiling(const struct intel_cmds_info *cmds_info,
			     enum blt_cmd_type cmd,
			     enum blt_tiling_type tiling)
{
	struct blt_cmd_info const *cmd_info;

	if (!cmds_info)
		return false;

	cmd_info = blt_get_cmd_info(cmds_info, cmd);

	/* no config means no support for that tiling */
	if (!cmd_info)
		return false;

	return cmd_info->supported_tiling & BIT(tiling);
}

/**
 * blt_cmd_has_property:
 * @cmds_info: Copy commands description struct
 * @cmd: Blitter command enum
 * @prop: property flag
 *
 * Checks if a @cmd entry of @cmds_info has @prop property. The properties can
 * be freely combined, but the function will return true for platforms for
 * which all properties defined in the bit flag are present. The function
 * returns false if no information about the command is stored.
 *
 * Returns: true if it does, false otherwise
 */
bool blt_cmd_has_property(const struct intel_cmds_info *cmds_info,
			  enum blt_cmd_type cmd, uint32_t prop)
{
	struct blt_cmd_info const *cmd_info;

	cmd_info = blt_get_cmd_info(cmds_info, cmd);

	if (!cmd_info)
		return false;

	return cmd_info->flags & prop;
}

/**
 * blt_has_block_copy
 * @fd: drm fd
 *
 * Check if block copy is supported by @fd device
 *
 * Returns:
 * true if it does, false otherwise.
 */
bool blt_has_block_copy(int fd)
{
	const struct intel_cmds_info *cmds_info = GET_CMDS_INFO(fd);

	return blt_supports_command(cmds_info, XY_BLOCK_COPY);
}

/**
 * blt_has_mem_copy
 * @fd: drm fd
 *
 * Check if mem copy is supported by @fd device
 *
 * Returns:
 * true if it does, false otherwise.
 */
bool blt_has_mem_copy(int fd)
{
	const struct intel_cmds_info *cmds_info = GET_CMDS_INFO(fd);

	return blt_supports_command(cmds_info, MEM_COPY);
}

/**
 * blt_has_mem_set
 * @fd: drm fd
 *
 * Check if mem set is supported by @fd device
 *
 * Returns:
 * true if it does, false otherwise.
 */
bool blt_has_mem_set(int fd)
{
	const struct intel_cmds_info *cmds_info = GET_CMDS_INFO(fd);

	return blt_supports_command(cmds_info, MEM_SET);
}

/**
 * blt_has_fast_copy
 * @fd: drm fd
 *
 * Check if fast copy is supported by @fd device
 *
 * Returns:
 * true if it does, false otherwise.
 */
bool blt_has_fast_copy(int fd)
{
	const struct intel_cmds_info *cmds_info = GET_CMDS_INFO(fd);

	return blt_supports_command(cmds_info, XY_FAST_COPY);
}

/**
 * blt_has_xy_src_copy
 * @fd: drm fd
 *
 * Check if XY src copy is supported by @fd device
 *
 * Returns:
 * true if it does, false otherwise.
 */
bool blt_has_xy_src_copy(int fd)
{
	const struct intel_cmds_info *cmds_info = GET_CMDS_INFO(fd);

	return blt_supports_command(cmds_info, XY_SRC_COPY);
}

/**
 * blt_has_xy_color
 * @fd: drm fd
 *
 * Check if XY_COLOR_BLT is supported by @fd device
 *
 * Returns:
 * true if it does, false otherwise.
 */
bool blt_has_xy_color(int fd)
{
	const struct intel_cmds_info *cmds_info = GET_CMDS_INFO(fd);

	return blt_supports_command(cmds_info, XY_COLOR_BLT);
}

/**
 * blt_fast_copy_supports_tiling
 * @fd: drm fd
 * @tiling: tiling format
 *
 * Check if fast copy provided by @fd device supports @tiling format
 *
 * Returns:
 * true if it does, false otherwise.
 */
bool blt_fast_copy_supports_tiling(int fd, enum blt_tiling_type tiling)
{
	const struct intel_cmds_info *cmds_info = GET_CMDS_INFO(fd);

	return blt_cmd_supports_tiling(cmds_info, XY_FAST_COPY, tiling);
}

/**
 * blt_block_copy_supports_tiling
 * @fd: drm fd
 * @tiling: tiling format
 *
 * Check if block copy provided by @fd device supports @tiling format
 *
 * Returns:
 * true if it does, false otherwise.
 */
bool blt_block_copy_supports_tiling(int fd, enum blt_tiling_type tiling)
{
	const struct intel_cmds_info *cmds_info = GET_CMDS_INFO(fd);

	return blt_cmd_supports_tiling(cmds_info, XY_BLOCK_COPY, tiling);
}

/**
 * blt_xy_src_copy_supports_tiling
 * @fd: drm fd
 * @tiling: tiling format
 *
 * Check if XY src copy provided by @fd device supports @tiling format
 *
 * Returns:
 * true if it does, false otherwise.
 */
bool blt_xy_src_copy_supports_tiling(int fd, enum blt_tiling_type tiling)
{
	const struct intel_cmds_info *cmds_info = GET_CMDS_INFO(fd);

	return blt_cmd_supports_tiling(cmds_info, XY_SRC_COPY, tiling);
}

/**
 * blt_block_copy_supports_compression
 * @fd: drm fd
 *
 * Check if block copy provided by @fd device supports compression.
 *
 * Returns:
 * true if it does, false otherwise.
 */
bool blt_block_copy_supports_compression(int fd)
{
	const struct intel_cmds_info *cmds_info = GET_CMDS_INFO(fd);

	return blt_cmd_has_property(cmds_info, XY_BLOCK_COPY,
				    BLT_CMD_SUPPORTS_COMPRESSION);
}

/**
 * blt_uses_extended_block_copy
 * @fd: drm fd
 *
 * Check if block copy provided by @fd device uses an extended version
 * of the command.
 *
 * Returns:
 * true if it does, false otherwise.
 */
bool blt_uses_extended_block_copy(int fd)
{
	const struct intel_cmds_info *cmds_info = GET_CMDS_INFO(fd);

	return blt_cmd_has_property(cmds_info, XY_BLOCK_COPY, BLT_CMD_EXTENDED);
}

/**
 * blt_tiling_name:
 * @tiling: tiling id
 *
 * Returns:
 * name of @tiling passed. Useful to build test names.
 */
const char *blt_tiling_name(enum blt_tiling_type tiling)
{
	switch (tiling) {
	case T_LINEAR: return "linear";
	case T_XMAJOR: return "xmajor";
	case T_YMAJOR: return "ymajor";
	case T_TILE4:  return "tile4";
	case T_TILE64: return "tile64";
	case T_YFMAJOR: return "yfmajor";
	default:
		break;
	}

	igt_warn("invalid tiling passed: %d\n", tiling);
	return NULL;
}

static int __block_tiling(enum blt_tiling_type tiling)
{
	switch (tiling) {
	case T_LINEAR: return 0;
	case T_XMAJOR: return 1;
	case T_YMAJOR: return 1;
	case T_TILE4:  return 2;
	case T_TILE64: return 3;
	default:
		break;
	}

	igt_warn("invalid tiling passed: %d\n", tiling);
	return 0;
}

static int __special_mode(const struct blt_copy_data *blt)
{
	if (blt->src.handle == blt->dst.handle &&
	    blt->src.compression && !blt->dst.compression)
		return SM_FULL_RESOLVE;


	return SM_NONE;
}

static int __memory_type(int fd, enum intel_driver driver, uint32_t region)
{
	if (driver == INTEL_DRIVER_I915) {
		igt_assert_f(IS_DEVICE_MEMORY_REGION(region) ||
			     IS_SYSTEM_MEMORY_REGION(region),
			     "Invalid region: %x\n", region);
	} else {
		igt_assert_f(XE_IS_VRAM_MEMORY_REGION(fd, region) ||
			     XE_IS_SYSMEM_MEMORY_REGION(fd, region),
			     "Invalid region: %x\n", region);
	}

	if (driver == INTEL_DRIVER_I915 && IS_DEVICE_MEMORY_REGION(region))
		return TM_LOCAL_MEM;
	else if (driver == INTEL_DRIVER_XE && XE_IS_VRAM_MEMORY_REGION(fd, region))
		return TM_LOCAL_MEM;

	return TM_SYSTEM_MEM;
}

static enum blt_aux_mode __aux_mode(int fd,
				    enum intel_driver driver,
				    const struct blt_copy_object *obj)
{
	if (driver == INTEL_DRIVER_I915 && obj->compression == COMPRESSION_ENABLED) {
		igt_assert_f(IS_DEVICE_MEMORY_REGION(obj->region),
			     "XY_BLOCK_COPY_BLT supports compression "
			     "on device memory only\n");
		return AM_AUX_CCS_E;
	} else if (driver == INTEL_DRIVER_XE && obj->compression == COMPRESSION_ENABLED) {
		igt_assert_f(XE_IS_VRAM_MEMORY_REGION(fd, obj->region),
			     "XY_BLOCK_COPY_BLT supports compression "
			     "on device memory only\n");
		return AM_AUX_CCS_E;
	}

	return AM_AUX_NONE;
}

static bool __new_tile_y_type(enum blt_tiling_type tiling)
{
	return tiling == T_TILE4 || tiling == T_YFMAJOR;
}

static void fill_data(struct gen12_block_copy_data *data,
		      const struct blt_copy_data *blt,
		      uint64_t src_offset, uint64_t dst_offset,
		      bool extended_command, unsigned int ip_ver)
{
	data->dw00.client = 0x2;
	data->dw00.opcode = 0x41;
	data->dw00.color_depth = blt->color_depth;
	data->dw00.special_mode = __special_mode(blt);
	data->dw00.length = extended_command ? 20 : 10;

	if (ip_ver >= IP_VER(20, 0)) {
		data->dw01_xe2.dst_pitch = blt->dst.pitch - 1;
		data->dw01_xe2.dst_mocs_index = blt->dst.mocs_index;
		data->dw01_xe2.dst_tiling = __block_tiling(blt->dst.tiling);
	} else {
		if (__special_mode(blt) == SM_FULL_RESOLVE)
			data->dw01.dst_aux_mode = __aux_mode(blt->fd, blt->driver, &blt->src);
		else
			data->dw01.dst_aux_mode = __aux_mode(blt->fd, blt->driver, &blt->dst);
		data->dw01.dst_pitch = blt->dst.pitch - 1;

		data->dw01.dst_mocs_index = blt->dst.mocs_index;
		data->dw01.dst_compression = blt->dst.compression;
		data->dw01.dst_tiling = __block_tiling(blt->dst.tiling);

		if (blt->dst.compression)
			data->dw01.dst_ctrl_surface_type = blt->dst.compression_type;
	}

	data->dw02.dst_x1 = blt->dst.x1;
	data->dw02.dst_y1 = blt->dst.y1;

	data->dw03.dst_x2 = blt->dst.x2;
	data->dw03.dst_y2 = blt->dst.y2;

	data->dw04.dst_address_lo = dst_offset;
	data->dw05.dst_address_hi = dst_offset >> 32;

	data->dw06.dst_x_offset = blt->dst.x_offset;
	data->dw06.dst_y_offset = blt->dst.y_offset;
	data->dw06.dst_target_memory = __memory_type(blt->fd, blt->driver, blt->dst.region);

	data->dw07.src_x1 = blt->src.x1;
	data->dw07.src_y1 = blt->src.y1;

	if (ip_ver >= IP_VER(20, 0)) {
		data->dw08_xe2.src_pitch = blt->src.pitch - 1;
		data->dw08_xe2.src_mocs_index = blt->src.mocs_index;
		data->dw08_xe2.src_tiling = __block_tiling(blt->src.tiling);
	} else {
		data->dw08.src_pitch = blt->src.pitch - 1;
		data->dw08.src_aux_mode = __aux_mode(blt->fd, blt->driver, &blt->src);
		data->dw08.src_mocs_index = blt->src.mocs_index;
		data->dw08.src_compression = blt->src.compression;
		data->dw08.src_tiling = __block_tiling(blt->src.tiling);

		if (blt->src.compression)
			data->dw08.src_ctrl_surface_type = blt->src.compression_type;
	}

	data->dw09.src_address_lo = src_offset;
	data->dw10.src_address_hi = src_offset >> 32;

	data->dw11.src_x_offset = blt->src.x_offset;
	data->dw11.src_y_offset = blt->src.y_offset;
	data->dw11.src_target_memory = __memory_type(blt->fd, blt->driver, blt->src.region);
}

static void fill_data_ext(struct gen12_block_copy_data_ext *dext,
			  const struct blt_block_copy_data_ext *ext)
{
	dext->dw12.src_compression_format = ext->src.compression_format;
	dext->dw12.src_clear_value_enable = ext->src.clear_value_enable;
	dext->dw12.src_clear_address_low = ext->src.clear_address;

	dext->dw13.src_clear_address_hi0 = ext->src.clear_address >> 32;

	dext->dw14.dst_compression_format = ext->dst.compression_format;
	dext->dw14.dst_clear_value_enable = ext->dst.clear_value_enable;
	dext->dw14.dst_clear_address_low = ext->dst.clear_address;

	dext->dw15.dst_clear_address_hi0 = ext->dst.clear_address >> 32;

	dext->dw16.dst_surface_width = ext->dst.surface_width - 1;
	dext->dw16.dst_surface_height = ext->dst.surface_height - 1;
	dext->dw16.dst_surface_type = ext->dst.surface_type;

	dext->dw17.dst_lod = ext->dst.lod;
	dext->dw17.dst_surface_depth = ext->dst.surface_depth;
	dext->dw17.dst_surface_qpitch = ext->dst.surface_qpitch;

	dext->dw18.dst_horizontal_align = ext->dst.horizontal_align;
	dext->dw18.dst_vertical_align = ext->dst.vertical_align;
	dext->dw18.dst_mip_tail_start_lod = ext->dst.mip_tail_start_lod;
	dext->dw18.dst_depth_stencil_resource = ext->dst.depth_stencil_resource;
	dext->dw18.dst_array_index = ext->dst.array_index;

	dext->dw19.src_surface_width = ext->src.surface_width - 1;
	dext->dw19.src_surface_height = ext->src.surface_height - 1;

	dext->dw19.src_surface_type = ext->src.surface_type;

	dext->dw20.src_lod = ext->src.lod;
	dext->dw20.src_surface_depth = ext->src.surface_depth;
	dext->dw20.src_surface_qpitch = ext->src.surface_qpitch;

	dext->dw21.src_horizontal_align = ext->src.horizontal_align;
	dext->dw21.src_vertical_align = ext->src.vertical_align;
	dext->dw21.src_mip_tail_start_lod = ext->src.mip_tail_start_lod;
	dext->dw21.src_depth_stencil_resource = ext->src.depth_stencil_resource;
	dext->dw21.src_array_index = ext->src.array_index;
}

static void dump_bb_cmd(struct gen12_block_copy_data *data)
{
	uint32_t *cmd = (uint32_t *) data;

	igt_info("details:\n");
	igt_info(" dw00: [%08x] <client: 0x%x, opcode: 0x%x, color depth: %d, "
		 "special mode: %d, length: %d>\n",
		 cmd[0],
		 data->dw00.client, data->dw00.opcode, data->dw00.color_depth,
		 data->dw00.special_mode, data->dw00.length);
	igt_info(" dw01: [%08x] dst <pitch: %d, aux: %d, mocs_idx: %d, compr: %d, "
		 "tiling: %d, ctrl surf type: %d>\n",
		 cmd[1], data->dw01.dst_pitch, data->dw01.dst_aux_mode,
		 data->dw01.dst_mocs_index, data->dw01.dst_compression,
		 data->dw01.dst_tiling, data->dw01.dst_ctrl_surface_type);
	igt_info(" dw02: [%08x] dst geom <x1: %d, y1: %d>\n",
		 cmd[2], data->dw02.dst_x1, data->dw02.dst_y1);
	igt_info(" dw03: [%08x]          <x2: %d, y2: %d>\n",
		 cmd[3], data->dw03.dst_x2, data->dw03.dst_y2);
	igt_info(" dw04: [%08x] dst offset lo (0x%x)\n",
		 cmd[4], data->dw04.dst_address_lo);
	igt_info(" dw05: [%08x] dst offset hi (0x%x)\n",
		 cmd[5], data->dw05.dst_address_hi);
	igt_info(" dw06: [%08x] dst <x offset: 0x%x, y offset: 0x%0x, target mem: %d>\n",
		 cmd[6], data->dw06.dst_x_offset, data->dw06.dst_y_offset,
		 data->dw06.dst_target_memory);
	igt_info(" dw07: [%08x] src geom <x1: %d, y1: %d>\n",
		 cmd[7], data->dw07.src_x1, data->dw07.src_y1);
	igt_info(" dw08: [%08x] src <pitch: %d, aux: %d, mocs_idx: %d, compr: %d, "
		 "tiling: %d, ctrl surf type: %d>\n",
		 cmd[8], data->dw08.src_pitch, data->dw08.src_aux_mode,
		 data->dw08.src_mocs_index, data->dw08.src_compression,
		 data->dw08.src_tiling, data->dw08.src_ctrl_surface_type);
	igt_info(" dw09: [%08x] src offset lo (0x%x)\n",
		 cmd[9], data->dw09.src_address_lo);
	igt_info(" dw10: [%08x] src offset hi (0x%x)\n",
		 cmd[10], data->dw10.src_address_hi);
	igt_info(" dw11: [%08x] src <x offset: 0x%x, y offset: 0x%0x, target mem: %d>\n",
		 cmd[11], data->dw11.src_x_offset, data->dw11.src_y_offset,
		 data->dw11.src_target_memory);
}

static void dump_bb_ext(struct gen12_block_copy_data_ext *data)
{
	uint32_t *cmd = (uint32_t *) data;

	igt_info("ext details:\n");
	igt_info(" dw12: [%08x] src <compression fmt: %d, clear value enable: %d, "
		 "clear address low: 0x%x>\n",
		 cmd[0],
		 data->dw12.src_compression_format,
		 data->dw12.src_clear_value_enable,
		 data->dw12.src_clear_address_low);
	igt_info(" dw13: [%08x] src clear address hi: 0x%x\n",
		 cmd[1], data->dw13.src_clear_address_hi0);
	igt_info(" dw14: [%08x] dst <compression fmt: %d, clear value enable: %d, "
		 "clear address low: 0x%x>\n",
		 cmd[2],
		 data->dw14.dst_compression_format,
		 data->dw14.dst_clear_value_enable,
		 data->dw14.dst_clear_address_low);
	igt_info(" dw15: [%08x] dst clear address hi: 0x%x\n",
		 cmd[3], data->dw15.dst_clear_address_hi0);
	igt_info(" dw16: [%08x] dst surface <width: %d, height: %d, type: %d>\n",
		 cmd[4], data->dw16.dst_surface_width,
		 data->dw16.dst_surface_height, data->dw16.dst_surface_type);
	igt_info(" dw17: [%08x] dst surface <lod: %d, depth: %d, qpitch: %d>\n",
		 cmd[5], data->dw17.dst_lod,
		 data->dw17.dst_surface_depth, data->dw17.dst_surface_qpitch);
	igt_info(" dw18: [%08x] dst <halign: %d, valign: %d, mip tail: %d, "
		 "depth stencil: %d, array index: %d>\n",
		 cmd[6],
		 data->dw18.dst_horizontal_align,
		 data->dw18.dst_vertical_align,
		 data->dw18.dst_mip_tail_start_lod,
		 data->dw18.dst_depth_stencil_resource,
		 data->dw18.dst_array_index);

	igt_info(" dw19: [%08x] src surface <width: %d, height: %d, type: %d>\n",
		 cmd[7], data->dw19.src_surface_width,
		 data->dw19.src_surface_height, data->dw19.src_surface_type);
	igt_info(" dw20: [%08x] src surface <lod: %d, depth: %d, qpitch: %d>\n",
		 cmd[8], data->dw20.src_lod,
		 data->dw20.src_surface_depth, data->dw20.src_surface_qpitch);
	igt_info(" dw21: [%08x] src <halign: %d, valign: %d, mip tail: %d, "
		 "depth stencil: %d, array index: %d>\n",
		 cmd[9],
		 data->dw21.src_horizontal_align,
		 data->dw21.src_vertical_align,
		 data->dw21.src_mip_tail_start_lod,
		 data->dw21.src_depth_stencil_resource,
		 data->dw21.src_array_index);
}

static uint64_t get_default_alignment(int fd, enum intel_driver driver)
{
	if (driver == INTEL_DRIVER_XE)
		return xe_get_default_alignment(fd);

	return gem_detect_safe_alignment(fd);
}

static void *bo_map(int fd, uint32_t handle, uint64_t size,
		    enum intel_driver driver)
{
	if (driver == INTEL_DRIVER_XE)
		return xe_bo_map(fd, handle, size);

	return gem_mmap__device_coherent(fd, handle, 0, size,
					 PROT_READ | PROT_WRITE);
}

/**
 * blt_copy_init:
 * @fd: drm fd
 * @blt: structure for initialization
 *
 * Function is zeroing @blt and sets fd and driver fields (INTEL_DRIVER_I915 or
 * INTEL_DRIVER_XE).
 */
void blt_copy_init(int fd, struct blt_copy_data *blt)
{
	memset(blt, 0, sizeof(*blt));

	blt->fd = fd;
	blt->driver = get_intel_driver(fd);
}

/**
 * emit_blt_block_copy:
 * @fd: drm fd
 * @ahnd: allocator handle
 * @blt: basic blitter data (for TGL/DG1 which doesn't support ext version)
 * @ext: extended blitter data (for DG2+, supports flatccs compression)
 * @bb_pos: position at which insert block copy commands
 * @emit_bbe: emit MI_BATCH_BUFFER_END after block-copy or not
 *
 * Function inserts block-copy blit into batch at @bb_pos. Allows concatenating
 * with other commands to achieve pipelining.
 *
 * Returns:
 * Next write position in batch.
 */
uint64_t emit_blt_block_copy(int fd,
			     uint64_t ahnd,
			     const struct blt_copy_data *blt,
			     const struct blt_block_copy_data_ext *ext,
			     uint64_t bb_pos,
			     bool emit_bbe)
{
	unsigned int ip_ver = intel_graphics_ver(intel_get_drm_devid(fd));
	struct gen12_block_copy_data data = {};
	struct gen12_block_copy_data_ext dext = {};
	uint64_t dst_offset, src_offset, bb_offset, alignment;
	uint32_t bbe = MI_BATCH_BUFFER_END;
	uint8_t *bb;

	igt_assert_f(ahnd, "block-copy supports softpin only\n");
	igt_assert_f(blt, "block-copy requires data to do blit\n");

	alignment = get_default_alignment(fd, blt->driver);
	src_offset = get_offset(ahnd, blt->src.handle, blt->src.size, alignment)
		     + blt->src.plane_offset;
	dst_offset = get_offset(ahnd, blt->dst.handle, blt->dst.size, alignment)
		     + blt->dst.plane_offset;
	bb_offset = get_offset(ahnd, blt->bb.handle, blt->bb.size, alignment);

	fill_data(&data, blt, src_offset, dst_offset, ext, ip_ver);

	bb = bo_map(fd, blt->bb.handle, blt->bb.size, blt->driver);

	igt_assert(bb_pos + sizeof(data) < blt->bb.size);
	memcpy(bb + bb_pos, &data, sizeof(data));
	bb_pos += sizeof(data);

	if (ext) {
		fill_data_ext(&dext, ext);
		igt_assert(bb_pos + sizeof(dext) < blt->bb.size);
		memcpy(bb + bb_pos, &dext, sizeof(dext));
		bb_pos += sizeof(dext);
	}

	if (emit_bbe) {
		igt_assert(bb_pos + sizeof(uint32_t) < blt->bb.size);
		memcpy(bb + bb_pos, &bbe, sizeof(bbe));
		bb_pos += sizeof(uint32_t);
	}

	if (blt->print_bb) {
		igt_info("[BLOCK COPY]\n");
		igt_info("src offset: %" PRIx64 ", dst offset: %" PRIx64
			 ", bb offset: %" PRIx64 "\n",
			 src_offset, dst_offset, bb_offset);

		dump_bb_cmd(&data);
		if (ext)
			dump_bb_ext(&dext);
	}

	munmap(bb, blt->bb.size);

	return bb_pos;
}

/**
 * blt_block_copy:
 * @fd: drm fd
 * @ctx: intel_ctx_t context
 * @e: blitter engine for @ctx
 * @ahnd: allocator handle
 * @blt: basic blitter data (for TGL/DG1 which doesn't support ext version)
 * @ext: extended blitter data (for DG2+, supports flatccs compression)
 *
 * Function does blit between @src and @dst described in @blt object.
 *
 * Returns:
 * execbuffer status.
 */
int blt_block_copy(int fd,
		   const intel_ctx_t *ctx,
		   const struct intel_execution_engine2 *e,
		   uint64_t ahnd,
		   const struct blt_copy_data *blt,
		   const struct blt_block_copy_data_ext *ext)
{
	struct drm_i915_gem_execbuffer2 execbuf = {};
	struct drm_i915_gem_exec_object2 obj[3] = {};
	uint64_t dst_offset, src_offset, bb_offset, alignment;
	int ret;

	igt_assert_f(ahnd, "block-copy supports softpin only\n");
	igt_assert_f(blt, "block-copy requires data to do blit\n");
	igt_assert_neq(blt->driver, 0);

	alignment = get_default_alignment(fd, blt->driver);
	src_offset = get_offset(ahnd, blt->src.handle, blt->src.size, alignment);
	dst_offset = get_offset(ahnd, blt->dst.handle, blt->dst.size, alignment);
	bb_offset = get_offset(ahnd, blt->bb.handle, blt->bb.size, alignment);

	emit_blt_block_copy(fd, ahnd, blt, ext, 0, true);

	if (blt->driver == INTEL_DRIVER_XE) {
		intel_ctx_xe_exec(ctx, ahnd, CANONICAL(bb_offset));
	} else {
		obj[0].offset = CANONICAL(dst_offset);
		obj[1].offset = CANONICAL(src_offset);
		obj[2].offset = CANONICAL(bb_offset);
		obj[0].handle = blt->dst.handle;
		obj[1].handle = blt->src.handle;
		obj[2].handle = blt->bb.handle;
		obj[0].flags = EXEC_OBJECT_PINNED | EXEC_OBJECT_WRITE |
				EXEC_OBJECT_SUPPORTS_48B_ADDRESS;
		obj[1].flags = EXEC_OBJECT_PINNED | EXEC_OBJECT_SUPPORTS_48B_ADDRESS;
		obj[2].flags = EXEC_OBJECT_PINNED | EXEC_OBJECT_SUPPORTS_48B_ADDRESS;
		execbuf.buffer_count = 3;
		execbuf.buffers_ptr = to_user_pointer(obj);
		execbuf.rsvd1 = ctx ? ctx->id : 0;
		execbuf.flags = e ? e->flags : I915_EXEC_BLT;

		ret = __gem_execbuf(fd, &execbuf);
	}

	return ret;
}

static uint16_t __ccs_size(const struct blt_ctrl_surf_copy_data *surf)
{
	uint32_t src_size, dst_size;

	src_size = surf->src.access_type == DIRECT_ACCESS ?
				surf->src.size : surf->src.size / CCS_RATIO;

	dst_size = surf->dst.access_type == DIRECT_ACCESS ?
				surf->dst.size : surf->dst.size / CCS_RATIO;

	igt_assert_f(src_size <= dst_size, "dst size must be >= src size for CCS copy\n");

	return src_size;
}

struct gen12_ctrl_surf_copy_data {
	struct {
		uint32_t length:			BITRANGE(0, 7);
		uint32_t size_of_ctrl_copy:		BITRANGE(8, 17);
		uint32_t rsvd0:				BITRANGE(18, 19);
		uint32_t dst_access_type:		BITRANGE(20, 20);
		uint32_t src_access_type:		BITRANGE(21, 21);
		uint32_t opcode:			BITRANGE(22, 28);
		uint32_t client:			BITRANGE(29, 31);
	} dw00;

	struct {
		uint32_t src_address_lo;
	} dw01;

	struct {
		uint32_t src_address_hi:		BITRANGE(0, 24);
		uint32_t pxp:				BITRANGE(25, 25);
		uint32_t src_mocs_index:		BITRANGE(26, 31);
	} dw02;

	struct {
		uint32_t dst_address_lo;
	} dw03;

	struct {
		uint32_t dst_address_hi:		BITRANGE(0, 24);
		uint32_t pxp:				BITRANGE(25, 25);
		uint32_t dst_mocs_index:		BITRANGE(26, 31);
	} dw04;
};

static void dump_bb_surf_ctrl_cmd(const struct gen12_ctrl_surf_copy_data *data)
{
	uint32_t *cmd = (uint32_t *) data;

	igt_info("details:\n");
	igt_info(" dw00: [%08x] <client: 0x%x, opcode: 0x%x, "
		 "src/dst access type: <%d, %d>, size of ctrl copy: %u, length: %d>\n",
		 cmd[0],
		 data->dw00.client, data->dw00.opcode,
		 data->dw00.src_access_type, data->dw00.dst_access_type,
		 data->dw00.size_of_ctrl_copy, data->dw00.length);
	igt_info(" dw01: [%08x] src offset lo (0x%x)\n",
		 cmd[1], data->dw01.src_address_lo);
	igt_info(" dw02: [%08x] src offset hi (0x%x), src mocs idx: %u\n",
		 cmd[2], data->dw02.src_address_hi, data->dw02.src_mocs_index);
	igt_info(" dw03: [%08x] dst offset lo (0x%x)\n",
		 cmd[3], data->dw03.dst_address_lo);
	igt_info(" dw04: [%08x] dst offset hi (0x%x), dst mocs idx: %u\n",
		 cmd[4], data->dw04.dst_address_hi, data->dw04.dst_mocs_index);
}

/**
 * blt_ctrl_surf_copy_init:
 * @fd: drm fd
 * @surf: structure for initialization
 *
 * Function is zeroing @surf and sets fd and driver fields (INTEL_DRIVER_I915 or
 * INTEL_DRIVER_XE).
 */
void blt_ctrl_surf_copy_init(int fd, struct blt_ctrl_surf_copy_data *surf)
{
	memset(surf, 0, sizeof(*surf));

	surf->fd = fd;
	surf->driver = get_intel_driver(fd);
}

/**
 * emit_blt_ctrl_surf_copy:
 * @fd: drm fd
 * @ahnd: allocator handle
 * @surf: blitter data for ctrl-surf-copy
 * @bb_pos: position at which insert block copy commands
 * @emit_bbe: emit MI_BATCH_BUFFER_END after ctrl-surf-copy or not
 *
 * Function emits ctrl-surf-copy blit between @src and @dst described in
 * @blt object at @bb_pos. Allows concatenating with other commands to
 * achieve pipelining.
 *
 * Returns:
 * Next write position in batch.
 */
uint64_t emit_blt_ctrl_surf_copy(int fd,
				 uint64_t ahnd,
				 const struct blt_ctrl_surf_copy_data *surf,
				 uint64_t bb_pos,
				 bool emit_bbe)
{
	struct gen12_ctrl_surf_copy_data data = {};
	uint64_t dst_offset, src_offset, bb_offset, alignment;
	uint32_t bbe = MI_BATCH_BUFFER_END;
	uint32_t *bb;

	igt_assert_f(ahnd, "ctrl-surf-copy supports softpin only\n");
	igt_assert_f(surf, "ctrl-surf-copy requires data to do ctrl-surf-copy blit\n");

	alignment = max_t(uint64_t, get_default_alignment(fd, surf->driver), 1ull << 16);

	data.dw00.client = 0x2;
	data.dw00.opcode = 0x48;
	data.dw00.src_access_type = surf->src.access_type;
	data.dw00.dst_access_type = surf->dst.access_type;

	/* Ensure dst has size capable to keep src ccs aux */
	data.dw00.size_of_ctrl_copy = __ccs_size(surf) / CCS_RATIO - 1;
	data.dw00.length = 0x3;

	src_offset = get_offset(ahnd, surf->src.handle, surf->src.size, alignment);
	dst_offset = get_offset(ahnd, surf->dst.handle, surf->dst.size, alignment);
	bb_offset = get_offset(ahnd, surf->bb.handle, surf->bb.size, alignment);

	data.dw01.src_address_lo = src_offset;
	data.dw02.src_address_hi = src_offset >> 32;
	data.dw02.src_mocs_index = surf->src.mocs_index;

	data.dw03.dst_address_lo = dst_offset;
	data.dw04.dst_address_hi = dst_offset >> 32;
	data.dw04.dst_mocs_index = surf->dst.mocs_index;

	bb = bo_map(fd, surf->bb.handle, surf->bb.size, surf->driver);

	igt_assert(bb_pos + sizeof(data) < surf->bb.size);
	memcpy(bb + bb_pos, &data, sizeof(data));
	bb_pos += sizeof(data);

	if (emit_bbe) {
		igt_assert(bb_pos + sizeof(uint32_t) < surf->bb.size);
		memcpy(bb + bb_pos, &bbe, sizeof(bbe));
		bb_pos += sizeof(uint32_t);
	}

	if (surf->print_bb) {
		igt_info("[CTRL SURF]:\n");
		igt_info("src offset: %" PRIx64 ", dst offset: %" PRIx64
			 ", bb offset: %" PRIx64 "\n",
			 src_offset, dst_offset, bb_offset);

		dump_bb_surf_ctrl_cmd(&data);
	}

	munmap(bb, surf->bb.size);

	return bb_pos;
}

/**
 * blt_ctrl_surf_copy:
 * @fd: drm fd
 * @ctx: intel_ctx_t context
 * @e: blitter engine for @ctx
 * @ahnd: allocator handle
 * @surf: blitter data for ctrl-surf-copy
 *
 * Function does ctrl-surf-copy blit between @src and @dst described in
 * @blt object.
 *
 * Returns:
 * execbuffer status.
 */
int blt_ctrl_surf_copy(int fd,
		       const intel_ctx_t *ctx,
		       const struct intel_execution_engine2 *e,
		       uint64_t ahnd,
		       const struct blt_ctrl_surf_copy_data *surf)
{
	struct drm_i915_gem_execbuffer2 execbuf = {};
	struct drm_i915_gem_exec_object2 obj[3] = {};
	uint64_t dst_offset, src_offset, bb_offset, alignment;

	igt_assert_f(ahnd, "ctrl-surf-copy supports softpin only\n");
	igt_assert_f(surf, "ctrl-surf-copy requires data to do ctrl-surf-copy blit\n");
	igt_assert_neq(surf->driver, 0);

	alignment = max_t(uint64_t, get_default_alignment(fd, surf->driver), 1ull << 16);
	src_offset = get_offset(ahnd, surf->src.handle, surf->src.size, alignment);
	dst_offset = get_offset(ahnd, surf->dst.handle, surf->dst.size, alignment);
	bb_offset = get_offset(ahnd, surf->bb.handle, surf->bb.size, alignment);

	emit_blt_ctrl_surf_copy(fd, ahnd, surf, 0, true);

	if (surf->driver == INTEL_DRIVER_XE) {
		intel_ctx_xe_exec(ctx, ahnd, CANONICAL(bb_offset));
	} else {
		obj[0].offset = CANONICAL(dst_offset);
		obj[1].offset = CANONICAL(src_offset);
		obj[2].offset = CANONICAL(bb_offset);
		obj[0].handle = surf->dst.handle;
		obj[1].handle = surf->src.handle;
		obj[2].handle = surf->bb.handle;
		obj[0].flags = EXEC_OBJECT_PINNED | EXEC_OBJECT_WRITE |
				EXEC_OBJECT_SUPPORTS_48B_ADDRESS;
		obj[1].flags = EXEC_OBJECT_PINNED | EXEC_OBJECT_SUPPORTS_48B_ADDRESS;
		obj[2].flags = EXEC_OBJECT_PINNED | EXEC_OBJECT_SUPPORTS_48B_ADDRESS;
		execbuf.buffer_count = 3;
		execbuf.buffers_ptr = to_user_pointer(obj);
		execbuf.flags = e ? e->flags : I915_EXEC_BLT;
		execbuf.rsvd1 = ctx ? ctx->id : 0;
		gem_execbuf(fd, &execbuf);
		put_offset(ahnd, surf->dst.handle);
		put_offset(ahnd, surf->src.handle);
		put_offset(ahnd, surf->bb.handle);
	}

	return 0;
}

struct gen12_fast_copy_data {
	struct {
		uint32_t length:			BITRANGE(0, 7);
		uint32_t rsvd1:				BITRANGE(8, 12);
		uint32_t dst_tiling:			BITRANGE(13, 14);
		uint32_t rsvd0:				BITRANGE(15, 19);
		uint32_t src_tiling:			BITRANGE(20, 21);
		uint32_t opcode:			BITRANGE(22, 28);
		uint32_t client:			BITRANGE(29, 31);
	} dw00;

	struct {
		uint32_t dst_pitch:			BITRANGE(0, 15);
		uint32_t rsvd1:				BITRANGE(16, 23);
		uint32_t color_depth:			BITRANGE(24, 26);
		uint32_t rsvd0:				BITRANGE(27, 27);
		uint32_t dst_memory:			BITRANGE(28, 28);
		uint32_t src_memory:			BITRANGE(29, 29);
		uint32_t dst_type_y:			BITRANGE(30, 30);
		uint32_t src_type_y:			BITRANGE(31, 31);
	} dw01;

	struct {
		int32_t dst_x1:				BITRANGE(0, 15);
		int32_t dst_y1:				BITRANGE(16, 31);
	} dw02;

	struct {
		int32_t dst_x2:				BITRANGE(0, 15);
		int32_t dst_y2:				BITRANGE(16, 31);
	} dw03;

	struct {
		uint32_t dst_address_lo;
	} dw04;

	struct {
		uint32_t dst_address_hi;
	} dw05;

	struct {
		int32_t src_x1:				BITRANGE(0, 15);
		int32_t src_y1:				BITRANGE(16, 31);
	} dw06;

	struct {
		uint32_t src_pitch:			BITRANGE(0, 15);
		uint32_t rsvd0:				BITRANGE(16, 31);
	} dw07;

	struct {
		uint32_t src_address_lo;
	} dw08;

	struct {
		uint32_t src_address_hi;
	} dw09;
};

static int __fast_tiling(enum blt_tiling_type tiling)
{
	switch (tiling) {
	case T_LINEAR: return 0;
	case T_XMAJOR: return 1;
	case T_YMAJOR: return 2;
	case T_TILE4:  return 2;
	case T_YFMAJOR: return 2;
	case T_TILE64: return 3;
	default:
		break;
	}

	igt_warn("invalid tiling passed: %d\n", tiling);
	return 0;
}

static int __fast_color_depth(enum blt_color_depth depth)
{
	switch (depth) {
	case CD_8bit:   return 0;
	case CD_16bit:  return 1;
	case CD_32bit:  return 3;
	case CD_64bit:  return 4;
	case CD_96bit:
		igt_assert_f(0, "Unsupported depth\n");
		break;
	case CD_128bit: return 5;
	};
	return 0;
}

static void dump_bb_fast_cmd(struct gen12_fast_copy_data *data)
{
	uint32_t *cmd = (uint32_t *) data;

	igt_info("BB details:\n");
	igt_info(" dw00: [%08x] <client: 0x%x, opcode: 0x%x, src tiling: %d, "
		 "dst tiling: %d, length: %d>\n",
		 cmd[0], data->dw00.client, data->dw00.opcode,
		 data->dw00.src_tiling, data->dw00.dst_tiling, data->dw00.length);
	igt_info(" dw01: [%08x] dst <pitch: %d, color depth: %d, dst memory: %d, "
		 "src memory: %d,\n"
		 "\t\t\tdst type tile: %d (0-legacy, 1-tile4),\n"
		 "\t\t\tsrc type tile: %d (0-legacy, 1-tile4)>\n",
		 cmd[1], data->dw01.dst_pitch, data->dw01.color_depth,
		 data->dw01.dst_memory, data->dw01.src_memory,
		 data->dw01.dst_type_y, data->dw01.src_type_y);
	igt_info(" dw02: [%08x] dst geom <x1: %d, y1: %d>\n",
		 cmd[2], data->dw02.dst_x1, data->dw02.dst_y1);
	igt_info(" dw03: [%08x]          <x2: %d, y2: %d>\n",
		 cmd[3], data->dw03.dst_x2, data->dw03.dst_y2);
	igt_info(" dw04: [%08x] dst offset lo (0x%x)\n",
		 cmd[4], data->dw04.dst_address_lo);
	igt_info(" dw05: [%08x] dst offset hi (0x%x)\n",
		 cmd[5], data->dw05.dst_address_hi);
	igt_info(" dw06: [%08x] src geom <x1: %d, y1: %d>\n",
		 cmd[6], data->dw06.src_x1, data->dw06.src_y1);
	igt_info(" dw07: [%08x] src <pitch: %d>\n",
		 cmd[7], data->dw07.src_pitch);
	igt_info(" dw08: [%08x] src offset lo (0x%x)\n",
		 cmd[8], data->dw08.src_address_lo);
	igt_info(" dw09: [%08x] src offset hi (0x%x)\n",
		 cmd[9], data->dw09.src_address_hi);
}

/**
 * emit_blt_fast_copy:
 * @fd: drm fd
 * @ahnd: allocator handle
 * @blt: blitter data for fast-copy (same as for block-copy but doesn't use
 * compression fields).
 * @bb_pos: position at which insert block copy commands
 * @emit_bbe: emit MI_BATCH_BUFFER_END after fast-copy or not
 *
 * Function emits fast-copy blit between @src and @dst described in @blt object
 * at @bb_pos. Allows concatenating with other commands to
 * achieve pipelining.
 *
 * Returns:
 * Next write position in batch.
 */
uint64_t emit_blt_fast_copy(int fd,
			    uint64_t ahnd,
			    const struct blt_copy_data *blt,
			    uint64_t bb_pos,
			    bool emit_bbe)
{
	struct gen12_fast_copy_data data = {};
	uint64_t dst_offset, src_offset, bb_offset, alignment;
	uint32_t bbe = MI_BATCH_BUFFER_END;
	uint32_t *bb;

	alignment = get_default_alignment(fd, blt->driver);

	data.dw00.client = 0x2;
	data.dw00.opcode = 0x42;
	data.dw00.dst_tiling = __fast_tiling(blt->dst.tiling);
	data.dw00.src_tiling = __fast_tiling(blt->src.tiling);
	data.dw00.length = 8;

	data.dw01.dst_pitch = blt->dst.pitch;
	data.dw01.color_depth = __fast_color_depth(blt->color_depth);
	data.dw01.dst_memory = __memory_type(blt->fd, blt->driver, blt->dst.region);
	data.dw01.src_memory = __memory_type(blt->fd, blt->driver, blt->src.region);
	data.dw01.dst_type_y = __new_tile_y_type(blt->dst.tiling) ? 1 : 0;
	data.dw01.src_type_y = __new_tile_y_type(blt->src.tiling) ? 1 : 0;

	data.dw02.dst_x1 = blt->dst.x1;
	data.dw02.dst_y1 = blt->dst.y1;

	data.dw03.dst_x2 = blt->dst.x2;
	data.dw03.dst_y2 = blt->dst.y2;

	src_offset = get_offset(ahnd, blt->src.handle, blt->src.size, alignment)
		     + blt->src.plane_offset;
	dst_offset = get_offset(ahnd, blt->dst.handle, blt->dst.size, alignment)
		     + blt->dst.plane_offset;
	bb_offset = get_offset(ahnd, blt->bb.handle, blt->bb.size, alignment);

	data.dw04.dst_address_lo = dst_offset;
	data.dw05.dst_address_hi = dst_offset >> 32;

	data.dw06.src_x1 = blt->src.x1;
	data.dw06.src_y1 = blt->src.y1;

	data.dw07.src_pitch = blt->src.pitch;

	data.dw08.src_address_lo = src_offset;
	data.dw09.src_address_hi = src_offset >> 32;

	bb = bo_map(fd, blt->bb.handle, blt->bb.size, blt->driver);

	igt_assert(bb_pos + sizeof(data) < blt->bb.size);
	memcpy(bb + bb_pos, &data, sizeof(data));
	bb_pos += sizeof(data);

	if (emit_bbe) {
		igt_assert(bb_pos + sizeof(uint32_t) < blt->bb.size);
		memcpy(bb + bb_pos, &bbe, sizeof(bbe));
		bb_pos += sizeof(uint32_t);
	}

	if (blt->print_bb) {
		igt_info("[FAST COPY]\n");
		igt_info("src offset: %" PRIx64 ", dst offset: %" PRIx64
			 ", bb offset: %" PRIx64 "\n",
			 src_offset, dst_offset, bb_offset);
		dump_bb_fast_cmd(&data);
	}

	munmap(bb, blt->bb.size);

	return bb_pos;
}

/**
 * blt_fast_copy:
 * @fd: drm fd
 * @ctx: intel_ctx_t context
 * @e: blitter engine for @ctx
 * @ahnd: allocator handle
 * @blt: blitter data for fast-copy (same as for block-copy but doesn't use
 * compression fields).
 *
 * Function does fast blit between @src and @dst described in @blt object.
 *
 * Returns:
 * execbuffer status.
 */
int blt_fast_copy(int fd,
		  const intel_ctx_t *ctx,
		  const struct intel_execution_engine2 *e,
		  uint64_t ahnd,
		  const struct blt_copy_data *blt)
{
	struct drm_i915_gem_execbuffer2 execbuf = {};
	struct drm_i915_gem_exec_object2 obj[3] = {};
	uint64_t dst_offset, src_offset, bb_offset, alignment;
	int ret;

	igt_assert_f(ahnd, "fast-copy supports softpin only\n");
	igt_assert_f(blt, "fast-copy requires data to do fast-copy blit\n");
	igt_assert_neq(blt->driver, 0);

	alignment = get_default_alignment(fd, blt->driver);
	src_offset = get_offset(ahnd, blt->src.handle, blt->src.size, alignment);
	dst_offset = get_offset(ahnd, blt->dst.handle, blt->dst.size, alignment);
	bb_offset = get_offset(ahnd, blt->bb.handle, blt->bb.size, alignment);

	emit_blt_fast_copy(fd, ahnd, blt, 0, true);

	if (blt->driver == INTEL_DRIVER_XE) {
		intel_ctx_xe_exec(ctx, ahnd, CANONICAL(bb_offset));
	} else {
		obj[0].offset = CANONICAL(dst_offset);
		obj[1].offset = CANONICAL(src_offset);
		obj[2].offset = CANONICAL(bb_offset);
		obj[0].handle = blt->dst.handle;
		obj[1].handle = blt->src.handle;
		obj[2].handle = blt->bb.handle;
		obj[0].flags = EXEC_OBJECT_PINNED | EXEC_OBJECT_WRITE |
				EXEC_OBJECT_SUPPORTS_48B_ADDRESS;
		obj[1].flags = EXEC_OBJECT_PINNED | EXEC_OBJECT_SUPPORTS_48B_ADDRESS;
		obj[2].flags = EXEC_OBJECT_PINNED | EXEC_OBJECT_SUPPORTS_48B_ADDRESS;
		execbuf.buffer_count = 3;
		execbuf.buffers_ptr = to_user_pointer(obj);
		execbuf.rsvd1 = ctx ? ctx->id : 0;
		execbuf.flags = e ? e->flags : I915_EXEC_BLT;
		ret = __gem_execbuf(fd, &execbuf);
		put_offset(ahnd, blt->dst.handle);
		put_offset(ahnd, blt->src.handle);
		put_offset(ahnd, blt->bb.handle);
	}

	return ret;
}

void blt_set_geom(struct blt_copy_object *obj, uint32_t pitch,
		  int16_t x1, int16_t y1, int16_t x2, int16_t y2,
		  uint16_t x_offset, uint16_t y_offset)
{
	obj->pitch = pitch;
	obj->x1 = x1;
	obj->y1 = y1;
	obj->x2 = x2;
	obj->y2 = y2;
	obj->x_offset = x_offset;
	obj->y_offset = y_offset;
}

void blt_set_batch(struct blt_copy_batch *batch,
		   uint32_t handle, uint64_t size, uint32_t region)
{
	batch->handle = handle;
	batch->size = size;
	batch->region = region;
}

struct blt_copy_object *
blt_create_object(const struct blt_copy_data *blt, uint32_t region,
		  uint32_t width, uint32_t height, uint32_t bpp, uint8_t mocs_index,
		  enum blt_tiling_type tiling,
		  enum blt_compression compression,
		  enum blt_compression_type compression_type,
		  bool create_mapping)
{
	struct blt_copy_object *obj;
	uint64_t size = width * height * bpp / 8;
	uint32_t stride = tiling == T_LINEAR ? width * 4 : width;
	uint32_t handle;

	igt_assert_f(blt->driver, "Driver isn't set, have you called blt_copy_init()?\n");

	obj = calloc(1, sizeof(*obj));

	obj->size = size;

	if (blt->driver == INTEL_DRIVER_XE) {
		size = ALIGN(size, xe_get_default_alignment(blt->fd));
		handle = xe_bo_create_flags(blt->fd, 0, size, region);
	} else {
		igt_assert(__gem_create_in_memory_regions(blt->fd, &handle,
							  &size, region) == 0);
	}

	blt_set_object(obj, handle, size, region, mocs_index, tiling,
		       compression, compression_type);
	blt_set_geom(obj, stride, 0, 0, width, height, 0, 0);

	if (create_mapping)
		obj->ptr = bo_map(blt->fd, handle, size, blt->driver);

	return obj;
}

void blt_destroy_object(int fd, struct blt_copy_object *obj)
{
	if (obj->ptr)
		munmap(obj->ptr, obj->size);

	gem_close(fd, obj->handle);
	free(obj);
}

void blt_set_object(struct blt_copy_object *obj,
		    uint32_t handle, uint64_t size, uint32_t region,
		    uint8_t mocs_index, enum blt_tiling_type tiling,
		    enum blt_compression compression,
		    enum blt_compression_type compression_type)
{
	obj->handle = handle;
	obj->size = size;
	obj->region = region;
	obj->mocs_index = mocs_index;
	obj->tiling = tiling;
	obj->compression = compression;
	obj->compression_type = compression_type;
}

void blt_set_object_ext(struct blt_block_copy_object_ext *obj,
			uint8_t compression_format,
			uint16_t surface_width, uint16_t surface_height,
			enum blt_surface_type surface_type)
{
	obj->compression_format = compression_format;
	obj->surface_width = surface_width;
	obj->surface_height = surface_height;
	obj->surface_type = surface_type;

	/* Ensure mip tail won't overlap lod */
	obj->mip_tail_start_lod = 0xf;
}

void blt_set_copy_object(struct blt_copy_object *obj,
			 const struct blt_copy_object *orig)
{
	memcpy(obj, orig, sizeof(*obj));
}

void blt_set_ctrl_surf_object(struct blt_ctrl_surf_copy_object *obj,
			      uint32_t handle, uint32_t region, uint64_t size,
			      uint8_t mocs_index, enum blt_access_type access_type)
{
	obj->handle = handle;
	obj->region = region;
	obj->size = size;
	obj->mocs_index = mocs_index;
	obj->access_type = access_type;
}

/**
 * blt_surface_fill_rect:
 * @fd: drm fd
 * @obj: blitter copy object (@blt_copy_object) to fill with gradient pattern
 * @width: width
 * @height: height
 *
 * Function fills surface @width x @height * 24bpp with color gradient
 * (internally uses ARGB where A == 0xff, see Cairo docs).
 */
void blt_surface_fill_rect(int fd, const struct blt_copy_object *obj,
			   uint32_t width, uint32_t height)
{
	cairo_surface_t *surface;
	cairo_pattern_t *pat;
	cairo_t *cr;
	void *map = obj->ptr;

	if (!map)
		map = gem_mmap__device_coherent(fd, obj->handle, 0,
						obj->size, PROT_READ | PROT_WRITE);

	surface = cairo_image_surface_create_for_data(map,
						      CAIRO_FORMAT_RGB24,
						      width, height,
						      obj->pitch);

	cr = cairo_create(surface);

	cairo_rectangle(cr, 0, 0, width, height);
	cairo_clip(cr);

	pat = cairo_pattern_create_mesh();
	cairo_mesh_pattern_begin_patch(pat);
	cairo_mesh_pattern_move_to(pat, 0, 0);
	cairo_mesh_pattern_line_to(pat, width, 0);
	cairo_mesh_pattern_line_to(pat, width, height);
	cairo_mesh_pattern_line_to(pat, 0, height);
	cairo_mesh_pattern_set_corner_color_rgb(pat, 0, 1.0, 0.0, 0.0);
	cairo_mesh_pattern_set_corner_color_rgb(pat, 1, 0.0, 1.0, 0.0);
	cairo_mesh_pattern_set_corner_color_rgb(pat, 2, 0.0, 0.0, 1.0);
	cairo_mesh_pattern_set_corner_color_rgb(pat, 3, 1.0, 1.0, 1.0);
	cairo_mesh_pattern_end_patch(pat);

	cairo_rectangle(cr, 0, 0, width, height);
	cairo_set_source(cr, pat);
	cairo_fill(cr);
	cairo_pattern_destroy(pat);

	cairo_destroy(cr);

	cairo_surface_destroy(surface);
	if (!obj->ptr)
		munmap(map, obj->size);
}

/**
 * blt_surface_info:
 * @info: information header
 * @obj: blitter copy object (@blt_copy_object) to print surface info
 */
void blt_surface_info(const char *info, const struct blt_copy_object *obj)
{
	igt_info("[%s]\n", info);
	igt_info("surface <handle: %u, size: %llx, region: %x, mocs_idx: %x>\n",
		 obj->handle, (long long) obj->size, obj->region, obj->mocs_index);
	igt_info("        <tiling: %s, compression: %u, compression type: %d>\n",
		 blt_tiling_name(obj->tiling), obj->compression, obj->compression_type);
	igt_info("        <pitch: %u, offset [x: %u, y: %u] geom [<%d,%d> <%d,%d>]>\n",
		 obj->pitch, obj->x_offset, obj->y_offset,
		 obj->x1, obj->y1, obj->x2, obj->y2);
}

/**
 * blt_surface_to_png:
 * @fd: drm fd
 * @run_id: prefix id to allow grouping files stored from single run
 * @fileid: file identifier
 * @obj: blitter copy object (@blt_copy_object) to save to png
 * @width: width
 * @height: height
 *
 * Function save surface to png file. Assumes ARGB format where A == 0xff.
 */
void blt_surface_to_png(int fd, uint32_t run_id, const char *fileid,
			const struct blt_copy_object *obj,
			uint32_t width, uint32_t height)
{
	cairo_surface_t *surface;
	cairo_status_t ret;
	uint8_t *map = (uint8_t *) obj->ptr;
	int format;
	int stride = obj->tiling ? obj->pitch * 4 : obj->pitch;
	char filename[FILENAME_MAX];
	bool is_xe = is_xe_device(fd);

	snprintf(filename, FILENAME_MAX-1, "%d-%s-%s-%ux%u-%s.png",
		 run_id, fileid, blt_tiling_name(obj->tiling), width, height,
		 obj->compression ? "compressed" : "uncompressed");

	if (!map) {
		if (is_xe)
			map = xe_bo_map(fd, obj->handle, obj->size);
		else
			map = gem_mmap__device_coherent(fd, obj->handle, 0,
							obj->size, PROT_READ);
	}
	format = CAIRO_FORMAT_RGB24;
	surface = cairo_image_surface_create_for_data(map,
						      format, width, height,
						      stride);
	ret = cairo_surface_write_to_png(surface, filename);
	if (ret)
		igt_info("Cairo ret: %d (%s)\n", ret, cairo_status_to_string(ret));
	igt_assert(ret == CAIRO_STATUS_SUCCESS);
	cairo_surface_destroy(surface);

	if (!obj->ptr)
		munmap(map, obj->size);
}

static int compare_nxn(const struct blt_copy_object *surf1,
		       const struct blt_copy_object *surf2,
		       int xsize, int ysize, int bx, int by)
{
	int x, y, corrupted;
	uint32_t pos, px1, px2;

	corrupted = 0;
	for (y = 0; y < ysize; y++) {
		for (x = 0; x < xsize; x++) {
			pos = bx * xsize + by * ysize * surf1->pitch / 4;
			pos += x + y * surf1->pitch / 4;
			px1 = surf1->ptr[pos];
			px2 = surf2->ptr[pos];
			if (px1 != px2)
				corrupted++;
		}
	}

	return corrupted;
}

/**
 * blt_dump_corruption_info_32b:
 * @surf1: first surface
 * @surf2: second surface
 *
 * Function dumps ascii representation of the surfaces corruption. Comparison
 * is performed on 8x8 32bpp color pixel blocks. Number of differences on
 * such block varies from 0 (no corruption) to 64 (pixels on those surfaces
 * differs). It is added then to '0' ascii character to point the corruption
 * occurred, for non affected block '.' is printed out.
 *
 * Idea of this function is to determine character of the differences between
 * two surfaces without generating difference image.
 *
 * Currently function assumes both @surf1 and @surf2 are 32-bit color surfaces.
 */
void blt_dump_corruption_info_32b(const struct blt_copy_object *surf1,
				  const struct blt_copy_object *surf2)
{
	const int xsize = 8, ysize = 8;
	int w, h, bx, by, corrupted;

	igt_assert(surf1->x1 == surf2->x1 && surf1->x2 == surf2->x2);
	igt_assert(surf1->y1 == surf2->y1 && surf1->y2 == surf2->y2);
	w = surf1->x2;
	h = surf1->y2;

	igt_info("dump corruption - width: %d, height: %d, sizex: %x, sizey: %x\n",
		 surf1->x2, surf1->y2, xsize, ysize);

	for (by = 0; by < h / ysize; by++) {
		for (bx = 0; bx < w / xsize; bx++) {
			corrupted = compare_nxn(surf1, surf2, xsize, ysize, bx, by);
			if (corrupted == 0)
				igt_info(".");
			else
				igt_info("%c", '0' + corrupted);
		}
		igt_info("\n");
	}
}

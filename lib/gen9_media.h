#ifndef GEN9_MEDIA_H
#define GEN9_MEDIA_H

#include <stdint.h>
#include "surfaceformat.h"
#include "gen4_render.h"

#define GEN9_PIPELINE_SELECTION_MASK		(3 << 8)

/*  If enabled, it will force awake media enginee and the following instructions
 *  will require that the media enginee is awake.
 */
#define GEN9_FORCE_MEDIA_AWAKE_DISABLE		(0 << 5)
#define GEN9_FORCE_MEDIA_AWAKE_ENABLE		(1 << 5)
#define GEN9_FORCE_MEDIA_AWAKE_MASK		(1 << 13)

#define GEN9_SAMPLER_DOP_GATE_DISABLE		(0 << 4)
#define GEN9_SAMPLER_DOP_GATE_ENABLE		(1 << 4)
#define GEN9_SAMPLER_DOP_GATE_MASK		(1 << 12)

struct gen9_surface_state
{
	struct {
		uint32_t cube_pos_z:1;
		uint32_t cube_neg_z:1;
		uint32_t cube_pos_y:1;
		uint32_t cube_neg_y:1;
		uint32_t cube_pos_x:1;
		uint32_t cube_neg_x:1;
		uint32_t media_boundary_pixel_mode:2;
		uint32_t render_cache_read_write:1;
		uint32_t sampler_l2_bypass_disable:1;
		uint32_t vert_line_stride_ofs:1;
		uint32_t vert_line_stride:1;
		uint32_t tiled_mode:2;
		uint32_t horizontal_alignment:2;
		uint32_t vertical_alignment:2;
		uint32_t surface_format:9;     /**< BRW_SURFACEFORMAT_x */
		uint32_t pad0:1;
		uint32_t is_array:1;
		uint32_t surface_type:3;       /**< BRW_SURFACE_1D/2D/3D/CUBE */
	} ss0;

	struct {
		uint32_t qpitch:15;
		uint32_t pad1:4;
		uint32_t base_mip_level:5;
		uint32_t pxp:1;
		uint32_t mocs_index:6;
		uint32_t pad0:1;
	} ss1;

	struct {
		uint32_t width:14;
		uint32_t pad1:2;
		uint32_t height:14;
		uint32_t pad0:2;
	} ss2;

	struct {
		uint32_t pitch:18;
		uint32_t pad:3;
		uint32_t depth:11;
	} ss3;

	struct {
		uint32_t multisample_position_palette_index:3;
		uint32_t num_multisamples:3;
		uint32_t multisampled_surface_storage_format:1;
		uint32_t render_target_view_extent:11;
		uint32_t min_array_elt:11;
		uint32_t rotation:2;
		uint32_t force_ncmp_reduce_type:1;
	} ss4;

	struct {
		uint32_t mip_count:4;
		uint32_t min_lod:4;
		uint32_t pad3:6;
		uint32_t coherency_type:1;
		uint32_t pad2:5;
		uint32_t ewa_disable_for_cube:1;
		uint32_t y_offset:3;
		uint32_t pad0:1;
		uint32_t x_offset:7;
	} ss5;

	struct {
		uint32_t pad; /* Multisample Control Surface stuff */
	} ss6;

	struct {
		uint32_t resource_min_lod:12;

		/* Only on Haswell */
		uint32_t pad0:4;
		uint32_t shader_chanel_select_a:3;
		uint32_t shader_chanel_select_b:3;
		uint32_t shader_chanel_select_g:3;
		uint32_t shader_chanel_select_r:3;

		uint32_t alpha_clear_color:1;
		uint32_t blue_clear_color:1;
		uint32_t green_clear_color:1;
		uint32_t red_clear_color:1;
	} ss7;

	struct {
		uint32_t base_addr;
	} ss8;

	struct {
		uint32_t base_addr_hi:16;
		uint32_t pad0:16;
	} ss9;

	struct {
		uint32_t pad0:12;
		uint32_t aux_base_addr:20;
	} ss10;

	struct {
		uint32_t aux_base_addr_hi:16;
		uint32_t pad:16;
	} ss11;

	struct {
		uint32_t hiz_depth_clear_value;
	} ss12;

	struct {
		uint32_t reserved;
	} ss13;

	struct {
		uint32_t reserved;
	} ss14;

	struct {
		uint32_t reserved;
	} ss15;
};

#endif /* GEN9_MEDIA_H */

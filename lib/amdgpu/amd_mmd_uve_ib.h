/* SPDX-License-Identifier: MIT
 * Copyright 2023 Advanced Micro Devices, Inc.
 * Copyright 2014 Advanced Micro Devices, Inc.
 */

#ifndef _AMD_MMD_UVE_IB_H_
#define _AMD_MMD_UVE_IB_H_

static const uint32_t uve_session_info[] = {
	0x00000018,
	0x00000001,
	0x00000000,
	0x00010000,
};

static const uint32_t uve_task_info[] = {
	0x00000014,
	0x00000002,
};

static const uint32_t uve_session_init[] = {
	0x00000020,
	0x00000003,
	0x000000c0,
	0x00000080,
	0x00000020,
	0x00000000,
	0x00000000,
	0x00000000,
};

static const uint32_t uve_layer_ctrl[] = {
	0x00000010,
	0x00000004,
	0x00000001,
	0x00000001,
};

static const uint32_t uve_layer_select[] = {
	0x0000000c,
	0x00000005,
	0x00000000,
};

static const uint32_t uve_slice_ctrl[] = {
	0x00000014,
	0x00000006,
	0x00000000,
	0x00000006,
	0x00000006,
};

static const uint32_t uve_spec_misc[] = {
	0x00000024,
	0x00000007,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000001,
	0x00000001,
};

static const uint32_t uve_rc_session_init[] = {
	0x00000010,
	0x00000008,
	0x00000000,
	0x00000040,
};

static const uint32_t uve_rc_layer_init[] = {
	0x00000028,
	0x00000009,
	0x001e8480,
	0x001e8480,
	0x0000001e,
	0x00000001,
	0x0001046a,
	0x0001046a,
	0x0001046a,
	0xaaaaaaaa,
};

static const uint32_t uve_deblocking_filter[] = {
	0x00000020,
	0x0000000e,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
};

static const uint32_t uve_quality_params[] = {
	0x00000014,
	0x0000000d,
	0x00000000,
	0x00000000,
	0x00000000,
};

static const uint32_t uve_feedback_buffer[] = {
	0x0000001c,
	0x00000012,
	0x00000000,
};

static const uint32_t uve_feedback_buffer_additional[] = {
	0x00000108,
	0x00000014,
	0x00000001,
	0x00000010,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
};

static const uint32_t uve_nalu_buffer_1[] = {
	0x00000018,
	0x00000013,
	0x00000001,
	0x00000007,
	0x00000001,
	0x46011000,
};

static const uint32_t uve_nalu_buffer_2[] = {
	0x0000002c,
	0x00000013,
	0x00000002,
	0x0000001b,
	0x00000001,
	0x40010c01,
	0xffff0160,
	0x00000300,
	0xb0000003,
	0x00000300,
	0x962c0900,
};

static const uint32_t uve_nalu_buffer_3[] = {
	0x00000034,
	0x00000013,
	0x00000003,
	0x00000023,
	0x00000001,
	0x42010101,
	0x60000003,
	0x00b00000,
	0x03000003,
	0x0096a018,
	0x2020708f,
	0xcb924295,
	0x12e08000,
};

static const uint32_t uve_nalu_buffer_4[] = {
	0x0000001c,
	0x00000013,
	0x00000004,
	0x0000000b,
	0x00000001,
	0x4401e0f1,
	0x80992000,
};

static const uint32_t uve_slice_header[] = {
	0x000000c8,
	0x0000000b,
	0x28010000,
	0x40000000,
	0x60000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000002,
	0x00000010,
	0x00000003,
	0x00000000,
	0x00000002,
	0x00000002,
	0x00000004,
	0x00000000,
	0x00000001,
	0x00000000,
	0x00000002,
	0x00000003,
	0x00000005,
	0x00000000,
	0x00000002,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
};

static const uint32_t uve_encode_param[] = {
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0xffffffff,
	0x00000001,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
};

static const uint32_t uve_intra_refresh[] = {
	0x00000014,
	0x0000000f,
	0x00000000,
	0x00000000,
	0x00000001,
};

static const uint32_t uve_ctx_buffer[] = {
	0x00000000,
	0x00000000,
	0x000000a0,
	0x000000a0,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
};

static const uint32_t uve_bitstream_buffer[] = {
	0x0000001c,
	0x00000011,
};

static const uint32_t uve_rc_per_pic[] = {
	0x00000024,
	0x0000000a,
	0x0000001a,
	0x00000000,
	0x00000033,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000001,
};

static const uint32_t uve_op_init[] = {
	0x00000008,
	0x08000001,
};

static const uint32_t uve_op_close[] = {
	0x00000008,
	0x08000002,
};

static const uint32_t uve_op_encode[] = {
	0x00000008,
	0x08000003,
};

static const uint32_t uve_op_init_rc[] = {
	0x00000008,
	0x08000004,
};

static const uint32_t uve_op_init_rc_vbv_level[] = {
	0x00000008,
	0x08000005,
};

static const uint32_t uve_op_speed_enc_mode[] = {
	0x00000008,
	0x08000006,
};

static const uint32_t uve_op_balance_enc_mode[] = {
	0x00000008,
	0x08000007,
};

static const uint32_t uve_op_quality_enc_mode[] = {
	0x00000008,
	0x08000008,
};
#endif /*_AMD_MMD_UVE_IB_H_*/

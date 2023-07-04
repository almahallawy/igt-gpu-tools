/* SPDX-License-Identifier: MIT
 * Copyright 2023 Advanced Micro Devices, Inc.
 * Copyright 2014 Advanced Micro Devices, Inc.
 */

#ifndef _AMD_MMD_UTIL_MATH_H_
#define _AMD_MMD_UTIL_MATH_H_

#define MIN2(A, B)   ((A) < (B) ? (A) : (B))
#define MAX2(A, B)   ((A) > (B) ? (A) : (B))
#define MAX3(A, B, C) ((A) > (B) ? MAX2(A, C) : MAX2(B, C))

#define __align_mask(value, mask)  (((value) + (mask)) & ~(mask))

#endif /*_AMD_MMD_UTIL_MATH_H_*/

/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef INTEL_PAT_H
#define INTEL_PAT_H

#include <stdint.h>

#define DEFAULT_PAT_INDEX ((uint8_t)-1) /* igt-core can pick 1way or better */

uint8_t intel_get_max_pat_index(int fd);

uint8_t intel_get_pat_idx_uc(int fd);
uint8_t intel_get_pat_idx_wt(int fd);
uint8_t intel_get_pat_idx_wb(int fd);

#endif /* INTEL_PAT_H */

/*
 * Copyright © 2022 Isabella Basso do Amaral <isabbasso@riseup.net>
 * Copyright © 2023 Intel Corporation
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

#ifndef IGT_KTAP_H
#define IGT_KTAP_H

#define BUF_LEN 4096

#include "igt_list.h"

struct igt_ktap_result {
	struct igt_list_head link;
	char *suite_name;
	char *case_name;
	char *msg;
	int code;
};

struct igt_ktap_results;

struct igt_ktap_results *igt_ktap_alloc(struct igt_list_head *results);
int igt_ktap_parse(const char *buf, struct igt_ktap_results *ktap);
void igt_ktap_free(struct igt_ktap_results *ktap);

#endif /* IGT_KTAP_H */

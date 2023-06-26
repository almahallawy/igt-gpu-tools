// SPDX-License-Identifier: MIT
/*
* Copyright © 2022 Intel Corporation
*/

#include <unistd.h>

#include "drmtest.h"
#include "igt_types.h"
#include "xe/xe_query.h"

void igt_cleanup_fd(volatile int *fd)
{
	if (!fd || *fd < 0)
		return;

	/* Remove xe_device from cache. */
	if (is_xe_device(*fd))
		xe_device_put(*fd);

	close(*fd);
	*fd = -1;
}

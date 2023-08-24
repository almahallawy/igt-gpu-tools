/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#include <fcntl.h>

#include "igt.h"

#include "intel_fbc.h"

#define FBC_STATUS_BUF_LEN 128

/**
 * intel_fbc_supported_on_chipset:
 * @device: fd of the device
 * @pipe: Display pipe
 *
 * Check if FBC is supported by chipset on given pipe.
 *
 * Returns:
 * true if FBC is supported and false otherwise.
 */
bool intel_fbc_supported_on_chipset(int device, enum pipe pipe)
{
	char buf[FBC_STATUS_BUF_LEN];
	int dir;

	dir = igt_debugfs_pipe_dir(device, pipe, O_DIRECTORY);
	igt_require_fd(dir);
	igt_debugfs_simple_read(dir, "i915_fbc_status", buf, sizeof(buf));
	close(dir);
	if (*buf == '\0')
		return false;

	return !strstr(buf, "FBC unsupported on this chipset\n") &&
		!strstr(buf, "stolen memory not initialised\n");
}

static bool _intel_fbc_is_enabled(int device, enum pipe pipe, int log_level, char *last_fbc_buf)
{
	char buf[FBC_STATUS_BUF_LEN];
	bool print = true;
	int dir;

	dir = igt_debugfs_pipe_dir(device, pipe, O_DIRECTORY);
	igt_require_fd(dir);
	igt_debugfs_simple_read(dir, "i915_fbc_status", buf, sizeof(buf));
	close(dir);
	if (log_level != IGT_LOG_DEBUG)
		last_fbc_buf[0] = '\0';
	else if (strcmp(last_fbc_buf, buf))
		strcpy(last_fbc_buf, buf);
	else
		print = false;

	if (print)
		igt_log(IGT_LOG_DOMAIN, log_level, "fbc_is_enabled():\n%s\n", buf);

	return strstr(buf, "FBC enabled\n");
}

/**
 * intel_fbc_is_enabled:
 * @device: fd of the device
 * @pipe: Display pipe
 * @log_level: Wanted loglevel
 *
 * Check if FBC is enabled on given pipe. Loglevel can be used to
 * control at which loglevel current state is printed out.
 *
 * Returns:
 * true if FBC is enabled.
 */
bool intel_fbc_is_enabled(int device, enum pipe pipe, int log_level)
{
	char last_fbc_buf[FBC_STATUS_BUF_LEN] = {'\0'};

	return _intel_fbc_is_enabled(device, pipe, log_level, last_fbc_buf);
}

/**
 * intel_fbc_wait_until_enabled:
 * @device: fd of the device
 * @pipe: Display pipe
 *
 * Wait until fbc is enabled. Used timeout is constant 2 seconds.
 *
 * Returns:
 * true if FBC got enabled.
 */
bool intel_fbc_wait_until_enabled(int device, enum pipe pipe)
{
	char last_fbc_buf[FBC_STATUS_BUF_LEN] = {'\0'};

	return igt_wait(_intel_fbc_is_enabled(device, pipe, IGT_LOG_DEBUG, last_fbc_buf), 2000, 1);
}

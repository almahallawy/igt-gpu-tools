// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

/**
 * TEST: cause fake gt reset failure and listen uevent from KMD
 * Category: Software building block
 * SUBTEST:fake_reset_uevent_listener
 * Functionality: uevent
 * Sub-category: GT reset failure uevent
 * Test category: functionality test
 * Description:
 *		Test creates uevent listener and causes fake reset failure for gt0
 *		and returns success if uevent is sent by driver and listened by listener.
 */

#include <libudev.h>
#include <string.h>
#include <sys/stat.h>

#include "igt.h"

#include "xe_drm.h"
#include "xe/xe_ioctl.h"
#include "xe/xe_query.h"

static void xe_fail_gt_reset(int fd, int gt)
{
	igt_debugfs_write(fd, "fail_gt_reset/probability", "100");
	igt_debugfs_write(fd, "fail_gt_reset/times", "2");

	xe_force_gt_reset(fd, gt);
}

static bool listen_reset_fail_uevent(struct udev_device *device, const char *source, int gt_id)
{
	struct udev_list_entry *list_entry;
	bool dev_needs_reset = false;
	bool tile_id_passed = false;
	bool gt_id_matches = false;
	const char *name, *val;

	udev_list_entry_foreach(list_entry, udev_device_get_properties_list_entry(device))
	{
		name = udev_list_entry_get_name(list_entry);
		val = udev_list_entry_get_value(list_entry);

		if (!strcmp(name, "DEVICE_STATUS") && !strcmp(val, "NEEDS_RESET")) {
			igt_debug("%s = %s\n", name, val);
			dev_needs_reset = true;
			continue;
		}

		if (!strcmp(name, "TILE_ID")) {
			igt_debug("%s = %s\n", name, val);
			tile_id_passed = true;
			continue;
		}

		if (!strcmp(name, "GT_ID") && (atoi(val) == gt_id)) {
			igt_debug("%s = %s\n", name, val);
			gt_id_matches = true;
			continue;
		}
	}

	return (dev_needs_reset && tile_id_passed && gt_id_matches);
}

static void fake_reset_uevent_listener(int fd, int gt_id)
{
	struct udev *udev;
	struct udev_device *dev;
	struct udev_monitor *mon;
	bool event_received = false;
	bool event_sent = false;
	const u32 listener_timeout = 5;

	/* create udev object */
	udev = udev_new();
	if (!udev)
		igt_assert_f(false, "New udev object creation failed");

	mon = udev_monitor_new_from_netlink(udev, "kernel");
	udev_monitor_filter_add_match_subsystem_devtype(mon, "pci", NULL);
	udev_monitor_enable_receiving(mon);
	igt_until_timeout(listener_timeout) {
		if (event_sent) {
			dev = udev_monitor_receive_device(mon);
			if (dev) {
				event_received = listen_reset_fail_uevent(dev, "kernel", gt_id);
				udev_device_unref(dev);
			}
		} else {
			event_sent = true;
			xe_fail_gt_reset(fd, gt_id);
		}

		if (event_received)
			break;
	}

	udev_unref(udev);
	igt_assert_f(event_received, "Event not received");
}

igt_main
{
	int fd;
	int gt;
	const u32 settle_xe_load_uevents = 50000;

	igt_fixture
		fd = drm_open_driver(DRIVER_XE);

	/* Ensures uevents triggered in case of driver
	 * load are settled down.
	 */
	usleep(settle_xe_load_uevents);

	igt_subtest("fake_reset_uevent_listener")
		xe_for_each_gt(fd, gt) {
			fake_reset_uevent_listener(fd, gt);
		}

	igt_fixture
		drm_close_driver(fd);
}

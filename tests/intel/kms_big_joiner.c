/*
 * Copyright © 2020 Intel Corporation
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
 *
 * Author:
 *  Karthik B S <karthik.b.s@intel.com>
 */

/**
 * TEST: kms big joiner
 * Category: Display
 * Description: Test big joiner
 */
#include "igt.h"

IGT_TEST_DESCRIPTION("Test big joiner");

struct bigjoiner_output {
	uint32_t output_id;
	drmModeModeInfo mode;
};

typedef struct {
	int drm_fd;
	igt_display_t display;
	struct igt_fb fb;
	int n_pipes;
	enum pipe pipe1;
	enum pipe pipe2;
	struct bigjoiner_output output[2];
} data_t;

static int max_dotclock;

/**
 * SUBTEST: invalid-modeset
 * Description: Verify if the modeset on the adjoining pipe is rejected when
 *              the pipe is active with a big joiner modeset
 * Driver requirement: i915, xe
 * Functionality: 2p1p
 * Mega feature: Bigjoiner
 * Test category: functionality test
 */
static void test_invalid_modeset(data_t *data)
{
	igt_output_t *output;
	igt_display_t *display = &data->display;
	int ret;

	igt_info("Bigjoiner test on ");
	for_each_connected_output(display, output){
		enum pipe p = output->pending_pipe;
		drmModeModeInfo *mode;
		igt_pipe_t *pipe;
		igt_plane_t *plane;

		if (p == PIPE_NONE)
			continue;

		mode = igt_output_get_mode(output);
		igt_info("pipe:%s, output:%s, mode:", kmstest_pipe_name(p), igt_output_name(output));
		kmstest_dump_mode(mode);

		pipe = &display->pipes[p];
		plane = igt_pipe_get_plane_type(pipe, DRM_PLANE_TYPE_PRIMARY);

		igt_plane_set_fb(plane, &data->fb);
		igt_fb_set_size(&data->fb, plane, mode->hdisplay, mode->vdisplay);
		igt_plane_set_size(plane, mode->hdisplay, mode->vdisplay);
	}

	igt_assert(!igt_check_bigjoiner_support(display));

	/* This commit is expectd to fail as this pipe is being used for big joiner */
	ret = igt_display_try_commit_atomic(display, DRM_MODE_ATOMIC_TEST_ONLY |
					    DRM_MODE_ATOMIC_ALLOW_MODESET, NULL);

	igt_display_reset(&data->display);
	igt_display_commit2(display, COMMIT_ATOMIC);

	igt_assert_lt(ret, 0);
}

/**
 * SUBTEST: basic
 * Description: Verify the basic modeset on big joiner mode on all pipes
 * Driver requirement: i915, xe
 * Functionality: 2p1p
 * Mega feature: Bigjoiner
 * Test category: functionality test
 */
static void test_basic_modeset(data_t *data)
{
	drmModeModeInfo *mode;
	igt_output_t *output, *bigjoiner_output = NULL;
	igt_display_t *display = &data->display;
	igt_pipe_t *pipe;
	igt_plane_t *plane;

	igt_display_reset(display);

	for_each_connected_output(display, output) {
		if (data->output[0].output_id == output->id) {
			bigjoiner_output = output;
			break;
		}
	}

	igt_output_set_pipe(bigjoiner_output, data->pipe1);

	mode = &data->output[0].mode;
	igt_output_override_mode(bigjoiner_output, mode);

	pipe = &display->pipes[data->pipe1];
	plane = igt_pipe_get_plane_type(pipe, DRM_PLANE_TYPE_PRIMARY);

	igt_plane_set_fb(plane, &data->fb);
	igt_fb_set_size(&data->fb, plane, mode->hdisplay, mode->vdisplay);
	igt_plane_set_size(plane, mode->hdisplay, mode->vdisplay);

	igt_display_commit2(display, COMMIT_ATOMIC);

	igt_output_set_pipe(bigjoiner_output, PIPE_NONE);
	igt_plane_set_fb(plane, NULL);
	igt_display_commit2(display, COMMIT_ATOMIC);
}

/**
 * SUBTEST: 2x-modeset
 * Description: Verify simultaneous modeset on 2 big joiner outputs
 * Driver requirement: i915, xe
 * Functionality: 2p1p
 * Mega feature: Bigjoiner
 * Test category: functionality test
 */
static void test_dual_display(data_t *data)
{
	drmModeModeInfo *mode;
	igt_output_t *output, *bigjoiner_output[2];
	igt_display_t *display = &data->display;
	igt_pipe_t *pipe;
	igt_plane_t *plane1, *plane2;
	int count = 0;

	igt_display_reset(display);

	for_each_connected_output(display, output) {
		if (data->output[count].output_id == output->id) {
			bigjoiner_output[count] = output;
			count++;
		}

		if (count > 1)
			break;
	}

	igt_output_set_pipe(bigjoiner_output[0], data->pipe1);
	igt_output_set_pipe(bigjoiner_output[1], data->pipe2);

	/* Set up first big joiner output on Pipe A*/
	mode = &data->output[0].mode;
	igt_output_override_mode(bigjoiner_output[0], mode);

	pipe = &display->pipes[data->pipe1];
	plane1 = igt_pipe_get_plane_type(pipe, DRM_PLANE_TYPE_PRIMARY);

	igt_plane_set_fb(plane1, &data->fb);
	igt_fb_set_size(&data->fb, plane1, mode->hdisplay, mode->vdisplay);
	igt_plane_set_size(plane1, mode->hdisplay, mode->vdisplay);

	/* Set up second big joiner output on Pipe C*/
	mode = &data->output[1].mode;
	igt_output_override_mode(bigjoiner_output[1], mode);

	pipe = &display->pipes[data->pipe2];
	plane2 = igt_pipe_get_plane_type(pipe, DRM_PLANE_TYPE_PRIMARY);

	igt_plane_set_fb(plane2, &data->fb);
	igt_fb_set_size(&data->fb, plane2, mode->hdisplay, mode->vdisplay);
	igt_plane_set_size(plane2, mode->hdisplay, mode->vdisplay);

	igt_display_commit2(display, COMMIT_ATOMIC);

	/* Clean up */
	igt_output_set_pipe(bigjoiner_output[0], PIPE_NONE);
	igt_output_set_pipe(bigjoiner_output[1], PIPE_NONE);
	igt_plane_set_fb(plane1, NULL);
	igt_plane_set_fb(plane2, NULL);
	igt_display_commit2(display, COMMIT_ATOMIC);
}

static bool bigjoiner_mode_found(drmModeConnector *connector,
				 int (*sort_method)(const void *, const void*),
				 drmModeModeInfo *mode)
{
	igt_sort_connector_modes(connector, sort_method);
	*mode = connector->modes[0];

	return igt_bigjoiner_possible(mode, max_dotclock);
}

igt_main
{
	data_t data;
	igt_output_t *output;
	drmModeModeInfo *mode;
	int valid_output = 0, i, count = 0, j = 0;
	uint16_t width = 0, height = 0;
	enum pipe pipe_seq[IGT_MAX_PIPES];

	igt_fixture {
		data.drm_fd = drm_open_driver_master(DRIVER_INTEL | DRIVER_XE);
		kmstest_set_vt_graphics_mode();

		igt_display_require(&data.display, data.drm_fd);
		igt_require(data.display.is_atomic);

		max_dotclock = igt_get_max_dotclock(data.drm_fd);

		for_each_connected_output(&data.display, output) {
			bool found = false;
			drmModeConnector *connector = output->config.connector;

			/*
			 * Bigjoiner will come in to the picture when the
			 * resolution > 5K or clock > max-dot-clock.
			 */
			found = (bigjoiner_mode_found(connector, sort_drm_modes_by_res_dsc, mode) ||
				 bigjoiner_mode_found(connector, sort_drm_modes_by_clk_dsc, mode)) ?
					true : false;

			if (found) {
				data.output[count].output_id = output->id;
				memcpy(&data.output[count].mode, mode, sizeof(drmModeModeInfo));
				count++;

				width = max(width, mode->hdisplay);
				height = max(height, mode->vdisplay);
			}
			valid_output++;
		}

		data.n_pipes = 0;
		for_each_pipe(&data.display, i) {
			data.n_pipes++;
			pipe_seq[j] = i;
			j++;
		}

		igt_require_f(count > 0, "No output with 5k+ mode (or) clock > max-dot-clock found\n");

		igt_create_pattern_fb(data.drm_fd, width, height, DRM_FORMAT_XRGB8888,
				      DRM_FORMAT_MOD_LINEAR, &data.fb);
	}

	igt_describe("Verify the basic modeset on big joiner mode on all pipes");
	igt_subtest_with_dynamic("basic") {
		for (i = 0; i < data.n_pipes - 1; i++) {
			data.pipe1 = pipe_seq[i];
			igt_dynamic_f("pipe-%s", kmstest_pipe_name(pipe_seq[i]))
				test_basic_modeset(&data);
		}
	}

	igt_describe("Verify if the modeset on the adjoining pipe is rejected "
		     "when the pipe is active with a big joiner modeset");
	igt_subtest_with_dynamic("invalid-modeset") {
		data.pipe1 = pipe_seq[j - 1];

		igt_display_reset(&data.display);
		for_each_connected_output(&data.display, output) {
			if (data.output[0].output_id != output->id)
				continue;

			mode = &data.output[0].mode;
			igt_output_set_pipe(output, data.pipe1);
			igt_output_override_mode(output, mode);

			igt_dynamic_f("pipe-%s-%s",
				      kmstest_pipe_name(data.pipe1),
				      igt_output_name(output))
				test_invalid_modeset(&data);
		}

		if(valid_output > 1) {
			for (i = 0; i < data.n_pipes - 1; i++) {
				igt_output_t *first_output = NULL, *second_output = NULL;

				data.pipe1 = pipe_seq[i];
				data.pipe2 = pipe_seq[i + 1];

				igt_display_reset(&data.display);
				for_each_connected_output(&data.display, output) {
					if (data.output[0].output_id == output->id) {
						first_output = output;
						mode = &data.output[0].mode;

						igt_output_set_pipe(output, data.pipe1);
						igt_output_override_mode(output, mode);
					} else if (second_output == NULL) {
						second_output = output;
						igt_output_set_pipe(output, data.pipe2);

						break;
					}
				}

				igt_dynamic_f("pipe-%s-%s-pipe-%s-%s",
					      kmstest_pipe_name(data.pipe1),
					      igt_output_name(first_output),
					      kmstest_pipe_name(data.pipe2),
					      igt_output_name(second_output))
					test_invalid_modeset(&data);
			}
		}
	}

	igt_describe("Verify simultaneous modeset on 2 big joiner outputs");
	igt_subtest_with_dynamic("2x-modeset") {
		igt_require_f(count > 1, "2 outputs with big joiner modes are required\n");
		igt_require_f(data.n_pipes > 3, "Minumum of 4 pipes are required\n");
		for (i = 0; (i + 2) < data.n_pipes - 1; i++) {
			data.pipe1 = pipe_seq[i];
			data.pipe2 = pipe_seq[i + 2];
			igt_dynamic_f("pipe-%s-%s", kmstest_pipe_name(pipe_seq[i]), kmstest_pipe_name(pipe_seq[i + 2]))
				test_dual_display(&data);
		}
	}

	igt_fixture {
		igt_remove_fb(data.drm_fd, &data.fb);
		igt_display_fini(&data.display);
		drm_close_driver(data.drm_fd);
	}
}

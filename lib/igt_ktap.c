// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Isabella Basso do Amaral <isabbasso@riseup.net>
 * Copyright © 2023 Intel Corporation
 */

#include <ctype.h>
#include <limits.h>
#include <libkmod.h>
#include <pthread.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "igt_aux.h"
#include "igt_core.h"
#include "igt_ktap.h"
#include "igt_list.h"

enum ktap_phase {
	KTAP_START,
	SUITE_COUNT,
	SUITE_START,
	SUITE_NAME,
	CASE_COUNT,
	CASE_NAME,
	SUB_RESULT,
	CASE_RESULT,
	SUITE_RESULT,
};

struct igt_ktap_results {
	enum ktap_phase expect;
	unsigned int suite_count;
	unsigned int suite_last;
	char *suite_name;
	unsigned int case_count;
	unsigned int case_last;
	char *case_name;
	unsigned int sub_last;
	struct igt_list_head *results;
};

/**
 * igt_ktap_parse:
 *
 * This function parses a line of text for KTAP report data
 * and passes results back to IGT kunit layer.
 */
int igt_ktap_parse(const char *buf, struct igt_ktap_results *ktap)
{
	char *suite_name = NULL, *case_name = NULL, *msg = NULL;
	struct igt_ktap_result *result;
	int code = IGT_EXIT_INVALID;
	unsigned int n, len;
	char s[2];

	/* KTAP report header */
	if (igt_debug_on(sscanf(buf, "KTAP%*[ ]version%*[ ]%u %n",
				&n, &len) == 1 && len == strlen(buf))) {
		if (igt_debug_on(ktap->expect != KTAP_START))
			return -EPROTO;

		ktap->suite_count = 0;
		ktap->expect = SUITE_COUNT;

	/* malformed TAP test plan? */
	} else if (len = 0,
		   igt_debug_on(sscanf(buf, " 1..%1[ ]", s) == 1)) {
		return -EINPROGRESS;

	/* valid test plan of a KTAP report */
	} else if (igt_debug_on(sscanf(buf, "1..%u %n", &n, &len) == 1 &&
				len == strlen(buf))) {
		if (igt_debug_on(ktap->expect != SUITE_COUNT))
			return -EPROTO;

		if (!n)
			return 0;

		ktap->suite_count = n;
		ktap->suite_last = 0;
		ktap->suite_name = NULL;
		ktap->expect = SUITE_START;

	/* KTAP test suite header */
	} else if (len = 0,
		   igt_debug_on(sscanf(buf,
				       "%*1[ ]%*1[ ]%*1[ ]%*1[ ]KTAP%*[ ]version%*[ ]%u %n",
				       &n, &len) == 1 && len == strlen(buf))) {
		/*
		 * TODO: drop the following workaround as soon as
		 * kernel side issue of missing lines with top level
		 * KTAP version and test suite plan is fixed.
		 */
		if (ktap->expect == KTAP_START) {
			ktap->suite_count = 1;
			ktap->suite_last = 0;
			ktap->suite_name = NULL;
			ktap->expect = SUITE_START;
		}

		if (igt_debug_on(ktap->expect != SUITE_START))
			return -EPROTO;

		ktap->expect = SUITE_NAME;

	/* KTAP test suite name */
	} else if (len = 0,
		   igt_debug_on(sscanf(buf,
				       "%*1[ ]%*1[ ]%*1[ ]%*1[ ]#%*[ ]Subtest:%*[ ]%ms %n",
				       &suite_name, &len) == 1 && len == strlen(buf))) {
		if (igt_debug_on(ktap->expect != SUITE_NAME))
			return -EPROTO;

		ktap->suite_name = suite_name;
		suite_name = NULL;
		ktap->case_count = 0;
		ktap->expect = CASE_COUNT;

	/* valid test plan of a KTAP test suite */
	} else if (len = 0, free(suite_name), suite_name = NULL,
		   igt_debug_on(sscanf(buf,
				       "%*1[ ]%*1[ ]%*1[ ]%*1[ ]1..%u %n",
				       &n, &len) == 1 && len == strlen(buf))) {
		if (igt_debug_on(ktap->expect != CASE_COUNT))
			return -EPROTO;

		if (n) {
			ktap->case_count = n;
			ktap->case_last = 0;
			ktap->case_name = NULL;
			ktap->expect = CASE_RESULT;
		} else {
			ktap->expect = SUITE_RESULT;
		}

	/* KTAP parametrized test case header */
	} else if (len = 0,
		   igt_debug_on(sscanf(buf,
				       "%*1[ ]%*1[ ]%*1[ ]%*1[ ]%*1[ ]%*1[ ]%*1[ ]%*1[ ]KTAP%*[ ]version%*[ ]%u %n",
				       &n, &len) == 1 && len == strlen(buf))) {
		if (igt_debug_on(ktap->expect != CASE_RESULT))
			return -EPROTO;

		ktap->sub_last = 0;
		ktap->expect = CASE_NAME;

	/* KTAP parametrized test case name */
	} else if (len = 0,
		   igt_debug_on(sscanf(buf,
				       "%*1[ ]%*1[ ]%*1[ ]%*1[ ]%*1[ ]%*1[ ]%*1[ ]%*1[ ]#%*[ ]Subtest:%*[ ]%ms %n",
				       &case_name, &len) == 1 && len == strlen(buf))) {
		if (igt_debug_on(ktap->expect != CASE_NAME))
			return -EPROTO;

		n = ktap->case_last + 1;
		ktap->expect = SUB_RESULT;

	/* KTAP parametrized subtest result */
	} else if (len = 0, free(case_name), case_name = NULL,
		   igt_debug_on(sscanf(buf,
				       "%*1[ ]%*1[ ]%*1[ ]%*1[ ]%*1[ ]%*1[ ]%*1[ ]%*1[ ]ok%*[ ]%u%*[ ]%*[^#\n]%1[#\n]",
				       &n, s) == 2) ||
		   igt_debug_on(sscanf(buf,
				       "%*1[ ]%*1[ ]%*1[ ]%*1[ ]%*1[ ]%*1[ ]%*1[ ]%*1[ ]not%*1[ ]ok%*[ ]%u%*[ ]%*[^#\n]%1[#\n]",
				       &n, s) == 2)) {
		/* at lease one result of a parametrised subtest expected */
		if (igt_debug_on(ktap->expect == SUB_RESULT &&
				 ktap->sub_last == 0))
			ktap->expect = CASE_RESULT;

		if (igt_debug_on(ktap->expect != CASE_RESULT) ||
		    igt_debug_on(n != ++ktap->sub_last))
			return -EPROTO;

	/* KTAP test case skip result */
	} else if ((igt_debug_on(sscanf(buf,
					"%*1[ ]%*1[ ]%*1[ ]%*1[ ]ok%*[ ]%u%*[ ]%ms%*[ ]#%*[ ]SKIP %n",
					&n, &case_name, &len) == 2 &&
				 len == strlen(buf))) ||
		   (len = 0, free(case_name), case_name = NULL,
		    igt_debug_on(sscanf(buf,
					"%*1[ ]%*1[ ]%*1[ ]%*1[ ]ok%*[ ]%u%*[ ]%ms%*[ ]#%*[ ]SKIP%*[ ]%m[^\n]",
					&n, &case_name, &msg) == 3))) {
		code = IGT_EXIT_SKIP;

	/* KTAP test case pass result */
	} else if ((free(case_name), case_name = NULL, free(msg), msg = NULL,
		    igt_debug_on(sscanf(buf,
					"%*1[ ]%*1[ ]%*1[ ]%*1[ ]ok%*[ ]%u%*[ ]%ms %n",
					&n, &case_name, &len) == 2 &&
				 len == strlen(buf))) ||
		   (len = 0, free(case_name), case_name = NULL,
		    igt_debug_on(sscanf(buf,
					"%*1[ ]%*1[ ]%*1[ ]%*1[ ]ok%*[ ]%u%*[ ]%ms%*[ ]#%*[ ]%m[^\n]",
					&n, &case_name, &msg) == 3))) {
		code = IGT_EXIT_SUCCESS;

	/* KTAP test case fail result */
	} else if ((free(case_name), case_name = NULL, free(msg), msg = NULL,
		    igt_debug_on(sscanf(buf,
					"%*1[ ]%*1[ ]%*1[ ]%*1[ ]not%*1[ ]ok%*[ ]%u%*[ ]%ms %n",
					&n, &case_name, &len) == 2 &&
				 len == strlen(buf))) ||
		   (len = 0, free(case_name), case_name = NULL,
		    igt_debug_on(sscanf(buf,
					"%*1[ ]%*1[ ]%*1[ ]%*1[ ]not%*1[ ]ok%*[ ]%u%*[ ]%ms%*[ ]#%*[ ]%m[^\n]",
					&n, &case_name, &msg) == 3))) {
		code = IGT_EXIT_FAILURE;

	/* KTAP test suite result */
	} else if ((free(case_name), free(msg),
		    igt_debug_on(sscanf(buf, "ok%*[ ]%u%*[ ]%ms %n",
					&n, &suite_name, &len) == 2 &&
				 len == strlen(buf))) ||
		   (len = 0, free(suite_name), suite_name = NULL,
		    igt_debug_on(sscanf(buf, "ok%*[ ]%u%*[ ]%ms%*[ ]%1[#]",
					&n, &suite_name, s) == 3)) ||
		   (free(suite_name), suite_name = NULL,
		    igt_debug_on(sscanf(buf,
					"not%*[ ]ok%*[ ]%u%*[ ]%ms %n",
					&n, &suite_name, &len) == 2 &&
				 len == strlen(buf))) ||
		   (len = 0, free(suite_name), suite_name = NULL,
		    igt_debug_on(sscanf(buf,
					"not%*[ ]ok%*[ ]%u%*[ ]%ms%*[ ]%1[#]",
					&n, &suite_name, s) == 3))) {
		if (igt_debug_on(ktap->expect != SUITE_RESULT) ||
		    igt_debug_on(strcmp(suite_name, ktap->suite_name)) ||
		    igt_debug_on(n != ++ktap->suite_last) ||
		    igt_debug_on(n > ktap->suite_count)) {
			free(suite_name);
			return -EPROTO;
		}
		free(suite_name);

		/* last test suite? */
		if (igt_debug_on(n == ktap->suite_count))
			return 0;

		ktap->suite_name = NULL;
		ktap->expect = SUITE_START;

	} else {
		return -EINPROGRESS;
	}

	/* neither a test case name nor result */
	if (ktap->expect != SUB_RESULT && code == IGT_EXIT_INVALID)
		return -EINPROGRESS;

	if (igt_debug_on(ktap->expect == SUB_RESULT &&
			 code != IGT_EXIT_INVALID) ||
	    igt_debug_on(code != IGT_EXIT_INVALID &&
			 ktap->expect != CASE_RESULT) ||
	    igt_debug_on(!ktap->suite_name) || igt_debug_on(!case_name) ||
	    igt_debug_on(ktap->expect == CASE_RESULT && ktap->case_name &&
			 strcmp(case_name, ktap->case_name)) ||
	    igt_debug_on(n > ktap->case_count) ||
	    igt_debug_on(n != (ktap->expect == SUB_RESULT ?
			       ktap->case_last + 1: ++ktap->case_last))) {
		free(case_name);
		free(msg);
		return -EPROTO;
	}

	if (ktap->expect == SUB_RESULT) {
		/* KTAP parametrized test case name */
		ktap->case_name = case_name;

	} else {
		/* KTAP test case result */
		ktap->case_name = NULL;

		/* last test case in a suite */
		if (n == ktap->case_count)
			ktap->expect = SUITE_RESULT;
	}

	if (igt_debug_on((result = calloc(1, sizeof(*result)), !result))) {
		free(case_name);
		free(msg);
		return -ENOMEM;
	}

	result->suite_name = ktap->suite_name;
	result->case_name = case_name;
	result->code = code;
	result->msg = msg;
	igt_list_add_tail(&result->link, ktap->results);

	return -EINPROGRESS;
}

struct igt_ktap_results *igt_ktap_alloc(struct igt_list_head *results)
{
	struct igt_ktap_results *ktap = calloc(1, sizeof(*ktap));

	if (!ktap)
		return NULL;

	ktap->expect = KTAP_START;
	ktap->results = results;

	return ktap;
}

void igt_ktap_free(struct igt_ktap_results *ktap)
{
	free(ktap);
}

#define DELIMITER "-"

struct ktap_parser_args {
	int fd;
	bool is_builtin;
	int ret;
} ktap_args;

static struct ktap_test_results results;

static int log_to_end(enum igt_log_level level, int fd,
		      char *record, const char *format, ...) __attribute__((format(printf, 4, 5)));

/**
 * log_to_end:
 * @level: #igt_log_level
 * @record: record to store the read data
 * @format: format string
 * @...: optional arguments used in the format string
 *
 * This is an altered version of the generic structured logging helper function
 * igt_log capable of reading to the end of a given line.
 *
 * Returns: 0 for success, or -2 if there's an error reading from the file
 */
static int log_to_end(enum igt_log_level level, int fd,
		      char *record, const char *format, ...)
{
	va_list args;
	const char *lend;

	/* Cutoff after newline character, in order to not display garbage */
	char *cutoff = strchr(record, '\n');
	if (cutoff) {
		if (cutoff - record < BUF_LEN)
			cutoff[1] = '\0';
	}

	va_start(args, format);
	igt_vlog(IGT_LOG_DOMAIN, level, format, args);
	va_end(args);

	lend = strchrnul(record, '\n');
	while (*lend == '\0') {
		igt_log(IGT_LOG_DOMAIN, level, "%s", record);

		if (read(fd, record, BUF_LEN) < 0) {
			if (errno == EPIPE)
				igt_warn("kmsg truncated: too many messages. You may want to increase log_buf_len in kmcdline\n");
			else
				igt_warn("an error occurred while reading kmsg: %m\n");

			return -2;
		}

		lend = strchrnul(record, '\n');
	}
	return 0;
}

/**
 * lookup_value:
 * @haystack: the string to search in
 * @needle: the string to search for
 *
 * Returns: the value of the needle in the haystack, or -1 if not found.
 */
static long lookup_value(const char *haystack, const char *needle)
{
	const char *needle_rptr;
	char *needle_end;
	long num;

	needle_rptr = strcasestr(haystack, needle);

	if (needle_rptr == NULL)
		return -1;

	/* Skip search string and whitespaces after it */
	needle_rptr += strlen(needle);

	num = strtol(needle_rptr, &needle_end, 10);

	if (needle_rptr == needle_end)
		return -1;

	if (num == LONG_MIN || num == LONG_MAX)
		return 0;

	return num > 0 ? num : 0;
}

/**
 * tap_version_present:
 * @record: buffer with tap data
 * @print_info: whether tap version should be printed or not
 *
 * Returns:
 * 0 if not found
 * 1 if found
 */
static int tap_version_present(char* record, bool print_info)
{
	/*
	 * "(K)TAP version XX" should be the first line on all (sub)tests as per
	 * https://kernel.org/doc/html/latest/dev-tools/ktap.html#version-lines
	 *
	 * but actually isn't, as it currently depends on the KUnit module
	 * being built-in, so we can't rely on it every time
	 */
	const char *version_rptr = strcasestr(record, "TAP version ");
	char *cutoff;

	if (version_rptr == NULL)
		return 0;

	/* Cutoff after newline character, in order to not display garbage */
	cutoff = strchr(version_rptr, '\n');
	if (cutoff)
		cutoff[0] = '\0';

	if (print_info)
		igt_info("%s\n", version_rptr);

	return 1;
}

/**
 * find_next_tap_subtest:
 * @fd: file descriptor
 * @record: buffer used to read fd
 * @is_builtin: whether KUnit is built-in or not
 *
 * Returns:
 * 0 if there's missing information
 * -1 if not found
 * -2 if there are problems while reading the file.
 * any other value corresponds to the amount of cases of the next (sub)test
 */
static int find_next_tap_subtest(int fd, char *record, char *test_name, bool is_builtin)
{
	const char *test_lookup_str, *subtest_lookup_str, *name_rptr;
	long test_count;
	char *cutoff;

	test_name[0] = '\0';
	test_name[BUF_LEN] = '\0';

	test_lookup_str = " subtest: ";
	subtest_lookup_str = " test: ";

	if (!tap_version_present(record, true))
		return -1;

	if (is_builtin) {
		if (read(fd, record, BUF_LEN) < 0) {
			if (errno == EPIPE)
				igt_warn("kmsg truncated: too many messages. You may want to increase log_buf_len in kmcdline\n");
			else
				igt_warn("an error occurred while reading kmsg: %m\n");

			return -2;
		}
	}

	name_rptr = strcasestr(record, test_lookup_str);
	if (name_rptr != NULL) {
		name_rptr += strlen(test_lookup_str);
	} else {
		name_rptr = strcasestr(record, subtest_lookup_str);
		if (name_rptr != NULL)
			name_rptr += strlen(subtest_lookup_str);
	}

	if (name_rptr == NULL) {
		if (!is_builtin)
			/* We've probably found nothing */
			return -1;
		igt_info("Missing test name\n");
	} else {
		strncpy(test_name, name_rptr, BUF_LEN);
		/* Cutoff after newline character, in order to not display garbage */
		cutoff = strchr(test_name, '\n');
		if (cutoff)
			cutoff[0] = '\0';

		if (read(fd, record, BUF_LEN) < 0) {
			if (errno == EPIPE)
				igt_warn("kmsg truncated: too many messages. You may want to increase log_buf_len in kmcdline\n");
			else
				igt_warn("unknown error reading kmsg (%m)\n");

			return -2;
		}

		/* Now we can be sure we found tests */
		if (!is_builtin)
			igt_info("KUnit is not built-in, skipping version check...\n");
	}

	/*
	 * Total test count will almost always appear as 0..N at the beginning
	 * of a run, so we use it to reliably identify a new run
	 */
	test_count = lookup_value(record, "..");

	if (test_count <= 0) {
		igt_info("Missing test count\n");
		if (test_name[0] == '\0')
			return 0;
		if (log_to_end(IGT_LOG_INFO, fd, record,
				"Running some tests in: %s\n",
				test_name) < 0)
			return -2;
		return 0;
	} else if (test_name[0] == '\0') {
		igt_info("Running %ld tests...\n", test_count);
		return 0;
	}

	if (log_to_end(IGT_LOG_INFO, fd, record,
			"Executing %ld tests in: %s\n",
			test_count, test_name) < 0)
		return -2;

	return test_count;
}

/**
 * parse_kmsg_for_tap:
 * @fd: file descriptor
 * @record: buffer used to read fd
 * @test_name: buffer to store the test name
 *
 * Returns:
 * 1 if no results were found
 * 0 if a test succeded
 * -1 if a test failed
 * -2 if there are problems reading the file
 */
static int parse_kmsg_for_tap(int fd, char *record, char *test_name)
{
	const char *lstart, *ok_lookup_str, *nok_lookup_str,
	      *ok_rptr, *nok_rptr, *comment_start, *value_parse_start;
	char *test_name_end;

	ok_lookup_str = "ok ";
	nok_lookup_str = "not ok ";

	lstart = strchrnul(record, ';');

	if (*lstart == '\0') {
		igt_warn("kmsg truncated: output malformed (%m)\n");
		return -2;
	}

	lstart++;
	while (isspace(*lstart))
		lstart++;

	nok_rptr = strstr(lstart, nok_lookup_str);
	if (nok_rptr != NULL) {
		nok_rptr += strlen(nok_lookup_str);
		while (isdigit(*nok_rptr) || isspace(*nok_rptr) || *nok_rptr == '-')
			nok_rptr++;
		test_name_end = strncpy(test_name, nok_rptr, BUF_LEN);
		while (!isspace(*test_name_end))
			test_name_end++;
		*test_name_end = '\0';
		if (log_to_end(IGT_LOG_WARN, fd, record,
			       "%s", lstart) < 0)
			return -2;
		return -1;
	}

	comment_start = strchrnul(lstart, '#');

	/* Check if we're still in a subtest */
	if (*comment_start != '\0') {
		comment_start++;
		value_parse_start = comment_start;

		if (lookup_value(value_parse_start, "fail: ") > 0) {
			if (log_to_end(IGT_LOG_WARN, fd, record,
				       "%s", lstart) < 0)
				return -2;
			return -1;
		}
	}

	ok_rptr = strstr(lstart, ok_lookup_str);
	if (ok_rptr != NULL) {
		ok_rptr += strlen(ok_lookup_str);
		while (isdigit(*ok_rptr) || isspace(*ok_rptr) || *ok_rptr == '-')
			ok_rptr++;
		test_name_end = strncpy(test_name, ok_rptr, BUF_LEN);
		while (!isspace(*test_name_end))
			test_name_end++;
		*test_name_end = '\0';
		return 0;
	}

	return 1;
}

/**
 * parse_tap_level:
 * @fd: file descriptor
 * @base_test_name: test_name from upper recursion level
 * @test_count: test_count of this level
 * @failed_tests: top level failed_tests pointer
 * @found_tests: top level found_tests pointer
 * @is_builtin: whether the KUnit module is built-in or not
 *
 * Returns:
 * 0 if succeded
 * -1 if error occurred
 */
__maybe_unused
static int parse_tap_level(int fd, char *base_test_name, int test_count, bool *failed_tests,
			   bool *found_tests, bool is_builtin)
{
	char record[BUF_LEN + 1];
	struct ktap_test_results_element *r;
	int internal_test_count;
	char test_name[BUF_LEN + 1];
	char base_test_name_for_next_level[BUF_LEN + 1];

	for (int i = 0; i < test_count; i++) {
		if (read(fd, record, BUF_LEN) < 0) {
			if (errno == EPIPE)
				igt_warn("kmsg truncated: too many messages. You may want to increase log_buf_len in kmcdline\n");
			else
				igt_warn("error reading kmsg (%m)\n");

			return -1;
		}

		/* Sublevel found */
		if (tap_version_present(record, false))
		{
			internal_test_count = find_next_tap_subtest(fd, record, test_name,
								    is_builtin);
			switch (internal_test_count) {
			case -2:
				/* No more data to read */
				return -1;
			case -1:
				/* No test found */
				return -1;
			case 0:
				/* Tests found, but they're missing info */
				*found_tests = true;
				return -1;
			default:
				*found_tests = true;

				memcpy(base_test_name_for_next_level, base_test_name, BUF_LEN);
				if (strlen(base_test_name_for_next_level) < BUF_LEN - 1 &&
				    base_test_name_for_next_level[0])
					strncat(base_test_name_for_next_level, DELIMITER,
						BUF_LEN - strlen(base_test_name_for_next_level));
				memcpy(base_test_name_for_next_level + strlen(base_test_name_for_next_level),
				       test_name, BUF_LEN - strlen(base_test_name_for_next_level));

				if (parse_tap_level(fd, base_test_name_for_next_level,
						    internal_test_count, failed_tests, found_tests,
						    is_builtin) == -1)
					return -1;
				break;
			}
		}

		switch (parse_kmsg_for_tap(fd, record, test_name)) {
		case -2:
			return -1;
		case -1:
			*failed_tests = true;

			r = malloc(sizeof(*r));

			memcpy(r->test_name, base_test_name, BUF_LEN);
			if (strlen(r->test_name) < BUF_LEN - 1)
				if (r->test_name[0])
					strncat(r->test_name, DELIMITER,
						BUF_LEN - strlen(r->test_name));
			memcpy(r->test_name + strlen(r->test_name), test_name,
			       BUF_LEN - strlen(r->test_name));
			r->test_name[BUF_LEN] = '\0';

			r->passed = false;

			pthread_mutex_lock(&results.mutex);
			igt_list_add_tail(&r->link, &results.list);
			pthread_mutex_unlock(&results.mutex);

			test_name[0] = '\0';
			break;
		case 0:
			r = malloc(sizeof(*r));

			memcpy(r->test_name, base_test_name, BUF_LEN);
			if (strlen(r->test_name) < BUF_LEN - 1)
				if (r->test_name[0])
					strncat(r->test_name, DELIMITER,
						BUF_LEN - strlen(r->test_name));
			memcpy(r->test_name + strlen(r->test_name), test_name,
			       BUF_LEN - strlen(r->test_name));
			r->test_name[BUF_LEN] = '\0';

			r->passed = true;

			pthread_mutex_lock(&results.mutex);
			igt_list_add_tail(&r->link, &results.list);
			pthread_mutex_unlock(&results.mutex);

			test_name[0] = '\0';
			break;
		default:
			break;
		}
	}
	return 0;
}

/**
 * igt_ktap_parser:
 *
 * This function parses the output of a ktap script and passes it to main thread.
 */
void *igt_ktap_parser(void *unused)
{
	char record[BUF_LEN + 1], *buf, *suite_name = NULL, *case_name = NULL;
	struct igt_ktap_results *ktap = NULL;
	int fd = ktap_args.fd;
	IGT_LIST_HEAD(list);
	int err;

	ktap = igt_ktap_alloc(&list);
	if (igt_debug_on(!ktap))
		goto igt_ktap_parser_end;

	while (err = read(fd, record, BUF_LEN), err > 0) {
		struct igt_ktap_result *r, *rn;

		/* skip kmsg continuation lines */
		if (igt_debug_on(*record == ' '))
			continue;

		/* NULL-terminate the record */
		record[err] = '\0';

		/* detect start of log message, continue if not found */
		buf = strchrnul(record, ';');
		if (igt_debug_on(*buf == '\0'))
			continue;
		buf++;

		err = igt_ktap_parse(buf, ktap);

		/* parsing error */
		if (err && err != -EINPROGRESS)
			goto igt_ktap_parser_end;

		igt_list_for_each_entry_safe(r, rn, &list, link) {
			struct ktap_test_results_element *result = NULL;
			int code = r->code;

			if (code != IGT_EXIT_INVALID)
				result = calloc(1, sizeof(*result));

			if (result) {
				snprintf(result->test_name, sizeof(result->test_name),
					 "%s-%s", r->suite_name, r->case_name);

				if (code == IGT_EXIT_SUCCESS)
					result->passed = true;
			}

			igt_list_del(&r->link);
			if (r->suite_name != suite_name) {
				free(suite_name);
				suite_name = r->suite_name;
			}
			if (r->case_name != case_name) {
				free(case_name);
				case_name = r->case_name;
			}
			free(r->msg);
			free(r);

			/*
			 * no extra result record expected on start
			 * of parametrized test case -- skip it
			 */
			if (code == IGT_EXIT_INVALID)
				continue;

			if (!result) {
				err = -ENOMEM;
				goto igt_ktap_parser_end;
			}

			pthread_mutex_lock(&results.mutex);
			igt_list_add_tail(&result->link, &results.list);
			pthread_mutex_unlock(&results.mutex);
		}

		/* end of KTAP report */
		if (!err)
			goto igt_ktap_parser_end;
	}

	if (err < 0) {
		if (errno == EPIPE)
			igt_warn("kmsg truncated: too many messages. You may want to increase log_buf_len in kmcdline\n");
		else
			igt_warn("error reading kmsg (%m)\n");
	}

igt_ktap_parser_end:
	free(suite_name);
	free(case_name);

	if (!err)
		ktap_args.ret = IGT_EXIT_SUCCESS;

	results.still_running = false;

	if (ktap)
		igt_ktap_free(ktap);

	return NULL;
}

static pthread_t ktap_parser_thread;

struct ktap_test_results *ktap_parser_start(int fd, bool is_builtin)
{
	IGT_INIT_LIST_HEAD(&results.list);
	pthread_mutex_init(&results.mutex, NULL);
	results.still_running = true;

	ktap_args.fd = fd;
	ktap_args.is_builtin = is_builtin;
	ktap_args.ret = IGT_EXIT_FAILURE;
	pthread_create(&ktap_parser_thread, NULL, igt_ktap_parser, NULL);

	return &results;
}

void ktap_parser_cancel(void)
{
	pthread_cancel(ktap_parser_thread);
}

int ktap_parser_stop(void)
{
	pthread_join(ktap_parser_thread, NULL);
	return ktap_args.ret;
}

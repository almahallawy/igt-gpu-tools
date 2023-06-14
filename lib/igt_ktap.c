// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Isabella Basso do Amaral <isabbasso@riseup.net>
 */

#include <ctype.h>
#include <limits.h>

#include "igt_aux.h"
#include "igt_core.h"
#include "igt_ktap.h"

static int log_to_end(enum igt_log_level level, FILE *f,
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
static int log_to_end(enum igt_log_level level, FILE *f,
		      char *record, const char *format, ...)
{
	va_list args;
	const char *lend;

	va_start(args, format);
	igt_vlog(IGT_LOG_DOMAIN, level, format, args);
	va_end(args);

	lend = strchrnul(record, '\n');
	while (*lend == '\0') {
		igt_log(IGT_LOG_DOMAIN, level, "%s", record);
		if (fgets(record, BUF_LEN, f) == NULL) {
			igt_warn("kmsg truncated: unknown error (%m)\n");
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

	/* skip search string and whitespaces after it */
	needle_rptr += strlen(needle);

	num = strtol(needle_rptr, &needle_end, 10);

	if (needle_rptr == needle_end)
		return -1;

	if (num == LONG_MIN || num == LONG_MAX)
		return 0;

	return num > 0 ? num : 0;
}

/**
 * find_next_tap_subtest:
 * @fp: FILE pointer
 * @record: buffer used to read fp
 * @is_builtin: whether KUnit is built-in or not
 *
 * Returns:
 * 0 if there's missing information
 * -1 if not found
 * -2 if there are problems while reading the file.
 * any other value corresponds to the amount of cases of the next (sub)test
 */
static int find_next_tap_subtest(FILE *fp, char *record, bool is_builtin)
{
	const char *test_lookup_str, *subtest_lookup_str, *name_rptr, *version_rptr;
	char test_name[BUF_LEN + 1];
	long test_count;

	test_name[0] = '\0';
	test_name[BUF_LEN] = '\0';

	test_lookup_str = " subtest: ";
	subtest_lookup_str = " test: ";

	/*
	 * "(K)TAP version XX" should be the first line on all (sub)tests as per
	 * https://kernel.org/doc/html/latest/dev-tools/ktap.html#version-lines
	 *
	 * but actually isn't, as it currently depends on the KUnit module
	 * being built-in, so we can't rely on it every time
	 */
	if (is_builtin) {
		version_rptr = strcasestr(record, "TAP version ");
		if (version_rptr == NULL)
			return -1;

		igt_info("%s", version_rptr);

		if (fgets(record, BUF_LEN, fp) == NULL) {
			igt_warn("kmsg truncated: unknown error (%m)\n");
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
			/* we've probably found nothing */
			return -1;
		igt_info("Missing test name\n");
	} else {
		strncpy(test_name, name_rptr, BUF_LEN);
		if (fgets(record, BUF_LEN, fp) == NULL) {
			igt_warn("kmsg truncated: unknown error (%m)\n");
			return -2;
		}
		/* now we can be sure we found tests */
		if (!is_builtin)
			igt_info("KUnit is not built-in, skipping version check...\n");
	}

	/*
	 * total test count will almost always appear as 0..N at the beginning
	 * of a run, so we use it to reliably identify a new run
	 */
	test_count = lookup_value(record, "..");

	if (test_count <= 0) {
		igt_info("Missing test count\n");
		if (test_name[0] == '\0')
			return 0;
		if (log_to_end(IGT_LOG_INFO, fp, record,
				"Running some tests in: %s",
				test_name) < 0)
			return -2;
		return 0;
	} else if (test_name[0] == '\0') {
		igt_info("Running %ld tests...\n", test_count);
		return 0;
	}

	if (log_to_end(IGT_LOG_INFO, fp, record,
			"Executing %ld tests in: %s",
			test_count, test_name) < 0)
		return -2;

	return test_count;
}

/**
 * find_next_tap_test:
 * @fp: FILE pointer
 * @record: buffer used to read fp
 * @test_name: buffer to store the test name
 *
 * Returns:
 * 1 if no results were found
 * 0 if a test succeded
 * -1 if a test failed
 * -2 if there are problems reading the file
 */
static int parse_kmsg_for_tap(FILE *fp, char *record, char *test_name)
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
		if (log_to_end(IGT_LOG_WARN, fp, record,
			       "%s", lstart) < 0)
			return -2;
		return -1;
	}

	comment_start = strchrnul(lstart, '#');

	/* check if we're still in a subtest */
	if (*comment_start != '\0') {
		comment_start++;
		value_parse_start = comment_start;

		if (lookup_value(value_parse_start, "fail: ") > 0) {
			if (log_to_end(IGT_LOG_WARN, fp, record,
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
 * igt_ktap_parser:
 * @fp: FILE pointer
 * @record: buffer used to read fp
 * @is_builtin: whether the KUnit module is built-in or not
 *
 * This function parses the output of a ktap script and prints the test results,
 * as well as any other output to stdout.
 *
 * Returns: IGT default codes
 */
int igt_ktap_parser(FILE *fp, char *record, bool is_builtin)
{
	char test_name[BUF_LEN + 1];
	bool failed_tests, found_tests;
	int sublevel = 0;

	test_name[0] = '\0';
	test_name[BUF_LEN] = '\0';

	failed_tests = false;
	found_tests = false;

	while (sublevel >= 0) {
		if (fgets(record, BUF_LEN, fp) == NULL) {
			if (!found_tests)
				igt_warn("kmsg truncated: unknown error (%m)\n");
			break;
		}

		switch (find_next_tap_subtest(fp, record, is_builtin)) {
		case -2:
			/* no more data to read */
			return IGT_EXIT_FAILURE;
		case -1:
			/* no test found, so we keep parsing */
			break;
		case 0:
			/*
			 * tests found, but they're missing info, so we might
			 * have read into test output
			 */
			found_tests = true;
			sublevel++;
			break;
		default:
			if (fgets(record, BUF_LEN, fp) == NULL) {
				igt_warn("kmsg truncated: unknown error (%m)\n");
				return -2;
			}
			found_tests = true;
			sublevel++;
			break;
		}

		switch (parse_kmsg_for_tap(fp, record, test_name)) {
		case -2:
			return IGT_EXIT_FAILURE;
		case -1:
			sublevel--;
			failed_tests = true;
			igt_subtest(test_name)
				igt_fail(IGT_EXIT_FAILURE);
			test_name[0] = '\0';
			break;
		case 0: /* fallthrough */
			igt_subtest(test_name)
				igt_success();
			test_name[0] = '\0';
		default:
			break;
		}
	}

	if (failed_tests || !found_tests)
		return IGT_EXIT_FAILURE;

	return IGT_EXIT_SUCCESS;
}

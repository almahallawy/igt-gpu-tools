// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Isabella Basso do Amaral <isabbasso@riseup.net>
 * Copyright © 2023 Intel Corporation
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
 * https://kernel.org/doc/html/latest/dev-tools/ktap.html
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
		 * TODO: drop the following workaround, which addresses a kernel
		 * side issue of missing lines that provide top level KTAP
		 * version and test suite plan, as soon as no longer needed.
		 *
		 * The issue has been fixed in v6.6-rc1, commit c95e7c05c139
		 * ("kunit: Report the count of test suites in a module"),
		 * but we still need this workaround for as long as LTS kernel
		 * version 6.1, with DRM selftests already converted to Kunit,
		 * but without that missing Kunit headers issue fixed, is used
		 * by major Linux distributions.
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

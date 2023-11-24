#!/usr/bin/env python3
# pylint: disable=C0301,C0201,C0206
# SPDX-License-Identifier: (GPL-2.0 OR MIT)

## Copyright (C) 2023    Intel Corporation                 ##
## Author: Mauro Carvalho Chehab <mchehab@kernel.org>      ##
##                                                         ##
## Allow keeping inlined test documentation and validate   ##
## if the documentation is kept updated.                   ##

"""Maintain test plan and test implementation documentation on IGT."""

import argparse
import os
import re
import sys

from test_list import TestList

class IgtTestList(TestList):
    """
        This class implements testlist generation as expected by Intel CI.
        It does that by handling test lists split by "Run type" and
        using GPU (or configuration) specific fields, being "GPU" for a
        permit list of tests, and "GPU excluded platform" for a block
        list of tests.

        The logic below has "priority zero" rules, which are:

        - if the test is not on any block lists nor it contains
          "GPU" or "GPU excluded platform", it won't be blocked;
        - if the test is in "all" block list, it will be blocked for all
          GPUs. Values from "GPU" and "GPU excluded platform" will be ignored.

        If none of the above rules apply, it will handle GPU positive
        and negative rules:

        - if "GPU" field is present on such test, the default is
          is to block the test (default_gpu_value = False). If not
          present, the default is to not block (default_gpu_value = True).

        Now, it will check for "GPU" and "GPU excluded platform":

        - it sets the default according to default_gpu_value.

        Then:

        - if "GPU" exists, for each GPU listed on the list, it will
          unblock the test;
        - if "GPU excluded platform" exists, for each GPU listed on
          the list, it will block the test.
    """
    def gen_intelci_testlist(self): #pylint: disable=R0912
        """Return a list of gpu configs and testlists."""

        subtest_dict = self.expand_dictionary(True)

        # Create a tests_per_list dict
        gpus = set()
        tests_per_list = {}
        split_regex = re.compile(r",\s*")

        for subname, subtest in subtest_dict.items():
            run_types = subtest.get("Run type", "other")
            run_type_set = set(split_regex.split(run_types))

            run_type_set = set()
            for run_type in set(split_regex.split(run_types)):
                run_type = run_type.lower()

                if run_type.startswith(tuple(self.drivers)):
                    run_type_set.add(run_type)
                else:
                    for driver in self.drivers:
                        run_type = f"{driver.lower()}_{run_type}"
                        run_type_set.add(run_type)

            for run_type in run_type_set:
                if run_type not in tests_per_list:
                    tests_per_list[run_type] = {}

                if subname not in tests_per_list[run_type]:
                    tests_per_list[run_type][subname] = {}

                if "GPU" in subtest:
                    for gpu_list in split_regex.split(subtest["GPU"]):
                        gpus.add(gpu_list)
                        tests_per_list[run_type][subname][gpu_list] = True

                if "GPU excluded platform" in subtest:
                    for gpu_list in split_regex.split(subtest["GPU excluded platform"]):
                        gpus.add(gpu_list)
                        tests_per_list[run_type][subname][gpu_list] = False

        # Handle block and permit lists

        for run_type in tests_per_list.keys():
            for subname, gpu in tests_per_list[run_type].items():

                # Trivial case: fields not defined
                if not gpus:
                    tests_per_list[run_type][subname]["default"] = True
                    continue

                if not gpu:
                    tests_per_list[run_type][subname] = {}
                    for gpu in gpus:
                        tests_per_list[run_type][subname][gpu] = True
                    continue

                default_gpu_value = True
                for gpu, value in tests_per_list[run_type][subname].items():
                    if value:
                        default_gpu_value = False
                        break
                    if not gpu in tests_per_list[run_type][subname]:
                        for gpu in gpus:
                            tests_per_list[run_type][subname][gpu] = default_gpu_value

                if "all" in tests_per_list[run_type][subname]:
                    if not tests_per_list[run_type][subname]["all"]:
                        for gpu in gpus:
                            tests_per_list[run_type][subname][gpu] = False

                if default_gpu_value:
                    tests_per_list[run_type][subname]["default"] = True

        return (gpus, tests_per_list)

    def write_intelci_testlist(self, directory):
        '''Create testlist directory (if needed) and files'''

        if not os.path.exists(directory):
            os.makedirs(directory)

        (gpus, tests_per_list)  = self.gen_intelci_testlist()
        testlists = {}

        for run_type in tests_per_list.keys():
            for subname, gpu_dict in tests_per_list[run_type].items():
                for gpu, value in gpu_dict.items():
                    if gpu == "all":
                        continue

                    gpu = re.sub(r"[\W_]+", "-", gpu).lower()
                    name = re.sub(r"[\W_]+", "-", run_type).lower()
                    name = re.sub(r"_+", "_", name)

                    drivers = set(self.drivers)

                    for driver in self.drivers:
                        driver = driver.lower()

                        if name.startswith(driver):
                            all_drivers = False
                            name = re.sub(r"^" + driver + r"[\W_]*", "", name)
                            drivers = set([driver])
                            break

                    for driver in drivers:
                        if driver not in testlists:
                            testlists[driver] = {}

                        if gpu not in testlists[driver]:
                            testlists[driver][gpu] = {}

                        if name not in testlists[driver][gpu]:
                            testlists[driver][gpu][name] = set()

                        testlists[driver][gpu][name].add(subname)

        for driver in testlists.keys():
            driver_path = os.path.join(directory, driver)
            try:
                os.makedirs(driver_path)
            except FileExistsError:
                pass

            for gpu, names in testlists[driver].items():
                dname = os.path.join(driver_path, gpu)
                try:
                    os.makedirs(dname)
                except FileExistsError:
                    pass

                for testlist, subtests in names.items():
                    if testlist == "":
                        testlist = "other"

                    if not subtests:
                        print(f"Warning: empty testlist: {testlist}")
                        continue

                    fname = os.path.join(dname, testlist) + ".testlist"
                    with open(fname, 'w', encoding='utf8') as handler:
                        for sub in sorted(subtests):
                            handler.write (f"{sub}\n")
                        print(f"{fname} created.")

def main():
    """
    Main logic
    """

    igt_build_path = 'build'

    parser = argparse.ArgumentParser(description = "Print formatted kernel documentation to stdout.",
                                    formatter_class = argparse.ArgumentDefaultsHelpFormatter,
                                    epilog = 'If no action specified, assume --rest.')
    parser.add_argument("--config", required = True,
                        help="JSON file describing the test plan template")
    parser.add_argument("--rest",
                        help="Output documentation from the source files in REST file.")
    parser.add_argument("--per-test", action="store_true",
                        help="Modifies ReST output to print subtests per test.")
    parser.add_argument("--to-json",
                        help="Output test documentation in JSON format as TO_JSON file")
    parser.add_argument("--show-subtests", action="store_true",
                        help="Shows the name of the documented subtests in alphabetical order.")
    parser.add_argument("--sort-field",
                        help="modify --show-subtests to sort output based on SORT_FIELD value")
    parser.add_argument("--filter-field", nargs='*',
                        help="filter subtests based on regular expressions given by FILTER_FIELD=~'regex'")
    parser.add_argument("--check-testlist", action="store_true",
                        help="Compare documentation against IGT built tests.")
    parser.add_argument("--include-plan", action="store_true",
                        help="Include test plans, if any.")
    parser.add_argument("--igt-build-path",
                        help="Path to the IGT build directory. Used by --check-testlist.",
                        default=igt_build_path)
    parser.add_argument("--gen-testlist",
                        help="Generate documentation at the GEN_TESTLIST directory, using SORT_FIELD to split the tests. Requires --sort-field.")
    parser.add_argument("--intelci-testlist",
                        help="Generate testlists for Intel CI integration at the INTELCI_TESTLIST directory.")
    parser.add_argument('--files', nargs='+',
                        help="File name(s) to be processed")

    parse_args = parser.parse_args()

    tests = IgtTestList(config_fname = parse_args.config,
                        include_plan = parse_args.include_plan,
                        file_list = parse_args.files,
                        igt_build_path = parse_args.igt_build_path)

    if parse_args.filter_field:
        for filter_expr in parse_args.filter_field:
            tests.add_filter(filter_expr)

    run = False
    if parse_args.show_subtests:
        run = True
        tests.show_subtests(parse_args.sort_field)

    if parse_args.check_testlist:
        run = True
        tests.check_tests()

    if parse_args.gen_testlist:
        run = True
        if not parse_args.sort_field:
            sys.exit("Need a field to split the testlists")
        tests.gen_testlist(parse_args.gen_testlist, parse_args.sort_field)

    if parse_args.intelci_testlist:
        run = True
        tests.write_intelci_testlist(parse_args.intelci_testlist)

    if parse_args.to_json:
        run = True
        tests.print_json(parse_args.to_json)

    if not run or parse_args.rest:
        if parse_args.per_test:
            tests.print_rest_flat(parse_args.rest)
        else:
            tests.print_nested_rest(parse_args.rest)

if __name__ == '__main__':
    main()

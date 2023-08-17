IGT Test documentation
======================

Legacy way
----------

IGT has been providing a way to document tests in runtime by using tree macros:

    - IGT_TEST_DESCRIPTION(test_file_description)
    - igt_describe(subtest_description) and igt_describe_f(format, args...)

This is limited, as:
    - Each test/subtest has just one “field”: description. It doesn't
      allow specifying what features are tested and/or special requirements;
    - It is meant to produce a very concise documentation;
    - Integration with external platforms to group tests per category
      is not possible, as there's no way to tell what category each test
      belongs;
    - Format is not easily expansible;
    - The build system doesn’t verify if all tests are documented.
      As time passes, documentation tends to be outdated or forgotten,
      as new patches modify the test set.

Documentation via structured comments (testplan)
------------------------------------------------

With the addition of Xe driver, a new way to document tests was added.
It is based on special comment-like annotation inside the C code.

It was written to be flexible, so the valid fields to be used by are
described on a JSON configuration file. That makes easy to add/modify
the fields as needed. It is also easy to use the documentation to
integrate with external reporting tools.

As an additional benefit, the documentation tags will be generating a
Restructured Text file. It is possible to add enriched test descriptions
if needed.

The build system can also optionally enforce a check at build time to
verify if the documentation is in place.

testplan configuration file
---------------------------

The configuration file contains the fields to be used for documenting
tests and test names. It may also mark a property as mandatory.

A typical example is:

```
{
    "description": "JSON example file",
    "name": "Tests for XYZ Driver",
    "files": [ "test.c" ],
    "fields": {
        "Feature": {
            "_properties_": {
                "description": "Feature to be tested"
            }
        },
        "Description" : {
            "_properties_": {
                "mandatory": true,
                "description": "Provides a description for the test/subtest."
            }
        }
    }
}
```

Documenting tests via testplan
------------------------------

A typical documentation markup at the test source code looks like:
```
/**
 * TEST: Check if new IGT test documentation logic functionality is working
 * Mega-feature: IGT test documentation
 * Category: Software build block
 * Sub-category: documentation
 * Functionality: test documentation
 * Issue: none
 * Description: Complete description of this test
 *
 * SUBTEST: foo
 * Description: do foo things.
 *      Foo description continuing on another line
 *
 * SUBTEST: bar
 * Description:
 *      Do bar things.
 *      Bar description continuing on another line
 */
```

It is also possible to add variables to the documentation with:

```
/**
 * SUBTEST: test-%s-binds-with-%ld-size-%s
 * Description: Test arg[3] arg[1] binds with arg[2] size
 *
 * SUBTEST: test-%s-%ld-size
 * Description: Test arg[1] with %arg[2] size
 *
 * arg[1]:
 *
 * @large:      large
 *              something
 * @small:      small
 *              something
 *
 * arg[2]:      buffer size
 *
 * arg[3]:
 *
 * @misaligned-binds:           misaligned
 * @userptr-binds:              user pointer
 */
 ```

The first "%s" will be replaced by the values of arg[1] for the subtest
name. At the description, arg[1] will be replaced by the string after
the field description. The same applies to the subsequent wildcards.

The above will produce the following output (using the example
configuration file described at the past session:

```
``igt@test@test-large-<buffer size>-size``

:Description: Test large something with <buffer size> size


``igt@test@test-large-binds-with-<buffer size>-size-misaligned-binds``

:Description: Test misaligned large something binds with <buffer size> size


``igt@test@test-small-binds-with-<buffer size>-size-misaligned-binds``

:Description: Test misaligned small something binds with <buffer size> size


``igt@test@test-small-<buffer size>-size``

:Description: Test small something with <buffer size> size


``igt@test@test-large-binds-with-<buffer size>-size-userptr-binds``

:Description: Test user pointer large something binds with <buffer size> size


``igt@test@test-small-binds-with-<buffer size>-size-userptr-binds``

:Description: Test user pointer small something binds with <buffer size> size
```

Wildcard replacement can also be used to maintain an argument with the
same name at the replaced description. That makes easy to document
numeric arguments with a fixed testset, like:

```
/**
 * SUBTEST: unbind-all-%d-vmas
 * Description: unbind all with %arg[1] VMAs
 *
 * arg[1].values: 2, 8
 * arg[1].values: 16, 32
 */

/**
 * SUBTEST: unbind-all-%d-vmas
 * Description: unbind all with %arg[1] VMAs
 *
 * arg[1].values: 64, 128
 */
```

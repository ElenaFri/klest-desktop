# Klest tests (ADR 0006).
#
# Unit tests exercise the `klest-core` library directly. Each test is a small
# executable that returns non-zero on failure; `meson test` runs them all.

test_version = executable(
    'test-version',
    'test_version.c',
    dependencies: klest_core_dep,
)
test('version', test_version)

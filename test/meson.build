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

test_config = executable(
    'test-config',
    'test_config.c',
    dependencies: klest_config_dep,
)
test('config', test_config)

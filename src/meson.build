# Klest sources.
#
# Kept minimal for now: a single executable that proves the toolchain works.
# As the modular architecture takes shape (ADR 0009), this will split into
# the compositor, services, and shared code.

executable(
    'klest',
    'main.c',
    c_args: ['-DKLEST_VERSION="' + meson.project_version() + '"'],
    install: true,
)

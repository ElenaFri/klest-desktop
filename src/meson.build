# Klest sources.
#
# Kept minimal for now. Testable logic lives in the `klest-core` library so it
# can be exercised by unit tests; `main.c` stays a thin entry point. As the
# modular architecture takes shape (ADR 0009), this will split into the
# compositor, services, and shared code.

klest_core = static_library(
    'klest-core',
    'version.c',
    include_directories: klest_inc,
)

klest_core_dep = declare_dependency(
    link_with: klest_core,
    include_directories: include_directories('.'),
)

executable(
    'klest',
    'main.c',
    dependencies: klest_core_dep,
    install: true,
)

# Vendored font8x8 — a public-domain 8x8 bitmap font (see README.md).
#
# Built as an isolated static library with warnings silenced so Klest's strict
# -Werror never trips on third-party data (same pattern as tomlc17).

font8x8_lib = static_library(
    'font8x8',
    'font8x8_basic.c',
    override_options: ['werror=false', 'warning_level=0'],
)

font8x8_dep = declare_dependency(
    link_with: font8x8_lib,
    include_directories: include_directories('.'),
)

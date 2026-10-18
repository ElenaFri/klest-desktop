# Vendored tomlc17 — a TOML v1.0 parser in C (ADR 0007).
#
# Pinned to upstream release R20260515 (see README.md for provenance). Built as
# an isolated static library with warnings silenced (-w) and werror disabled so
# Klest's own strict -Werror never trips on third-party code.

m_dep = cc.find_library('m', required: false)

tomlc17_lib = static_library(
    'tomlc17',
    'tomlc17.c',
    c_args: ['-w', '-DTOML_VERSION_STRING="R20260515"'],
    override_options: ['werror=false', 'warning_level=0'],
    dependencies: m_dep,
)

tomlc17_dep = declare_dependency(
    link_with: tomlc17_lib,
    include_directories: include_directories('.'),
    dependencies: m_dep,
)

project(
    'klest',
    'c',
    version: '0.0.0',
    license: 'GPL-3.0-or-later',
    meson_version: '>= 0.60.0',
    default_options: [
        'c_std=c17',
        'warning_level=3',
        'werror=true',
        'buildtype=debugoptimized',
    ],
)

# Strict, portable warnings on top of warning_level=3 (ADR 0001, 0006).
cc = meson.get_compiler('c')
add_project_arguments(
    cc.get_supported_arguments(
        '-Wshadow',
        '-Wpointer-arith',
        '-Wstrict-prototypes',
        '-Wmissing-prototypes',
        '-Wvla',
    ),
    language: 'c',
)

subdir('src')

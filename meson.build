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

# Compile-time configuration (version, ...), exposed as <config.h>.
conf_data = configuration_data()
conf_data.set_quoted('KLEST_VERSION', meson.project_version())
configure_file(output: 'config.h', configuration: conf_data)

# include_directories('.') exposes both the source and build roots, so
# generated headers like config.h are found alongside hand-written ones.
klest_inc = include_directories('.')

subdir('src')
subdir('test')

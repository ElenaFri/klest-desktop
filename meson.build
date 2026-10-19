project(
    'klest',
    'c',
    version: '0.0.0',
    license: 'GPL-3.0-or-later',
    meson_version: '>= 0.60.0',
    default_options: [
        'c_std=c17',
        'warning_level=2',
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

# Optional compositor dependencies (ADR 0002, 0004). When absent, the base
# build and tests still work; only the compositor is skipped.
wlroots_dep = dependency('wlroots-0.18', required: get_option('compositor'))
wayland_server_dep = dependency('wayland-server', required: get_option('compositor'))
wayland_client_dep = dependency('wayland-client', required: false)
wayland_protocols_dep = dependency('wayland-protocols', required: get_option('compositor'))
xkbcommon_dep = dependency('xkbcommon', required: get_option('compositor'))
wayland_scanner = find_program('wayland-scanner', required: get_option('compositor'))
have_compositor = wlroots_dep.found() and wayland_server_dep.found() \
    and wayland_protocols_dep.found() and xkbcommon_dep.found() \
    and wayland_scanner.found()

# wlr-layer-shell is a wlroots protocol, not shipped by wayland-protocols, so we
# vendor its XML and generate the server/client glue ourselves (ADR 0009).
wlr_layer_shell_xml = files('protocol/wlr-layer-shell-unstable-v1.xml')
klest_workspace_xml = files('protocol/klest-workspace-v1.xml')

# Optional GTK configuration tool (ADR 0005). GTK is only ever linked into the
# tools, never the compositor, keeping the core lean.
gtk4_dep = dependency('gtk4', required: get_option('settings'))
have_settings = gtk4_dep.found()

# Compile-time configuration (version, feature flags), exposed as <config.h>.
conf_data = configuration_data()
conf_data.set_quoted('KLEST_VERSION', meson.project_version())
conf_data.set10('KLEST_HAVE_COMPOSITOR', have_compositor)
configure_file(output: 'config.h', configuration: conf_data)

# include_directories('.') exposes both the source and build roots, so
# generated headers like config.h are found alongside hand-written ones.
klest_inc = include_directories('.')

# Vendored third-party code (built in isolation; see ADR 0007).
subdir('third_party/tomlc17')
subdir('third_party/font8x8')

subdir('src')
subdir('test')

summary({'compositor': have_compositor}, section: 'Features')
summary({'settings (GTK tool)': have_settings}, section: 'Features')

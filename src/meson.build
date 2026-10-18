# Klest sources.
#
# Testable logic lives in `klest-core`; the compositor lives in its own library
# so it can be built and replaced independently (ADR 0009). `main.c` stays a
# thin entry point.

klest_core = static_library(
    'klest-core',
    'version.c',
    include_directories: klest_inc,
)

klest_core_dep = declare_dependency(
    link_with: klest_core,
    include_directories: include_directories('.'),
)

klest_deps = [klest_core_dep]

if have_compositor
    # wlroots' xdg-shell headers need the generated protocol header.
    wl_protocol_dir = wayland_protocols_dep.get_variable('pkgdatadir')
    xdg_shell_header = custom_target(
        'xdg-shell-protocol.h',
        input: wl_protocol_dir / 'stable/xdg-shell/xdg-shell.xml',
        output: 'xdg-shell-protocol.h',
        command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
    )

    klest_compositor = static_library(
        'klest-compositor',
        'compositor/server.c',
        'compositor/view.c',
        'compositor/input.c',
        xdg_shell_header,
        include_directories: [klest_inc, include_directories('.')],
        c_args: ['-DWLR_USE_UNSTABLE'],
        dependencies: [wlroots_dep, wayland_server_dep, xkbcommon_dep],
    )
    klest_compositor_dep = declare_dependency(
        link_with: klest_compositor,
        dependencies: [wlroots_dep, wayland_server_dep, xkbcommon_dep],
    )
    klest_deps += klest_compositor_dep
endif

executable(
    'klest',
    'main.c',
    include_directories: klest_inc,
    dependencies: klest_deps,
    install: true,
)

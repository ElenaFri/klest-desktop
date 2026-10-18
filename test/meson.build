# Klest tests (ADR 0006).
#
# Unit and headless compositor integration tests. Each test is a small
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

if have_compositor and wayland_client_dep.found()
    xdg_shell_xml = wayland_protocols_dep.get_variable('pkgdatadir') / 'stable/xdg-shell/xdg-shell.xml'
    xdg_shell_client_header = custom_target(
        'xdg-shell-client-protocol.h',
        input: xdg_shell_xml,
        output: 'xdg-shell-client-protocol.h',
        command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
    )
    xdg_shell_client_code = custom_target(
        'xdg-shell-client-protocol.c',
        input: xdg_shell_xml,
        output: 'xdg-shell-client-protocol.c',
        command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
    )

    test_compositor_client = executable(
        'test-compositor-client',
        'test_compositor_client.c',
        xdg_shell_client_header,
        xdg_shell_client_code,
        dependencies: wayland_client_dep,
    )
    test_compositor = executable('test-compositor', 'test_compositor.c')
    test('compositor-integration', test_compositor, args: [
        meson.project_build_root() / 'src/klest',
        test_compositor_client.full_path(),
        meson.current_build_dir(),
    ], timeout: 30)

    # Layer-shell client glue. The generated code references xdg_popup, so the
    # xdg-shell client code is linked in alongside it.
    layer_shell_client_header = custom_target(
        'wlr-layer-shell-unstable-v1-client-protocol.h',
        input: wlr_layer_shell_xml,
        output: 'wlr-layer-shell-unstable-v1-client-protocol.h',
        command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
    )
    layer_shell_client_code = custom_target(
        'wlr-layer-shell-unstable-v1-protocol.c',
        input: wlr_layer_shell_xml,
        output: 'wlr-layer-shell-unstable-v1-protocol.c',
        command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
    )

    test_layer_client = executable(
        'test-layer-client',
        'test_layer_client.c',
        layer_shell_client_header,
        layer_shell_client_code,
        xdg_shell_client_header,
        xdg_shell_client_code,
        dependencies: wayland_client_dep,
    )
    test('layer-shell-integration', test_compositor, args: [
        meson.project_build_root() / 'src/klest',
        test_layer_client.full_path(),
        meson.current_build_dir(),
    ], timeout: 30)
endif

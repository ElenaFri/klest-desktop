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
    test_bindings = executable(
        'test-bindings',
        'test_bindings.c',
        '../src/compositor/bindings.c',
        include_directories: include_directories('../src'),
        dependencies: xkbcommon_dep,
    )
    test('bindings', test_bindings)

    test_window_placement = executable(
        'test-window-placement',
        'test_window_placement.c',
        '../src/compositor/placement.c',
        include_directories: include_directories('../src'),
        dependencies: wlroots_dep,
    )
    test('window-placement', test_window_placement)

    test_panel_layout = executable(
        'test-panel-layout',
        'test_panel_layout.c',
        '../src/components/klest-panel-layout.c',
        include_directories: include_directories('../src'),
    )
    test('panel-layout', test_panel_layout)

    test_launcher_filter = executable(
        'test-launcher-filter',
        'test_launcher_filter.c',
        '../src/components/klest-launcher-filter.c',
        include_directories: include_directories('../src'),
        dependencies: xkbcommon_dep,
    )
    test('launcher-filter', test_launcher_filter)

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
    	workspace_client_header = custom_target(
    		'klest-workspace-v1-client-protocol.h',
    		input: klest_workspace_xml,
    		output: 'klest-workspace-v1-client-protocol.h',
    		command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
    	)
    	workspace_client_code = custom_target(
    		'klest-workspace-v1-client-protocol.c',
    		input: klest_workspace_xml,
    		output: 'klest-workspace-v1-client-protocol.c',
    		command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
    	)

    	test_compositor_client = executable(
        'test-compositor-client',
        'test_compositor_client.c',
        		'../src/wayland/shm.c',
        		xdg_shell_client_header,
        		xdg_shell_client_code,
        		workspace_client_header,
        		workspace_client_code,
        include_directories: include_directories('../src'),
        dependencies: wayland_client_dep,
    )
    test_compositor = executable('test-compositor', 'test_compositor.c')
    test('compositor-integration', test_compositor, args: [
        meson.project_build_root() / 'src/klest',
        test_compositor_client.full_path(),
        meson.current_build_dir(),
    ], timeout: 30)

    test_autostart = executable('test-autostart', 'test_autostart.c')
    test('autostart-integration', test_autostart, args: [
        meson.project_build_root() / 'src/klest',
        meson.current_build_dir(),
    ], timeout: 30)

    virtual_keyboard_xml = files('../protocol/virtual-keyboard-unstable-v1.xml')
    virtual_keyboard_header = custom_target(
        'virtual-keyboard-unstable-v1-client-protocol.h',
        input: virtual_keyboard_xml,
        output: 'virtual-keyboard-unstable-v1-client-protocol.h',
        command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
    )
    virtual_keyboard_code = custom_target(
        'virtual-keyboard-unstable-v1-protocol.c',
        input: virtual_keyboard_xml,
        output: 'virtual-keyboard-unstable-v1-protocol.c',
        command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
    )
    virtual_pointer_xml = files('../protocol/wlr-virtual-pointer-unstable-v1.xml')
    virtual_pointer_header = custom_target(
        'wlr-virtual-pointer-unstable-v1-client-protocol.h',
        input: virtual_pointer_xml,
        output: 'wlr-virtual-pointer-unstable-v1-client-protocol.h',
        command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
    )
    virtual_pointer_code = custom_target(
        'wlr-virtual-pointer-unstable-v1-protocol.c',
        input: virtual_pointer_xml,
        output: 'wlr-virtual-pointer-unstable-v1-protocol.c',
        command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
    )

    test_virtual_input = executable(
        'test-virtual-input',
        'test_virtual_input.c',
        '../src/wayland/shm.c',
        xdg_shell_client_header,
        xdg_shell_client_code,
        workspace_client_header,
        workspace_client_code,
        virtual_keyboard_header,
        virtual_keyboard_code,
        virtual_pointer_header,
        virtual_pointer_code,
        include_directories: include_directories('../src'),
        dependencies: [wayland_client_dep, xkbcommon_dep],
    )
    test('virtual-input-integration', test_virtual_input, args: [
        meson.project_build_root() / 'src/klest',
        meson.current_build_dir(),
    ], env: [
        'KLEST_PANEL_TEST_EXECUTABLE=' +
            (meson.project_build_root() / 'src/klest-panel'),
        'KLEST_LAUNCHER_TEST_EXECUTABLE=' +
            (meson.project_build_root() / 'src/klest-launcher'),
        'KLEST_BACKGROUND_TEST_EXECUTABLE=' +
            (meson.project_build_root() / 'src/klest-background'),
    ], timeout: 60)


    # Command-line contract and no-compositor behaviour of the components.
    component_cli_args = [
        meson.project_build_root() / 'src/klest',
        meson.project_build_root() / 'src/klest-panel',
        meson.project_build_root() / 'src/klest-launcher',
        meson.project_build_root() / 'src/klest-background',
    ]
    if have_notify
        component_cli_args += meson.project_build_root() / 'src/klest-notify'
    endif
    test_component_cli = executable('test-component-cli', 'test_component_cli.c')
    test('component-cli', test_component_cli, args: component_cli_args,
        timeout: 30)

    test('background-integration', test_compositor, args: [
        meson.project_build_root() / 'src/klest',
        meson.project_build_root() / 'src/klest-background',
        meson.current_build_dir(),
        '--once',
    ], timeout: 30)

    test('panel-integration', test_compositor, args: [
        meson.project_build_root() / 'src/klest',
        meson.project_build_root() / 'src/klest-panel',
        meson.current_build_dir(),
        '--once',
    ], timeout: 30)

    test('launcher-integration', test_compositor, args: [
        meson.project_build_root() / 'src/klest',
        meson.project_build_root() / 'src/klest-launcher',
        meson.current_build_dir(),
        '--once',
    ], timeout: 30)

    if have_notify
        test('notify-integration', test_compositor, args: [
            meson.project_build_root() / 'src/klest',
            meson.project_build_root() / 'src/klest-notify',
            meson.current_build_dir(),
            '--once',
        ], timeout: 30)

        dbus_run_session = find_program('dbus-run-session')
        test_notify_dbus = executable(
            'test-notify-dbus',
            'test_notify_dbus.c',
            dependencies: sdbus_dep,
        )
        test('notify-dbus-integration', dbus_run_session, args: [
            '--',
            test_compositor.full_path(),
            meson.project_build_root() / 'src/klest',
            test_notify_dbus.full_path(),
            meson.current_build_dir(),
        ], env: [
            'KLEST_NOTIFY_TEST_EXECUTABLE=' +
                (meson.project_build_root() / 'src/klest-notify'),
        ], timeout: 30)
    endif
endif

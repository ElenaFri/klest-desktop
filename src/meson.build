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

# Configuration loading (ADR 0007). Always built; it only needs the vendored
# TOML parser, no system dependencies.
klest_config_lib = static_library(
    'klest-config',
    'config/config.c',
    include_directories: [klest_inc, include_directories('.')],
    dependencies: tomlc17_dep,
)

klest_config_dep = declare_dependency(
    link_with: klest_config_lib,
    include_directories: include_directories('.'),
)

klest_deps = [klest_core_dep, klest_config_dep]

if have_compositor
    # wlroots' xdg-shell headers need the generated protocol header.
    wl_protocol_dir = wayland_protocols_dep.get_variable('pkgdatadir')
    xdg_shell_header = custom_target(
        'xdg-shell-protocol.h',
        input: wl_protocol_dir / 'stable/xdg-shell/xdg-shell.xml',
        output: 'xdg-shell-protocol.h',
        command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
    )

    # wlroots' layer-shell header includes this generated protocol header.
    	layer_shell_header = custom_target(
    		'wlr-layer-shell-unstable-v1-protocol.h',
    		input: wlr_layer_shell_xml,
    		output: 'wlr-layer-shell-unstable-v1-protocol.h',
    		command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
    	)
    	workspace_server_header = custom_target(
    		'klest-workspace-v1-server-protocol.h',
    		input: klest_workspace_xml,
    		output: 'klest-workspace-v1-server-protocol.h',
    		command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
    	)
    	workspace_server_code = custom_target(
    		'klest-workspace-v1-server-protocol.c',
    		input: klest_workspace_xml,
    		output: 'klest-workspace-v1-server-protocol.c',
    		command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
    	)

    	klest_compositor = static_library(
        'klest-compositor',
        'compositor/server.c',
        'compositor/view.c',
        'compositor/input.c',
        'compositor/startup.c',
        		'compositor/layer.c',
        		'compositor/workspace.c',
        		xdg_shell_header,
        		layer_shell_header,
        		workspace_server_header,
        		workspace_server_code,
        include_directories: [klest_inc, include_directories('.')],
        c_args: ['-DWLR_USE_UNSTABLE'],
        dependencies: [wlroots_dep, wayland_server_dep, xkbcommon_dep,
            klest_config_dep],
    )
    klest_compositor_dep = declare_dependency(
        link_with: klest_compositor,
        dependencies: [wlroots_dep, wayland_server_dep, xkbcommon_dep],
    )
    klest_deps += klest_compositor_dep

    if wayland_client_dep.found()
        layer_shell_client_header = custom_target(
            'wlr-layer-shell-unstable-v1-client-protocol.h',
            input: wlr_layer_shell_xml,
            output: 'wlr-layer-shell-unstable-v1-client-protocol.h',
            command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
        )
        layer_shell_client_code = custom_target(
            'wlr-layer-shell-unstable-v1-client-protocol.c',
            input: wlr_layer_shell_xml,
            output: 'wlr-layer-shell-unstable-v1-client-protocol.c',
            command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
        )
        		xdg_shell_client_code = custom_target(
        			'wayland-xdg-shell-client-protocol.c',
        			input: wl_protocol_dir / 'stable/xdg-shell/xdg-shell.xml',
        			output: 'wayland-xdg-shell-client-protocol.c',
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

        		klest_wayland_client = static_library(
            'klest-wayland-client',
            'wayland/layer_client.c',
            'wayland/shm.c',
            'wayland/font.c',
            			layer_shell_client_header,
            			layer_shell_client_code,
            			xdg_shell_client_code,
            			workspace_client_header,
            			workspace_client_code,
            include_directories: [klest_inc, include_directories('.')],
            dependencies: [wayland_client_dep, font8x8_dep],
        )
        klest_wayland_client_dep = declare_dependency(
            link_with: klest_wayland_client,
            include_directories: include_directories('.'),
            dependencies: wayland_client_dep,
        )

        executable(
            'klest-background',
            'components/klest-background.c',
            include_directories: [klest_inc, include_directories('.')],
            dependencies: [klest_config_dep, klest_wayland_client_dep],
            install: true,
        )

        executable(
            'klest-panel',
            'components/klest-panel.c',
            include_directories: [klest_inc, include_directories('.')],
            dependencies: klest_wayland_client_dep,
            install: true,
        )

        executable(
            'klest-launcher',
            'components/klest-launcher.c',
            include_directories: [klest_inc, include_directories('.')],
            dependencies: [klest_wayland_client_dep, xkbcommon_dep],
            install: true,
        )

        if have_notify
            executable(
                'klest-notify',
                'components/klest-notify.c',
                include_directories: [klest_inc, include_directories('.')],
                dependencies: [klest_wayland_client_dep, sdbus_dep],
                install: true,
            )
        endif
    endif
endif

executable(
    'klest',
    'main.c',
    include_directories: klest_inc,
    dependencies: klest_deps,
    install: true,
)

# GTK configuration tool (ADR 0005). Linked against GTK only here.
if have_settings
    executable(
        'klest-settings',
        'tools/klest-settings.c',
        include_directories: [klest_inc, include_directories('.')],
        dependencies: [klest_config_dep, gtk4_dep],
        install: true,
    )
endif

# Changelog

## 0.1.0-alpha.1

First installable preview of Klest for Debian.

### Included

- wlroots-based Wayland compositor with keyboard, pointer and layer-shell support
- Workspaces, window focus, move, minimize and maximize controls
- Background, panel, application launcher and notification components
- TOML configuration and optional GTK settings tool
- Debian packages, Wayland session entry and QEMU VM test workflow

### Validation

- Unit and integration tests, including D-Bus notification coverage
- AddressSanitizer and UndefinedBehaviorSanitizer test runs
- Debian package install and headless smoke test in Debian QEMU

### Known limitations

This is an early preview, not a daily-driver desktop. Klest currently supports
Wayland-native clients only (no XWayland) and the background component supports
a solid colour only.

# Vendored Wayland protocols

Protocols Klest needs that are not shipped by the system `wayland-protocols`
package. The build generates the server/client glue from these XML files with
`wayland-scanner` (see `src/meson.build` and `test/meson.build`).

## `wlr-layer-shell-unstable-v1.xml`

- Source: [wlr-protocols](https://gitlab.freedesktop.org/wlroots/wlr-protocols),
  `unstable/wlr-layer-shell-unstable-v1.xml`.
- Copyright © 2017 Drew DeVault. Licensed under the permissive MIT-style notice
  embedded in the file.

Used by the compositor to let standalone components (background, panel,
launcher) anchor surfaces to the screen (ADR 0009).

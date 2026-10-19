# Architecture Decision Records

This directory records the technical choices behind Klest and the reasoning
that led to them. Each file documents one decision, so the project's direction
stays traceable as it grows.

Each record uses a short status:

- **Accepted** — locked in; changing it would reshape the project.
- **Proposed** — a sensible starting point, still open to revision.
- **Deferred** — not needed yet; revisit when a concrete need appears.
- **Superseded** — replaced by a later record (which it links to).

## Index

| #    | Decision                               | Status   |
| ---- | -------------------------------------- | -------- |
| 0001 | [Language: C (C17)](0001-language-c17.md) | Accepted |
| 0002 | [Display protocol: Wayland](0002-wayland.md) | Accepted |
| 0003 | [Initial target: Debian](0003-target-debian.md) | Accepted |
| 0004 | [Compositor foundation: wlroots](0004-compositor-wlroots.md) | Proposed |
| 0005 | [GUI toolkit: GTK](0005-gui-toolkit-gtk.md) | Proposed |
| 0006 | [Build system: Meson + Ninja](0006-build-meson-ninja.md) | Proposed |
| 0007 | [Configuration format: TOML](0007-config-toml.md) | Proposed |
| 0008 | [Inter-module communication](0008-ipc-unix-sockets.md) | Deferred |
| 0009 | [Modular architecture & process boundaries](0009-modular-architecture.md) | Accepted |
| 0010 | [Workspace model & control protocol](0010-workspace-model.md) | Accepted |
| 0011 | [Desktop notifications over D-Bus](0011-notifications-dbus.md) | Accepted |

## Open questions

One boundary still needs to be settled:

1. **wlroots scope** — stay close to the library, or hide it behind a Klest
   abstraction? (see 0004)

Two earlier open questions are now settled:

- **Module granularity** — resolved by 0009: a minimal compositor plus
  standalone Wayland clients for every other component.
- **Configuration contract** — resolved by 0007: simple TOML defaults with
  advanced overrides.

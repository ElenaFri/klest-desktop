# Architecture Decision Records

This directory records the technical choices behind Klest and the reasoning
that led to them. Each file documents one decision, so the project's direction
stays traceable as it grows.

Each record uses a short status:

- **Accepted** — locked in; changing it would reshape the project.
- **Proposed** — a sensible starting point, still open to revision.
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
| 0008 | [Inter-module communication: Unix sockets](0008-ipc-unix-sockets.md) | Proposed |
| 0009 | [Modular architecture & process boundaries](0009-modular-architecture.md) | Proposed |

## Open questions

The decisions above depend on three boundaries that still need to be settled:

1. **wlroots scope** — stay close to the library, or hide it behind a Klest
   abstraction? (see 0004)
2. **Module granularity** — what lives inside the compositor, and what becomes
   an independent program? This is the most consequential choice: libraries can
   change, but a poor split of responsibilities is far costlier to fix.
   (see 0009)
3. **Configuration contract** — simple defaults for newcomers, fine-grained
   control for advanced users. (see 0007)

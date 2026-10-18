# Klest

_**A lean, adaptable Wayland desktop environment in C, built to adapt and thriving through simplicity.**_

<img src="logo.png" alt="Klest's logo" width="200">

Klest takes its name from the crossbill, a bird adapted to demanding environments. Its distinctive beak is not a decoration, but a practical answer to the conditions in which it lives.

The same idea guides this project: a desktop environment should adapt to its user, not the other way around. Klest aims to be lightweight and straightforward to configure, with sensible defaults for those who want things to work and enough flexibility for those who want to shape their own environment.

Built in C and designed around Wayland, Klest follows a modular approach: small, independent components, clear interfaces, and the freedom to replace or extend parts without rebuilding everything around them.

Minimalism, here, is not about removing features for the sake of it. It is about keeping what matters, making it understandable, and leaving room for the user to decide what comes next.

> Klest is in early development.

## Technical stack

- **Language:** C (C17)
- **Display protocol:** Wayland
- **Initial target:** Debian
- **Compositor:** wlroots
- **GUI tools:** GTK
- **Build system:** Meson + Ninja
- **Configuration:** TOML
- **Module communication:** Unix sockets

The reasoning behind each choice, as well as the questions still open, are documented in the [Architecture Decision Records](docs/adr/README.md).

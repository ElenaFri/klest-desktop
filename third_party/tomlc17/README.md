# tomlc17 (vendored)

A TOML v1.0 parser in C, used by Klest for its configuration files (ADR 0007).

- **Upstream:** https://github.com/cktan/tomlc17
- **Release:** `R20260515`
- **License:** MIT (see `LICENSE`)

## Why vendored

No clean C TOML library is packaged on Debian, and Klest targets environments
that may build offline. Vendoring the two upstream source files keeps the build
reproducible and network-free. The library is compiled as an isolated static
library (`meson.build`) with warnings silenced, so Klest's strict `-Werror`
only ever applies to Klest's own code.

## Updating

Replace `tomlc17.c` and `tomlc17.h` with a newer tagged release, update the
version above and the `-DTOML_VERSION_STRING` value in `meson.build`, then run
the test suite.

## Files

- `tomlc17.c`, `tomlc17.h` — the library (upstream, unmodified).
- `API.md` — upstream API reference, kept for convenience.
- `LICENSE` — upstream MIT license.
- `meson.build` — Klest's isolated build of the above (not upstream).

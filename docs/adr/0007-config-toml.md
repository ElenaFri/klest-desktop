# 0007 — Configuration format: TOML

**Status:** Accepted

## Context

Klest should be easy to configure by hand. The format must be readable for
newcomers yet structured enough for advanced settings, and simple to parse from
C.

## Decision

Use **TOML** for Klest's configuration files.

Parse it with **tomlc17** (MIT, TOML v1.0), vendored under `third_party/tomlc17`
and built as an isolated static library. No clean C TOML library is packaged on
Debian, and Klest targets environments that may build offline; vendoring the two
upstream source files keeps the build reproducible and network-free. The vendored
code is compiled with warnings silenced so Klest's strict `-Werror` only ever
applies to Klest's own code.

## Consequences

- Human-readable and structured, well suited to settings grouped into sections.
- Unambiguous and easier to hand-edit than formats with significant whitespace
  or heavy punctuation.
- One vendored third-party dependency to track and occasionally update
  (see `third_party/tomlc17/README.md`).

## The configuration contract

The format is only part of the problem. The contract serves two audiences, and
is implemented in `src/config`:

- **Newcomers:** built-in defaults make Klest work with no config file at all. A
  missing file is not an error.
- **Advanced users:** a TOML file overlays only the keys it sets; every other
  setting keeps its default. A malformed file logs a warning and the defaults
  are kept, so the compositor always starts.

This "defaults, then overlay" layering — not the file format — is what keeps
simple setups simple while leaving room for finer control. New settings are added
by extending `struct klest_config`, its defaults, and the loader in lockstep.

See `examples/config.toml` for the current schema.

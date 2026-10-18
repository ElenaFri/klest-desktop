# 0007 — Configuration format: TOML

**Status:** Proposed

## Context

Klest should be easy to configure by hand. The format must be readable for
newcomers yet structured enough for advanced settings, and simple to parse from
C.

## Decision

Use **TOML** for Klest's configuration files.

## Consequences

- Human-readable and structured, well suited to settings grouped into sections.
- Unambiguous and easier to hand-edit than formats with significant whitespace
  or heavy punctuation.
- Requires a C TOML parser as a dependency.

## Open question — the configuration contract

The format is only part of the problem. The contract must serve two audiences:

- **Newcomers:** sensible defaults, minimal required configuration — things work
  out of the box.
- **Advanced users:** access to finer-grained settings without the defaults
  getting in the way.

How layering, defaults, and overrides are structured matters more than the file
format itself and should be designed deliberately.

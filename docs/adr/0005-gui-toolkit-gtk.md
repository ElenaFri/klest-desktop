# 0005 — GUI toolkit: GTK

**Status:** Proposed

## Context

Klest needs graphical tools — a configuration utility and possibly small
helpers. These need a toolkit, ideally one that does not introduce a second
principal language alongside C.

## Decision

Use **GTK** for Klest's graphical tools.

## Consequences

- C-native API, consistent with the language decision (0001); no second core
  language.
- Mature toolkit with good Wayland support.
- Scope is limited to user-facing tools — GTK is **not** a dependency of the
  compositor itself, which keeps the core lean.
- Adds GTK to the dependency surface for the parts that use it; those tools can
  evolve or be replaced independently thanks to the modular design (0009).

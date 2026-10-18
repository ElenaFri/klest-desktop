# 0005 — GUI toolkit: GTK

**Status:** Accepted

## Context

Klest needs graphical tools — a configuration utility and possibly small
helpers. These need a toolkit, ideally one that does not introduce a second
principal language alongside C.

## Decision

Use **GTK** (GTK 4) for Klest's graphical tools.

The first such tool is `klest-settings`, which reads and writes the compositor's
TOML configuration (ADR 0007) via the shared `klest_config` module. It is built
as a separate, optional executable gated behind the `settings` Meson feature.

## Consequences

- C-native API, consistent with the language decision (0001); no second core
  language.
- Mature toolkit with good Wayland support.
- Scope is limited to user-facing tools — GTK is **not** a dependency of the
  compositor itself, which keeps the core lean. It is linked only into the tools.
- Adds GTK to the dependency surface for the parts that use it; those tools can
  evolve or be replaced independently thanks to the modular design (0009).

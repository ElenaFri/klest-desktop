# 0003 — Initial target: Debian

**Status:** Accepted

## Context

Supporting many distributions from day one spreads effort across packaging,
dependency versions, and deployment quirks before the project is even stable.

## Decision

Klest targets **Debian** first.

Starting on a single, well-understood platform lets us control development,
dependencies, and deployment before broadening compatibility.

## Consequences

- One reference environment for building, testing, and documenting setup.
- Dependency versions (wlroots, GTK, Meson) are pinned to what Debian provides,
  keeping the moving parts predictable.
- Broader distribution support is possible later, but is deliberately out of
  scope until the core is solid. It should be treated as a separate decision
  when the time comes.

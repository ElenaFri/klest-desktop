# 0004 — Compositor foundation: wlroots

**Status:** Accepted (coupling settled: stay close)

## Context

Building a Wayland compositor from scratch means reimplementing a large amount
of plumbing: output management, input handling, buffer management, and the
Wayland protocol glue. wlroots is a C library that provides these building
blocks and backs several established compositors.

## Decision

Use **wlroots** as the foundation for Klest's compositor: window, output, and
input handling plus Wayland integration.

## Consequences

- Avoids rebuilding low-level graphics plumbing; effort goes into Klest's own
  behaviour instead.
- C-native, so it fits the language decision (0001) with no binding layer.
- Ties Klest to wlroots' release cadence and API changes.

## Open question — resolved

How tightly should Klest couple to wlroots?

- **Stay close:** use wlroots types and idioms directly — less code, faster to
  build, but wlroots details leak throughout Klest.
- **Abstract:** hide wlroots behind a Klest-owned interface — more insulation
  and easier to swap later, at the cost of extra indirection now.

**Resolution: stay close.** The compositor uses wlroots types and idioms
directly, mirroring the canonical wlroots layout (cf. tinywl). The alpha code
already follows this shape — `struct klest_server` intentionally mirrors the
standard wlroots server, and listener dispatch relies on `wl_container_of()`.
An abstraction layer would add indirection now for insulation Klest does not
need: the wlroots API surface Klest touches is small and well identified
(server, views, layers, input), and a later swap would reshape the compositor
regardless. If wlroots ever becomes a constraint, the boundary to refactor is
the compositor directory, not a wrapper API.

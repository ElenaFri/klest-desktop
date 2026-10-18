# 0004 — Compositor foundation: wlroots

**Status:** Proposed

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

## Open question

How tightly should Klest couple to wlroots?

- **Stay close:** use wlroots types and idioms directly — less code, faster to
  build, but wlroots details leak throughout Klest.
- **Abstract:** hide wlroots behind a Klest-owned interface — more insulation
  and easier to swap later, at the cost of extra indirection now.

This should be resolved early, as it shapes how the compositor is written.

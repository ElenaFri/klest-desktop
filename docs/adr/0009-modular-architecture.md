# 0009 — Modular architecture & process boundaries

**Status:** Proposed

## Context

Klest should not be built as a single monolithic desktop. The goal is a modular
system where each part has a clear responsibility and a defined interface, so a
panel or service can be replaced without rewriting the compositor.

But modularity has a cost: turning everything into an independent process adds
complexity and unnecessary communication machinery.

## Decision

Separate responsibilities **progressively**, around three broad areas:

1. **Wayland compositor** — windows, outputs, keyboard, mouse.
2. **Klest interfaces & services** — configuration, shortcuts, panels, utilities.
3. **Shared configuration & interfaces** — settings, communication, and the
   conventions components agree on.

Each component has one clear responsibility and a defined interface. Components
are split into separate processes **only where the separation earns its cost**.

## Consequences

- Parts can be replaced or extended independently without rebuilding the rest.
- Clear interfaces make the system easier to reason about and test (unit +
  integration, see the build decision 0006).
- Communication cost is incurred only at genuine process boundaries (0008).

## Open question — module granularity

This is the most consequential open decision in the project. What belongs
*inside* the compositor, and what becomes a *standalone* program?

Libraries can be swapped; a poor split of responsibilities is far more expensive
to correct later. The boundaries must be chosen deliberately, resisting the urge
to make everything a separate process.

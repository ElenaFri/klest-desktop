# 0009 — Modular architecture & process boundaries

**Status:** Accepted

## Context

Klest should not be built as a single monolithic desktop. The goal is a modular
system where each part has a clear responsibility and a defined interface, so a
panel or service can be replaced without rewriting the compositor.

But modularity has a cost: turning everything into an independent process adds
complexity and unnecessary communication machinery. The key question was the
*module granularity* — what belongs inside the compositor, and what becomes a
standalone program.

## Decision

Keep the **compositor minimal**. It owns only what must live next to the
Wayland server:

- outputs, windows (xdg-shell), keyboard and mouse input;
- the **layer-shell** protocol (`wlr-layer-shell-unstable-v1`), so other
  components can anchor surfaces to the screen edges.

Everything else — background, panel, launcher, notifications, utilities — is a
**standalone Wayland client**. These components talk to the compositor through
**standard Wayland protocols** (primarily layer-shell), not through a
Klest-specific channel. This is the idiomatic wlroots model (cf. Sway with
`swaybg`, `swaybar`, `swaynag`).

Responsibilities are separated **progressively**, around three broad areas:

1. **Wayland compositor** — windows, outputs, input, layer-shell.
2. **Klest components** — background, panel, launcher, notifications, utilities,
   each a standalone Wayland client.
3. **Shared configuration & conventions** — the TOML config (0007) and the
   interfaces components agree on.

## Consequences

- Components can be replaced or extended independently without rebuilding the
  compositor.
- The compositor ↔ component channel is Wayland itself, so most components need
  **no custom IPC** — this reduces the scope of the Unix-socket decision (0008)
  to a Klest-specific control channel only.
- Clear interfaces make the system easier to reason about and test (unit +
  integration, see 0006). Layer-shell can be exercised headless like xdg-shell.
- The first components validate the pattern before heavier ones are built.

# 0008 — Inter-module communication

**Status:** Deferred (scope reduced by 0009)

## Context

If Klest separated responsibilities into independent local processes, those
processes would need a way to talk to each other.

## Decision

The architecture decision (0009) keeps the compositor minimal and makes every
other component a **standalone Wayland client**. The compositor ↔ component
channel is therefore **Wayland itself** (primarily the layer-shell protocol) —
not a Klest-specific transport. Most components need **no custom IPC at all**.

A dedicated inter-process channel is only justified for a **Klest-specific
control need** that Wayland does not cover, for example:

- asking a running component (or the compositor) to reload its configuration;
- a `klestctl`-style command to query or drive the session.

**When such a need actually appears**, use **Unix domain sockets** (simple,
local, well-supported in C, no external broker). Until then this decision stays
deferred, to avoid building IPC machinery nothing uses.

## Consequences

- No socket layer is introduced speculatively.
- When a control channel is needed, Unix sockets are the chosen mechanism; a
  small message/protocol layer on top of the socket will then have to be defined.

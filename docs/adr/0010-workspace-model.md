# 0010 — Workspace model & control protocol

**Status:** Accepted

## Context

A desktop needs workspaces (virtual desktops) to group windows. Two questions
had to be settled: how many workspaces, and how the panel learns about them and
switches between them.

ADR 0009 kept the compositor minimal and had components talk through **standard**
Wayland protocols, explicitly avoiding a Klest-specific channel. Workspaces,
however, are compositor state (which window is on which workspace, which one is
active) that no standard protocol exposes. The panel must both *read* that state
and *request* a switch.

## Decision

- **Fixed set of four workspaces** (`KLEST_WORKSPACE_COUNT`). A fixed count keeps
  the model, the UI and the keybindings simple; a dynamic count can come later if
  a real need appears.
- The compositor owns the model: each toplevel carries a workspace index, and
  switching toggles scene-node visibility and focus. Keybindings live in the
  compositor: `Alt`+`1..4` activates, `Alt`+`Shift`+`1..4` moves the focused
  window.
- A small **Klest-specific Wayland protocol**, `klest_workspace_manager_v1`,
  exposes the state. It has one event (`state`: count + active index) and one
  request (`activate`). The panel binds it, reflects the active workspace, and
  switches on click.

## Consequences

- This is the first Klest-specific Wayland protocol, a deliberate narrowing of
  0009: components still use standard protocols by default, and a custom protocol
  is introduced only for state that standard protocols cannot carry. Custom
  protocols are idiomatic in the wlroots ecosystem (cf. wlr-* protocols).
- The protocol is intentionally tiny and versioned (`v1`), so it can grow
  (per-workspace names, dynamic count) without breaking existing clients.
- Panel and compositor stay independent: the panel degrades gracefully when the
  global is absent, and the model is covered by the headless integration test.

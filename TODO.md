# Klest — TODO

## Decisions to lock

- [x] Language: C (C17)
- [x] Display protocol: Wayland
- [x] Initial target: Debian
- [x] Compositor foundation: wlroots
- [x] GUI toolkit: GTK
- [x] Build system: Meson + Ninja
- [x] Configuration format: TOML (tomlc17, vendored — ADR 0007)
- [x] Inter-module communication: deferred — Wayland covers compositor ↔ component; Unix sockets only for a future Klest control channel (ADR 0008)

## Open questions to settle

- [ ] wlroots scope: stay close vs. hide behind a Klest abstraction (ADR 0004)
- [x] Module granularity: minimal compositor + standalone Wayland clients (ADR 0009)
- [x] Configuration contract: simple defaults + advanced overrides (ADR 0007)

## Project setup

- [x] Set up Meson + Ninja build skeleton
- [x] Configure strict warnings (`-Wall -Wextra`) and sanitizers (ASan/UBSan)
- [x] Set up CI (GitHub Actions: build + test)
- [x] Add unit test harness
- [x] Add compositor integration tests (headless startup and window lifecycle)
- [x] Document the Debian build/run requirements

## First milestones

- [x] Minimal wlroots-based compositor that starts and shows an output
- [x] Handle keyboard and mouse input
- [x] Open, move, and close a window
- [x] Load configuration from a TOML file
- [x] First GTK configuration tool (`klest-settings`)

## Roadmap — components (ADR 0009)

The compositor stays minimal; every other part is a standalone Wayland client
that anchors to the screen through layer-shell.

- [x] Compositor: implement `wlr-layer-shell-unstable-v1` (+ headless test)
- [x] `klest-background` — standalone solid-colour layer-shell client (image loading remains future work)
- [ ] `klest-panel` — bar (workspaces, clock)
- [ ] `klest-launcher` — application launcher
- [ ] Notifications daemon
- [ ] Session startup: launch the compositor and its components together
- [ ] Klest control channel (Unix socket) — only if a real need appears (ADR 0008)

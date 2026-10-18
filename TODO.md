# Klest — TODO

## Decisions to lock

- [x] Language: C (C17)
- [x] Display protocol: Wayland
- [x] Initial target: Debian
- [x] Compositor foundation: wlroots
- [x] GUI toolkit: GTK
- [x] Build system: Meson + Ninja
- [x] Configuration format: TOML (tomlc17, vendored — ADR 0007)
- [ ] Inter-module communication: Unix sockets

## Open questions to settle

- [ ] wlroots scope: stay close vs. hide behind a Klest abstraction (ADR 0004)
- [ ] Module granularity: what is inside the compositor vs. standalone (ADR 0009)
- [x] Configuration contract: simple defaults + advanced overrides (ADR 0007)

## Project setup

- [x] Set up Meson + Ninja build skeleton
- [x] Configure strict warnings (`-Wall -Wextra`) and sanitizers (ASan/UBSan)
- [x] Set up CI (GitHub Actions: build + test)
- [x] Add unit + integration test harness
- [x] Document the Debian build/run requirements

## First milestones

- [x] Minimal wlroots-based compositor that starts and shows an output
- [x] Handle keyboard and mouse input
- [x] Open, move, and close a window
- [x] Load configuration from a TOML file
- [x] First GTK configuration tool (`klest-settings`)

# Klest — TODO

## Decisions to lock

- [x] Language: C (C17)
- [x] Display protocol: Wayland
- [x] Initial target: Debian
- [ ] Compositor foundation: wlroots
- [ ] GUI toolkit: GTK
- [ ] Build system: Meson + Ninja
- [ ] Configuration format: TOML
- [ ] Inter-module communication: Unix sockets

## Open questions to settle

- [ ] wlroots scope: stay close vs. hide behind a Klest abstraction (ADR 0004)
- [ ] Module granularity: what is inside the compositor vs. standalone (ADR 0009)
- [ ] Configuration contract: simple defaults + advanced overrides (ADR 0007)

## Project setup

- [ ] Set up Meson + Ninja build skeleton
- [ ] Configure strict warnings (`-Wall -Wextra`) and sanitizers (ASan/UBSan)
- [ ] Set up CI (GitHub Actions: build + test)
- [ ] Add unit + integration test harness
- [ ] Document the Debian build/run requirements

## First milestones

- [ ] Minimal wlroots-based compositor that starts and shows an output
- [ ] Handle keyboard and mouse input
- [ ] Open, move, and close a window
- [ ] Load configuration from a TOML file
- [ ] First GTK configuration tool

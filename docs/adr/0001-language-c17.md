# 0001 — Language: C (C17)

**Status:** Accepted

## Context

Klest is a desktop environment that must stay lightweight, predictable, and
close to the system layers it builds on (Wayland, wlroots, GTK). The core
libraries in this space expose C APIs.

## Decision

Klest is written in **C**, targeting the **C17** standard.

C17 is modern, stable, and broadly supported by current GCC and Clang
toolchains, without pulling in a second principal language for the core.

## Consequences

- Direct, idiomatic access to the C ecosystem (Wayland, wlroots, GTK) with no
  binding layer.
- Full control over memory and resources — and full responsibility for them.
  Rigorous resource management and strict compiler warnings are expected
  (see 0006).
- Explicit architecture with limited, well-defined interfaces between
  components.
- No managed-language safety net: discipline around ownership, lifetimes, and
  error handling must be part of the code review culture from the start.

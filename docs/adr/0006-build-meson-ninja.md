# 0006 — Build system: Meson + Ninja

**Status:** Accepted

## Context

Klest is a C project organised into several components (compositor, services,
tools, shared code). It needs a build system that handles multiple targets and
dependencies cleanly, and that is well supported on the target platform (0003).

## Decision

Use **Meson** as the build definition, with **Ninja** as the backend.

## Consequences

- A concise, readable build description suited to a multi-component C project.
- Fast incremental builds via Ninja.
- Well integrated with the dependencies already chosen (wlroots, GTK), which
  ship Meson/pkg-config support.
- Strict compiler settings — `-Wall -Wextra`, warnings-as-errors where
  practical, and sanitizers (ASan/UBSan) in debug builds — are configured here
  to catch memory errors and undefined behaviour early (see 0001).

# 0002 — Display protocol: Wayland

**Status:** Accepted

## Context

A desktop environment needs a display server foundation. The two realistic
options on Linux are X11 and Wayland. X11 carries decades of legacy design and
is in maintenance mode; Wayland is the direction the Linux graphics stack is
moving toward.

## Decision

Klest is designed for **Wayland from the start**, without relying on X11 as a
foundation.

## Consequences

- A simpler, more secure model than X11, aligned with where the ecosystem is
  heading.
- Klest acts as (or hosts) a Wayland compositor rather than a client of an
  external display server (see 0004).
- X11 applications are not a foundational concern. If needed later, they can be
  supported through a compatibility layer (e.g. Xwayland), as an explicit,
  separate decision — not as a core dependency.

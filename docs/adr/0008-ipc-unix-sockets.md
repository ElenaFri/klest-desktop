# 0008 — Inter-module communication: Unix sockets

**Status:** Proposed

## Context

If Klest separates responsibilities into independent local processes (0009),
those processes need a way to talk to each other. The mechanism should be
simple, local, and well supported in C.

## Decision

Where independent components need to communicate, use **Unix domain sockets**.

## Consequences

- Simple and reliable for local, same-machine components.
- Standard, well-understood C APIs; no external message broker.
- A message/protocol layer on top of the socket still needs to be defined.

## Dependency on module granularity

This decision is **conditional** on the architecture actually splitting work
across processes. The communication protocol depends on how components are
separated (0009): if a function lives inside the compositor, it needs no socket
at all. Settle the boundaries first; adopt sockets only where a real process
boundary exists, to avoid needless IPC complexity.

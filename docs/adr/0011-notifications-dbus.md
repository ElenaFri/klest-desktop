# 0011 — Desktop notifications over D-Bus

**Status:** Accepted

## Context

Applications expect to post desktop notifications through the
`org.freedesktop.Notifications` D-Bus interface — the de-facto standard every
toolkit (GTK, Qt, libnotify) targets. To show notifications under Klest, a
component must own that bus name and render the messages on screen.

Two questions had to be settled: whether to speak D-Bus at all (rather than a
Klest-specific channel), and which D-Bus library to link.

ADR 0009 keeps the compositor minimal and makes every other part a standalone
Wayland client. A notification daemon fits that model exactly: it renders
through layer-shell like the panel and background.

## Decision

- **Implement the standard `org.freedesktop.Notifications` interface.** Speaking
  the freedesktop protocol is the whole point: unmodified applications must be
  able to notify without knowing about Klest. This is an exception to 0009's
  "standard Wayland protocols" preference only in that the transport is the
  session bus — the natural home of this API — not Wayland.
- **Use sd-bus from `libsystemd`.** It is a clean, modern C API, already present
  on Debian, and needs no extra runtime beyond the session bus. `basu` (a
  standalone sd-bus fork) is a drop-in alternative for non-systemd systems and
  can be wired later; the code uses only the portable sd-bus subset. The daemon
  is an **optional** build target (like the compositor and the GTK tool): when
  the library is absent, the rest of Klest still builds.
- **`klest-notify` is a standalone layer-shell client.** It draws a vertical
  stack of notification cards in the top-right corner on a transparent overlay
  surface (anchored top/bottom/right, click-through), reusing the shared bitmap
  font and SHM helpers. Notifications expire on their timeout; a single poll
  loop multiplexes the Wayland and D-Bus file descriptors.

## Consequences

- Any application can post notifications to Klest with no Klest-specific code.
- Scope is deliberately small for v1: `Notify`, `CloseNotification`,
  `GetCapabilities` (`body`), `GetServerInformation`, and the
  `NotificationClosed` signal. Actions, icons, markup and sound are future work
  and are not advertised as capabilities, so clients degrade gracefully.
- The transparent full-column overlay avoids dynamic surface resizing: cards are
  drawn at the top and the rest stays fully transparent and click-through.
- A `--once` self-test renders a synthetic notification and exits, so the render
  path is covered by the headless integration test without requiring a session
  bus in CI.

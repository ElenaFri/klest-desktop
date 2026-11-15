/* Klest — compositor keybinding decisions (INSTALL.md, "Keybindings").
 *
 * Pure lookup only: no wlroots, no server state. input.c owns the effects
 * (spawning, focusing, workspace calls), which keeps this module unit-testable
 * and the binding contract — the shortcuts documented in INSTALL.md — explicit.
 */
#ifndef KLEST_COMPOSITOR_BINDINGS_H
#define KLEST_COMPOSITOR_BINDINGS_H

#include <stdbool.h>
#include <stdint.h>

#include <xkbcommon/xkbcommon.h>

/* Same bit values as wlroots' WLR_MODIFIER_ALT / WLR_MODIFIER_SHIFT, restated
 * so this header does not pull wlroots in; input.c static-asserts the match. */
#define KLEST_MOD_SHIFT (1u << 0)
#define KLEST_MOD_ALT (1u << 3)

enum klest_action {
	KLEST_ACTION_NONE = 0,
	KLEST_ACTION_QUIT,
	KLEST_ACTION_SPAWN_TERMINAL,
	KLEST_ACTION_SPAWN_LAUNCHER,
	KLEST_ACTION_MINIMIZE_FOCUSED,
	KLEST_ACTION_TOGGLE_MAXIMIZE_FOCUSED,
	KLEST_ACTION_CYCLE_FOCUS,
	KLEST_ACTION_ACTIVATE_WORKSPACE,
	KLEST_ACTION_MOVE_FOCUSED_TO_WORKSPACE,
};

struct klest_binding {
	enum klest_action action;
	uint32_t workspace; /* workspace index, for the two workspace actions */
};

/*
 * Workspace binding: Alt+1..4 activates a workspace, Alt+Shift+1..4 moves the
 * focused window to it. Resolved from the raw key NAME (e.g. "AE01") rather
 * than the keysym, because Shift+digit yields layout-dependent symbols.
 *
 * `key_name` may be NULL (key not in the keymap). Returns false when the key
 * is not a workspace binding, in which case the caller must not consume it.
 */
bool klest_bindings_workspace(uint32_t modifiers, const char *key_name,
	struct klest_binding *out);

/*
 * Action binding for Alt-modified keys: Escape quits, Return spawns the
 * configured terminal, Space the launcher, F9 minimizes, F10 toggles
 * maximize, F1 cycles focus. The Alt gate is the caller's (input.c).
 * Returns false when the keysym carries no binding.
 */
bool klest_bindings_action(xkb_keysym_t sym, struct klest_binding *out);

#endif /* KLEST_COMPOSITOR_BINDINGS_H */

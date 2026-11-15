/*
 * Klest — keybinding contract tests (INSTALL.md, "Keybindings").
 *
 * These pin the documented shortcuts: which keys the compositor consumes and
 * what action each resolves to, plus the negative cases — keys that must NOT
 * be consumed, because a wrongly consumed key never reaches the client.
 */

#include "compositor/bindings.h"
#include "compositor/workspace.h"

#include <assert.h>

/* Alt+1..4 activate workspaces 0..3; Alt+Shift+1..4 move the focused window. */
static void test_workspace_bindings(void)
{
	struct klest_binding b;

	for (uint32_t i = 0; i < KLEST_WORKSPACE_COUNT; i++) {
		char name[] = "AE0x";
		name[3] = (char)('1' + i);

		assert(klest_bindings_workspace(KLEST_MOD_ALT, name, &b));
		assert(b.action == KLEST_ACTION_ACTIVATE_WORKSPACE);
		assert(b.workspace == i);

		assert(klest_bindings_workspace(KLEST_MOD_ALT | KLEST_MOD_SHIFT,
			name, &b));
		assert(b.action == KLEST_ACTION_MOVE_FOCUSED_TO_WORKSPACE);
		assert(b.workspace == i);
	}
}

/* Without Alt the digits must stay available to the client. */
static void test_workspace_requires_alt(void)
{
	struct klest_binding b;
	assert(!klest_bindings_workspace(0, "AE01", &b));
	assert(!klest_bindings_workspace(KLEST_MOD_SHIFT, "AE01", &b));
	assert(!klest_bindings_workspace(KLEST_MOD_ALT, NULL, &b));
}

/* Only digits 1..4 are bound: 5 and beyond, and non-digit keys, are not. */
static void test_workspace_rejects_other_keys(void)
{
	struct klest_binding b;
	assert(!klest_bindings_workspace(KLEST_MOD_ALT, "AE05", &b));
	assert(!klest_bindings_workspace(KLEST_MOD_ALT, "AE10", &b));
	assert(!klest_bindings_workspace(KLEST_MOD_ALT, "FK09", &b)); /* F9 */
	assert(!klest_bindings_workspace(KLEST_MOD_ALT, "KP1", &b));  /* numpad */
}

/* Alt+action keys resolve to their documented actions. */
static void test_action_bindings(void)
{
	struct klest_binding b;

	assert(klest_bindings_action(XKB_KEY_Escape, &b));
	assert(b.action == KLEST_ACTION_QUIT);

	assert(klest_bindings_action(XKB_KEY_Return, &b));
	assert(b.action == KLEST_ACTION_SPAWN_TERMINAL);

	assert(klest_bindings_action(XKB_KEY_space, &b));
	assert(b.action == KLEST_ACTION_SPAWN_LAUNCHER);

	assert(klest_bindings_action(XKB_KEY_F9, &b));
	assert(b.action == KLEST_ACTION_MINIMIZE_FOCUSED);

	assert(klest_bindings_action(XKB_KEY_F10, &b));
	assert(b.action == KLEST_ACTION_TOGGLE_MAXIMIZE_FOCUSED);

	assert(klest_bindings_action(XKB_KEY_F1, &b));
	assert(b.action == KLEST_ACTION_CYCLE_FOCUS);
}

/* Unbound keysyms must not be consumed. F2-F8 stay free. */
static void test_action_rejects_unbound_keys(void)
{
	struct klest_binding b;
	assert(!klest_bindings_action(XKB_KEY_F2, &b));
	assert(!klest_bindings_action(XKB_KEY_F8, &b));
	assert(!klest_bindings_action(XKB_KEY_a, &b));
	assert(!klest_bindings_action(XKB_KEY_NoSymbol, &b));
}

int main(void)
{
	test_workspace_bindings();
	test_workspace_requires_alt();
	test_workspace_rejects_other_keys();
	test_action_bindings();
	test_action_rejects_unbound_keys();
	return 0;
}

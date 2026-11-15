/* Klest — compositor keybinding decisions (see bindings.h). */

#include "compositor/bindings.h"

#include <string.h>

#include "workspace.h"

bool klest_bindings_workspace(uint32_t modifiers, const char *key_name,
	struct klest_binding *out)
{
	*out = (struct klest_binding){0};
	if ((modifiers & KLEST_MOD_ALT) == 0 || key_name == NULL) {
		return false;
	}

	/* Raw key names for the digits 1..4 on the main row. */
	static const char *const key_names[KLEST_WORKSPACE_COUNT] = {
		"AE01", "AE02", "AE03", "AE04",
	};
	for (uint32_t i = 0; i < KLEST_WORKSPACE_COUNT; i++) {
		if (strcmp(key_name, key_names[i]) != 0) {
			continue;
		}
		out->workspace = i;
		out->action = (modifiers & KLEST_MOD_SHIFT) != 0 ?
			KLEST_ACTION_MOVE_FOCUSED_TO_WORKSPACE :
			KLEST_ACTION_ACTIVATE_WORKSPACE;
		return true;
	}
	return false;
}

bool klest_bindings_action(xkb_keysym_t sym, struct klest_binding *out)
{
	*out = (struct klest_binding){0};
	switch (sym) {
	case XKB_KEY_Escape:
		out->action = KLEST_ACTION_QUIT;
		return true;
	case XKB_KEY_Return:
		out->action = KLEST_ACTION_SPAWN_TERMINAL;
		return true;
	case XKB_KEY_space:
		out->action = KLEST_ACTION_SPAWN_LAUNCHER;
		return true;
	case XKB_KEY_F9:
		out->action = KLEST_ACTION_MINIMIZE_FOCUSED;
		return true;
	case XKB_KEY_F10:
		out->action = KLEST_ACTION_TOGGLE_MAXIMIZE_FOCUSED;
		return true;
	case XKB_KEY_F1:
		out->action = KLEST_ACTION_CYCLE_FOCUS;
		return true;
	default:
		return false;
	}
}

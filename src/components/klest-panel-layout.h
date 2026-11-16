/*
 * Klest — panel layout model (pure geometry and hit-testing).
 *
 * The panel's placement maths and window-list bookkeeping are kept free of
 * Wayland and rendering so they can be unit-tested directly, mirroring the
 * split used by the compositor's bindings module.
 */
#ifndef KLEST_PANEL_LAYOUT_H
#define KLEST_PANEL_LAYOUT_H

#include <stdbool.h>
#include <stdint.h>

#define KLEST_PANEL_HEIGHT 36U
#define KLEST_GLYPH_SCALE 2U
#define KLEST_WS_ORIGIN_X 96U
#define KLEST_WS_BOX_WIDTH 26U
#define KLEST_PANEL_MAX_WORKSPACES 9U
#define KLEST_WINDOW_ORIGIN_X (KLEST_WS_ORIGIN_X + \
	KLEST_PANEL_MAX_WORKSPACES * KLEST_WS_BOX_WIDTH + 8U)
#define KLEST_WINDOW_BOX_WIDTH 144U
#define KLEST_WINDOW_LABEL_CHARS 14U
#define KLEST_WINDOW_TITLE_LENGTH 128U

struct panel_window {
	struct panel_window *next;
	uint32_t id;
	uint32_t workspace;
	char title[KLEST_WINDOW_TITLE_LENGTH];
	bool active;
	bool minimized;
	bool maximized;
};

/* Number of workspace indicators to draw, capped at KLEST_PANEL_MAX_WORKSPACES. */
uint32_t klest_panel_visible_workspaces(uint32_t workspace_count);

/* Background colour of a window-list box: active, minimized, or plain. */
uint32_t klest_panel_window_box_color(const struct panel_window *window);

/* Copies up to KLEST_WINDOW_LABEL_CHARS of `title` into `label`, replacing the
 * tail with an ellipsis when the title is longer. `label` must hold at least
 * KLEST_WINDOW_LABEL_CHARS + 1 bytes. */
void klest_panel_truncate_title(char *label, const char *title);

/* Finds a window by id in the singly linked list, or NULL. */
struct panel_window *klest_panel_window_by_id(struct panel_window *windows,
	uint32_t id);

/* Returns the window whose box contains `pointer_x` on the active workspace,
 * honouring the same right-edge clipping as the renderer (the window strip ends
 * 8px before `clock_x`), or NULL. */
const struct panel_window *klest_panel_window_at(
	const struct panel_window *windows, uint32_t active_workspace,
	uint32_t clock_x, double pointer_x);

/* Maps a pointer x coordinate to a workspace index, or -1 when none. */
int klest_panel_workspace_at(uint32_t workspace_count, double x);

/* A click on the panel resolves to one of these actions. */
enum klest_panel_action {
	KLEST_PANEL_ACTION_NONE,
	KLEST_PANEL_ACTION_ACTIVATE_WORKSPACE,
	KLEST_PANEL_ACTION_MINIMIZE_WINDOW,
	KLEST_PANEL_ACTION_ACTIVATE_WINDOW,
};

struct klest_panel_click {
	enum klest_panel_action action;
	uint32_t target; /* workspace index or window id, per action */
};

/* Resolves a left-click at `pointer_x` into an action: a workspace indicator
 * switches workspace; otherwise, when `supports_windows` is set, a window box
 * toggles that window (minimize when it is the active one, else activate). */
struct klest_panel_click klest_panel_click_at(uint32_t workspace_count,
	uint32_t active_workspace, const struct panel_window *windows,
	uint32_t clock_x, double pointer_x, bool supports_windows);

/* Removes the window with `id` from the list, freeing it. Returns the new head
 * and, via `removed`, whether a node was unlinked. */
struct panel_window *klest_panel_window_remove(struct panel_window *windows,
	uint32_t id, bool *removed);

#endif /* KLEST_PANEL_LAYOUT_H */

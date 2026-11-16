/* Klest — panel layout model (see klest-panel-layout.h). */

#include "klest-panel-layout.h"

#include <stdlib.h>
#include <string.h>

uint32_t klest_panel_visible_workspaces(uint32_t workspace_count)
{
	return workspace_count < KLEST_PANEL_MAX_WORKSPACES ?
		workspace_count : KLEST_PANEL_MAX_WORKSPACES;
}

uint32_t klest_panel_window_box_color(const struct panel_window *window)
{
	if (window->active) {
		return 0xff5c8fe8U;
	}
	return window->minimized ? 0xff20222cU : 0xff2a2d3eU;
}

void klest_panel_truncate_title(char *label, const char *title)
{
	size_t length = 0;
	while (length < KLEST_WINDOW_LABEL_CHARS && title[length] != '\0') {
		label[length] = title[length];
		length++;
	}
	if (length == KLEST_WINDOW_LABEL_CHARS && title[length] != '\0') {
		label[length - 3U] = '.';
		label[length - 2U] = '.';
		label[length - 1U] = '.';
	}
	label[length] = '\0';
}

struct panel_window *klest_panel_window_by_id(struct panel_window *windows,
	uint32_t id)
{
	for (struct panel_window *window = windows; window != NULL;
			window = window->next) {
		if (window->id == id) {
			return window;
		}
	}
	return NULL;
}

const struct panel_window *klest_panel_window_at(
	const struct panel_window *windows, uint32_t active_workspace,
	uint32_t clock_x, double pointer_x)
{
	uint32_t x = KLEST_WINDOW_ORIGIN_X;
	uint32_t right = clock_x;
	if (right > 8U) {
		right -= 8U;
	}
	for (const struct panel_window *window = windows; window != NULL;
			window = window->next) {
		if (window->workspace != active_workspace) {
			continue;
		}
		if (x + KLEST_WINDOW_BOX_WIDTH > right) {
			break;
		}
		if (pointer_x >= (double)x &&
				pointer_x < (double)(x + KLEST_WINDOW_BOX_WIDTH - 4U)) {
			return window;
		}
		x += KLEST_WINDOW_BOX_WIDTH;
	}
	return NULL;
}

int klest_panel_workspace_at(uint32_t workspace_count, double x)
{
	if (x < (double)KLEST_WS_ORIGIN_X) {
		return -1;
	}
	uint32_t index = (uint32_t)((x - (double)KLEST_WS_ORIGIN_X) /
		(double)KLEST_WS_BOX_WIDTH);
	return index < klest_panel_visible_workspaces(workspace_count) ?
		(int)index : -1;
}

struct klest_panel_click klest_panel_click_at(uint32_t workspace_count,
	uint32_t active_workspace, const struct panel_window *windows,
	uint32_t clock_x, double pointer_x, bool supports_windows)
{
	int index = klest_panel_workspace_at(workspace_count, pointer_x);
	if (index >= 0) {
		return (struct klest_panel_click){
			.action = KLEST_PANEL_ACTION_ACTIVATE_WORKSPACE,
			.target = (uint32_t)index,
		};
	}
	if (!supports_windows) {
		return (struct klest_panel_click){.action = KLEST_PANEL_ACTION_NONE};
	}
	const struct panel_window *window = klest_panel_window_at(windows,
		active_workspace, clock_x, pointer_x);
	if (window == NULL) {
		return (struct klest_panel_click){.action = KLEST_PANEL_ACTION_NONE};
	}
	if (window->active && !window->minimized) {
		return (struct klest_panel_click){
			.action = KLEST_PANEL_ACTION_MINIMIZE_WINDOW,
			.target = window->id,
		};
	}
	return (struct klest_panel_click){
		.action = KLEST_PANEL_ACTION_ACTIVATE_WINDOW,
		.target = window->id,
	};
}

struct panel_window *klest_panel_window_remove(struct panel_window *windows,
	uint32_t id, bool *removed)
{
	*removed = false;
	struct panel_window **link = &windows;
	while (*link != NULL) {
		if ((*link)->id == id) {
			struct panel_window *dead = *link;
			*link = dead->next;
			free(dead);
			*removed = true;
			return windows;
		}
		link = &(*link)->next;
	}
	return windows;
}

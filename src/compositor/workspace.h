/* Klest workspace model and Wayland control protocol. */
#ifndef KLEST_COMPOSITOR_WORKSPACE_H
#define KLEST_COMPOSITOR_WORKSPACE_H

#include <stdbool.h>
#include <stdint.h>

#define KLEST_WORKSPACE_COUNT 4U

struct klest_server;
struct klest_toplevel;

bool klest_workspaces_init(struct klest_server *server);
void klest_workspaces_finish(struct klest_server *server);
void klest_workspace_activate(struct klest_server *server, uint32_t index);
void klest_workspace_move_focused(struct klest_server *server, uint32_t index);
void klest_workspace_activate_toplevel(struct klest_server *server, uint32_t id);
void klest_workspace_minimize_toplevel(struct klest_server *server, uint32_t id);
void klest_workspace_toggle_maximize_toplevel(struct klest_server *server,
	uint32_t id);
void klest_workspace_minimize_focused(struct klest_server *server);
void klest_workspace_toggle_maximize_focused(struct klest_server *server);
void klest_workspace_toplevel_mapped(struct klest_toplevel *toplevel);
void klest_workspace_toplevel_unmapped(struct klest_toplevel *toplevel);
void klest_workspace_toplevel_changed(struct klest_toplevel *toplevel);
void klest_workspace_focus_changed(struct klest_server *server);

#endif /* KLEST_COMPOSITOR_WORKSPACE_H */

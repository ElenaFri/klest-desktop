/* Klest workspace model and Wayland control protocol. */
#ifndef KLEST_COMPOSITOR_WORKSPACE_H
#define KLEST_COMPOSITOR_WORKSPACE_H

#include <stdbool.h>
#include <stdint.h>

#define KLEST_WORKSPACE_COUNT 4U

struct klest_server;

bool klest_workspaces_init(struct klest_server *server);
void klest_workspaces_finish(struct klest_server *server);
void klest_workspace_activate(struct klest_server *server, uint32_t index);
void klest_workspace_move_focused(struct klest_server *server, uint32_t index);

#endif /* KLEST_COMPOSITOR_WORKSPACE_H */

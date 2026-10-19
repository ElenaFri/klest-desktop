/* Fixed-size workspace model and its small Wayland control protocol. */
#include "workspace.h"

#include <stdlib.h>

#include <wayland-server-core.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>

#include "view.h"
#include "klest-workspace-v1-server-protocol.h"

struct klest_workspace_binding {
	struct wl_list link;
	struct klest_server *server;
	struct wl_resource *resource;
};

static struct klest_toplevel *focused_toplevel(struct klest_server *server)
{
	struct wlr_surface *surface = server->seat->keyboard_state.focused_surface;
	if (surface == NULL) {
		return NULL;
	}
	surface = wlr_surface_get_root_surface(surface);

	struct klest_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->toplevels, link) {
		if (toplevel->xdg_toplevel->base->surface == surface) {
			return toplevel;
		}
	}
	return NULL;
}

static void clear_focus(struct klest_server *server)
{
	struct klest_toplevel *focused = focused_toplevel(server);
	if (focused != NULL) {
		wlr_xdg_toplevel_set_activated(focused->xdg_toplevel, false);
	}
	wlr_seat_keyboard_notify_clear_focus(server->seat);
	wlr_seat_pointer_clear_focus(server->seat);
}

static void focus_active_workspace(struct klest_server *server)
{
	struct klest_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->toplevels, link) {
		if (toplevel->workspace == server->workspaces.active) {
			klest_focus_toplevel(toplevel);
			return;
		}
	}
}

static void broadcast_state(struct klest_server *server)
{
	struct klest_workspace_binding *binding;
	wl_list_for_each(binding, &server->workspaces.bindings, link) {
		klest_workspace_manager_v1_send_state(binding->resource,
			KLEST_WORKSPACE_COUNT, server->workspaces.active);
	}
}

static void workspace_binding_destroy(struct wl_resource *resource)
{
	struct klest_workspace_binding *binding =
		wl_resource_get_user_data(resource);
	wl_list_remove(&binding->link);
	free(binding);
}

static void workspace_manager_destroy(struct wl_client *client,
	struct wl_resource *resource)
{
	(void)client;
	wl_resource_destroy(resource);
}

static void workspace_manager_activate(struct wl_client *client,
	struct wl_resource *resource, uint32_t index)
{
	(void)client;
	struct klest_workspace_binding *binding =
		wl_resource_get_user_data(resource);
	klest_workspace_activate(binding->server, index);
}

static const struct klest_workspace_manager_v1_interface workspace_manager_impl = {
	.destroy = workspace_manager_destroy,
	.activate = workspace_manager_activate,
};

static void workspace_manager_bind(struct wl_client *client, void *data,
	uint32_t version, uint32_t id)
{
	struct klest_server *server = data;
	struct wl_resource *resource = wl_resource_create(client,
		&klest_workspace_manager_v1_interface, version, id);
	if (resource == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	struct klest_workspace_binding *binding = calloc(1, sizeof(*binding));
	if (binding == NULL) {
		wl_resource_destroy(resource);
		wl_client_post_no_memory(client);
		return;
	}
	binding->server = server;
	binding->resource = resource;
	wl_resource_set_implementation(resource, &workspace_manager_impl,
		binding, workspace_binding_destroy);
	wl_list_insert(&server->workspaces.bindings, &binding->link);
	klest_workspace_manager_v1_send_state(resource, KLEST_WORKSPACE_COUNT,
		server->workspaces.active);
}

bool klest_workspaces_init(struct klest_server *server)
{
	server->workspaces.active = 0;
	wl_list_init(&server->workspaces.bindings);
	server->workspaces.global = wl_global_create(server->display,
		&klest_workspace_manager_v1_interface, 1, server,
		workspace_manager_bind);
	return server->workspaces.global != NULL;
}

void klest_workspaces_finish(struct klest_server *server)
{
	if (server->workspaces.global != NULL) {
		wl_global_destroy(server->workspaces.global);
		server->workspaces.global = NULL;
	}
}

void klest_workspace_activate(struct klest_server *server, uint32_t index)
{
	if (index >= KLEST_WORKSPACE_COUNT ||
			index == server->workspaces.active) {
		return;
	}

	clear_focus(server);
	server->workspaces.active = index;

	struct klest_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->toplevels, link) {
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node,
			toplevel->workspace == index);
	}
	focus_active_workspace(server);
	broadcast_state(server);
}

void klest_workspace_move_focused(struct klest_server *server, uint32_t index)
{
	if (index >= KLEST_WORKSPACE_COUNT ||
			index == server->workspaces.active) {
		return;
	}

	struct klest_toplevel *toplevel = focused_toplevel(server);
	if (toplevel == NULL) {
		return;
	}

	clear_focus(server);
	toplevel->workspace = index;
	wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
	focus_active_workspace(server);
}

/*
 * Klest — xdg-shell window ("toplevel") management.
 *
 * Maps client toplevels into the scene graph, tracks focus, and implements
 * interactive move/resize grabs.
 */

#include "view.h"
#include "workspace.h"
#include "placement.h"

#include <stdlib.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>

/* A popup only needs to live inside the scene graph; no extra bookkeeping. */
struct klest_popup {
	struct wlr_xdg_popup *xdg_popup;
	struct wl_listener commit;
	struct wl_listener destroy;
};

static struct klest_output *output_at_cursor(struct klest_server *server)
{
	if (server->cursor != NULL) {
		struct wlr_output *wlr_output = wlr_output_layout_output_at(
			server->output_layout, server->cursor->x, server->cursor->y);
		if (wlr_output != NULL && wlr_output->data != NULL) {
			return wlr_output->data;
		}
	}

	if (wl_list_empty(&server->outputs)) {
		return NULL;
	}
	struct klest_output *output =
		wl_container_of(server->outputs.next, output, link);
	return output;
}

static void place_toplevel_in_usable_area(struct klest_toplevel *toplevel)
{
	struct klest_output *output = output_at_cursor(toplevel->server);
	if (output == NULL) {
		return;
	}

	struct wlr_box geometry;
	wlr_xdg_surface_get_geometry(toplevel->xdg_toplevel->base, &geometry);
	int scene_x;
	int scene_y;
	if (klest_window_initial_position(&output->usable_area, &geometry,
			&scene_x, &scene_y)) {
		wlr_scene_node_set_position(&toplevel->scene_tree->node,
			scene_x, scene_y);
	}
}

void klest_focus_toplevel(struct klest_toplevel *toplevel)
{
	if (toplevel == NULL || toplevel->minimized || !toplevel->mapped ||
			toplevel->workspace != toplevel->server->workspaces.active) {
		return;
	}

	struct klest_server *server = toplevel->server;
	struct wlr_seat *seat = server->seat;
	struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;
	struct wlr_surface *surface = toplevel->xdg_toplevel->base->surface;

	if (prev_surface == surface) {
		return;
	}

	if (prev_surface != NULL) {
		struct wlr_xdg_toplevel *prev =
			wlr_xdg_toplevel_try_from_wlr_surface(prev_surface);
		if (prev != NULL) {
			wlr_xdg_toplevel_set_activated(prev, false);
		}
	}

	wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
	wl_list_remove(&toplevel->link);
	wl_list_insert(&server->toplevels, &toplevel->link);
	wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, true);

	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
	if (keyboard != NULL) {
		wlr_seat_keyboard_notify_enter(seat, surface, keyboard->keycodes,
			keyboard->num_keycodes, &keyboard->modifiers);
	}
	klest_workspace_focus_changed(server);
}

static bool toplevel_is_focused(const struct klest_toplevel *toplevel)
{
	struct wlr_surface *focused = toplevel->server->seat->keyboard_state.focused_surface;
	return focused != NULL && wlr_surface_get_root_surface(focused) ==
		toplevel->xdg_toplevel->base->surface;
}

static void focus_next_toplevel(struct klest_server *server,
	const struct klest_toplevel *excluded)
{
	struct klest_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->toplevels, link) {
		if (toplevel != excluded && !toplevel->minimized &&
				toplevel->workspace == server->workspaces.active) {
			klest_focus_toplevel(toplevel);
			return;
		}
	}
	wlr_seat_keyboard_notify_clear_focus(server->seat);
}

void klest_toplevel_set_minimized(struct klest_toplevel *toplevel, bool minimized)
{
	if (toplevel->minimized == minimized) {
		return;
	}
	struct klest_server *server = toplevel->server;
	bool was_focused = toplevel_is_focused(toplevel);
	if (minimized && was_focused) {
		wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, false);
		wlr_seat_keyboard_notify_clear_focus(server->seat);
		wlr_seat_pointer_clear_focus(server->seat);
	}
	toplevel->minimized = minimized;
	bool visible = toplevel->mapped && !minimized &&
		toplevel->workspace == server->workspaces.active;
	wlr_scene_node_set_enabled(&toplevel->scene_tree->node, visible);
	if (minimized && was_focused) {
		focus_next_toplevel(server, toplevel);
	}
	klest_workspace_toplevel_changed(toplevel);
	klest_workspace_focus_changed(server);
}

static struct klest_output *output_for_toplevel(struct klest_toplevel *toplevel,
	const struct wlr_box *geometry)
{
	struct klest_server *server = toplevel->server;
	double center_x = toplevel->scene_tree->node.x + geometry->x +
		geometry->width / 2.0;
	double center_y = toplevel->scene_tree->node.y + geometry->y +
		geometry->height / 2.0;
	struct wlr_output *wlr_output = wlr_output_layout_output_at(
		server->output_layout, center_x, center_y);
	if (wlr_output != NULL && wlr_output->data != NULL) {
		return wlr_output->data;
	}
	return output_at_cursor(server);
}

/* Aligns a maximized toplevel's visible geometry with the output's usable area
 * (i.e. just below reserved panels). Called on every commit while maximized, so
 * the window stays flush even after the client changes its geometry offset in
 * response to the maximized size (e.g. CSD clients adjusting their margins). */
static void snap_maximized_to_usable_area(struct klest_toplevel *toplevel)
{
	struct wlr_box geometry;
	wlr_xdg_surface_get_geometry(toplevel->xdg_toplevel->base, &geometry);
	struct klest_output *output = output_for_toplevel(toplevel, &geometry);
	if (output == NULL) {
		return;
	}
	/* The scene node origin is the window's geometry top-left (see
	 * wlr_scene_xdg_surface_create), so place it straight at the usable area
	 * without re-applying the geometry offset. */
	wlr_scene_node_set_position(&toplevel->scene_tree->node,
		output->usable_area.x, output->usable_area.y);
}

void klest_toplevel_set_maximized(struct klest_toplevel *toplevel, bool maximized)
{
	struct wlr_xdg_toplevel *xdg_toplevel = toplevel->xdg_toplevel;
	bool changed = toplevel->maximized != maximized;
	struct wlr_box geometry;
	wlr_xdg_surface_get_geometry(xdg_toplevel->base, &geometry);

	if (changed && maximized) {
		toplevel->restore_geometry = (struct wlr_box){
			.x = toplevel->scene_tree->node.x,
			.y = toplevel->scene_tree->node.y,
			.width = geometry.width,
			.height = geometry.height,
		};
		toplevel->restore_geometry_valid = true;
		struct klest_output *output = output_for_toplevel(toplevel, &geometry);
		if (output != NULL) {
			wlr_scene_node_set_position(&toplevel->scene_tree->node,
				output->usable_area.x, output->usable_area.y);
			wlr_xdg_toplevel_set_size(xdg_toplevel,
				output->usable_area.width, output->usable_area.height);
		}
	} else if (changed && toplevel->restore_geometry_valid) {
		wlr_scene_node_set_position(&toplevel->scene_tree->node,
			toplevel->restore_geometry.x, toplevel->restore_geometry.y);
		wlr_xdg_toplevel_set_size(xdg_toplevel,
			toplevel->restore_geometry.width, toplevel->restore_geometry.height);
		toplevel->restore_geometry_valid = false;
	}

	toplevel->maximized = maximized;
	wlr_xdg_toplevel_set_maximized(xdg_toplevel, maximized);
	if (toplevel->mapped) {
		klest_workspace_toplevel_changed(toplevel);
	}
}

void klest_toplevel_toggle_maximized(struct klest_toplevel *toplevel)
{
	klest_toplevel_set_maximized(toplevel, !toplevel->maximized);
}

struct klest_toplevel *klest_toplevel_at(struct klest_server *server,
	double lx, double ly, struct wlr_surface **surface, double *sx, double *sy)
{
	struct wlr_scene_node *node =
		wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);
	if (node == NULL || node->type != WLR_SCENE_NODE_BUFFER) {
		return NULL;
	}

	struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
	struct wlr_scene_surface *scene_surface =
		wlr_scene_surface_try_from_buffer(scene_buffer);
	if (scene_surface == NULL) {
		return NULL;
	}
	*surface = scene_surface->surface;

	/* Walk up to the toplevel's scene tree, which carries a back-pointer. */
	struct wlr_scene_tree *tree = node->parent;
	while (tree != NULL && tree->node.data == NULL) {
		tree = tree->node.parent;
	}
	return tree != NULL ? tree->node.data : NULL;
}

void klest_reset_cursor_mode(struct klest_server *server)
{
	server->cursor_mode = KLEST_CURSOR_PASSTHROUGH;
	server->grabbed_toplevel = NULL;
}

void klest_process_cursor_move(struct klest_server *server)
{
	struct klest_toplevel *toplevel = server->grabbed_toplevel;
	wlr_scene_node_set_position(&toplevel->scene_tree->node,
		server->cursor->x - server->grab_x,
		server->cursor->y - server->grab_y);
}

void klest_process_cursor_resize(struct klest_server *server)
{
	struct klest_toplevel *toplevel = server->grabbed_toplevel;
	double border_x = server->cursor->x - server->grab_x;
	double border_y = server->cursor->y - server->grab_y;

	int new_left = server->grab_geobox.x;
	int new_right = server->grab_geobox.x + server->grab_geobox.width;
	int new_top = server->grab_geobox.y;
	int new_bottom = server->grab_geobox.y + server->grab_geobox.height;

	if (server->resize_edges & WLR_EDGE_TOP) {
		new_top = border_y;
		if (new_top >= new_bottom) {
			new_top = new_bottom - 1;
		}
	} else if (server->resize_edges & WLR_EDGE_BOTTOM) {
		new_bottom = border_y;
		if (new_bottom <= new_top) {
			new_bottom = new_top + 1;
		}
	}
	if (server->resize_edges & WLR_EDGE_LEFT) {
		new_left = border_x;
		if (new_left >= new_right) {
			new_left = new_right - 1;
		}
	} else if (server->resize_edges & WLR_EDGE_RIGHT) {
		new_right = border_x;
		if (new_right <= new_left) {
			new_right = new_left + 1;
		}
	}

	struct wlr_box geo_box;
	wlr_xdg_surface_get_geometry(toplevel->xdg_toplevel->base, &geo_box);
	wlr_scene_node_set_position(&toplevel->scene_tree->node,
		new_left - geo_box.x, new_top - geo_box.y);

	wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel,
		new_right - new_left, new_bottom - new_top);
}

void klest_begin_interactive(struct klest_toplevel *toplevel,
	enum klest_cursor_mode mode, uint32_t edges)
{
	struct klest_server *server = toplevel->server;
	struct wlr_surface *focused =
		server->seat->pointer_state.focused_surface;

	/* Deny the grab request if the toplevel is not actually focused. */
	if (focused == NULL ||
			toplevel->xdg_toplevel->base->surface !=
				wlr_surface_get_root_surface(focused)) {
		return;
	}

	server->grabbed_toplevel = toplevel;
	server->cursor_mode = mode;

	if (mode == KLEST_CURSOR_MOVE) {
		server->grab_x = server->cursor->x - toplevel->scene_tree->node.x;
		server->grab_y = server->cursor->y - toplevel->scene_tree->node.y;
	} else {
		struct wlr_box geo_box;
		wlr_xdg_surface_get_geometry(toplevel->xdg_toplevel->base, &geo_box);

		double border_x = (toplevel->scene_tree->node.x + geo_box.x) +
			((edges & WLR_EDGE_RIGHT) ? geo_box.width : 0);
		double border_y = (toplevel->scene_tree->node.y + geo_box.y) +
			((edges & WLR_EDGE_BOTTOM) ? geo_box.height : 0);
		server->grab_x = server->cursor->x - border_x;
		server->grab_y = server->cursor->y - border_y;

		server->grab_geobox = geo_box;
		server->grab_geobox.x += toplevel->scene_tree->node.x;
		server->grab_geobox.y += toplevel->scene_tree->node.y;
		server->resize_edges = edges;
	}
}

static void xdg_toplevel_map(struct wl_listener *listener, void *data)
{
	struct klest_toplevel *toplevel = wl_container_of(listener, toplevel, map);
	(void)data;

	wl_list_insert(&toplevel->server->toplevels, &toplevel->link);
	toplevel->mapped = true;
	bool visible = !toplevel->minimized && toplevel->workspace ==
		toplevel->server->workspaces.active;
	wlr_scene_node_set_enabled(&toplevel->scene_tree->node, visible);
	if (visible) {
		place_toplevel_in_usable_area(toplevel);
		klest_focus_toplevel(toplevel);
	}
	klest_workspace_toplevel_mapped(toplevel);
}

static void xdg_toplevel_unmap(struct wl_listener *listener, void *data)
{
	struct klest_toplevel *toplevel = wl_container_of(listener, toplevel, unmap);
	(void)data;

	if (toplevel == toplevel->server->grabbed_toplevel) {
		klest_reset_cursor_mode(toplevel->server);
	}
	if (toplevel->mapped) {
		klest_workspace_toplevel_unmapped(toplevel);
		toplevel->mapped = false;
		wl_list_remove(&toplevel->link);
	}
}

static void xdg_toplevel_commit(struct wl_listener *listener, void *data)
{
	struct klest_toplevel *toplevel = wl_container_of(listener, toplevel, commit);
	(void)data;

	/* On the first commit, let the client pick its own size. */
	if (toplevel->xdg_toplevel->base->initial_commit) {
		wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, 0, 0);
	}

	/* Keep a maximized window flush against the usable area: the client may
	 * commit a different geometry offset once it applies the maximized size. */
	if (toplevel->maximized) {
		snap_maximized_to_usable_area(toplevel);
	}
}

static void xdg_toplevel_destroy(struct wl_listener *listener, void *data)
{
	struct klest_toplevel *toplevel = wl_container_of(listener, toplevel, destroy);
	(void)data;

	wl_list_remove(&toplevel->map.link);
	wl_list_remove(&toplevel->unmap.link);
	wl_list_remove(&toplevel->commit.link);
	wl_list_remove(&toplevel->destroy.link);
	wl_list_remove(&toplevel->request_move.link);
	wl_list_remove(&toplevel->request_resize.link);
	wl_list_remove(&toplevel->request_maximize.link);
	wl_list_remove(&toplevel->request_minimize.link);
	wl_list_remove(&toplevel->request_fullscreen.link);
	wl_list_remove(&toplevel->set_title.link);
	free(toplevel);
}

static void xdg_toplevel_request_move(struct wl_listener *listener, void *data)
{
	struct klest_toplevel *toplevel =
		wl_container_of(listener, toplevel, request_move);
	(void)data;
	klest_begin_interactive(toplevel, KLEST_CURSOR_MOVE, 0);
}

static void xdg_toplevel_request_resize(struct wl_listener *listener, void *data)
{
	struct klest_toplevel *toplevel =
		wl_container_of(listener, toplevel, request_resize);
	struct wlr_xdg_toplevel_resize_event *event = data;
	klest_begin_interactive(toplevel, KLEST_CURSOR_RESIZE, event->edges);
}

static void xdg_toplevel_request_maximize(struct wl_listener *listener, void *data)
{
	struct klest_toplevel *toplevel =
		wl_container_of(listener, toplevel, request_maximize);
	(void)data;
	klest_toplevel_set_maximized(toplevel,
		toplevel->xdg_toplevel->requested.maximized);
}

static void xdg_toplevel_request_minimize(struct wl_listener *listener,
	void *data)
{
	struct klest_toplevel *toplevel =
		wl_container_of(listener, toplevel, request_minimize);
	(void)data;
	klest_toplevel_set_minimized(toplevel, true);
}

static void xdg_toplevel_set_title(struct wl_listener *listener, void *data)
{
	struct klest_toplevel *toplevel =
		wl_container_of(listener, toplevel, set_title);
	(void)data;
	klest_workspace_toplevel_changed(toplevel);
}

static void xdg_toplevel_request_fullscreen(struct wl_listener *listener, void *data)
{
	struct klest_toplevel *toplevel =
		wl_container_of(listener, toplevel, request_fullscreen);
	(void)data;
	if (toplevel->xdg_toplevel->base->initialized) {
		wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
	}
}

static void server_new_xdg_toplevel(struct wl_listener *listener, void *data)
{
	struct klest_server *server =
		wl_container_of(listener, server, new_xdg_toplevel);
	struct wlr_xdg_toplevel *xdg_toplevel = data;

	struct klest_toplevel *toplevel = calloc(1, sizeof(*toplevel));
	if (toplevel == NULL) {
		return;
	}
	toplevel->server = server;
	toplevel->xdg_toplevel = xdg_toplevel;
	toplevel->id = wl_display_next_serial(server->display);
	toplevel->workspace = server->workspaces.active;
	toplevel->scene_tree = wlr_scene_xdg_surface_create(
		server->toplevel_tree, xdg_toplevel->base);
	toplevel->scene_tree->node.data = toplevel;
	xdg_toplevel->base->data = toplevel->scene_tree;

	toplevel->map.notify = xdg_toplevel_map;
	wl_signal_add(&xdg_toplevel->base->surface->events.map, &toplevel->map);
	toplevel->unmap.notify = xdg_toplevel_unmap;
	wl_signal_add(&xdg_toplevel->base->surface->events.unmap, &toplevel->unmap);
	toplevel->commit.notify = xdg_toplevel_commit;
	wl_signal_add(&xdg_toplevel->base->surface->events.commit, &toplevel->commit);
	toplevel->destroy.notify = xdg_toplevel_destroy;
	wl_signal_add(&xdg_toplevel->events.destroy, &toplevel->destroy);
	toplevel->request_move.notify = xdg_toplevel_request_move;
	wl_signal_add(&xdg_toplevel->events.request_move, &toplevel->request_move);
	toplevel->request_resize.notify = xdg_toplevel_request_resize;
	wl_signal_add(&xdg_toplevel->events.request_resize, &toplevel->request_resize);
	toplevel->request_maximize.notify = xdg_toplevel_request_maximize;
	wl_signal_add(&xdg_toplevel->events.request_maximize,
		&toplevel->request_maximize);
	toplevel->request_minimize.notify = xdg_toplevel_request_minimize;
	wl_signal_add(&xdg_toplevel->events.request_minimize,
		&toplevel->request_minimize);
	toplevel->set_title.notify = xdg_toplevel_set_title;
	wl_signal_add(&xdg_toplevel->events.set_title, &toplevel->set_title);
	toplevel->request_fullscreen.notify = xdg_toplevel_request_fullscreen;
	wl_signal_add(&xdg_toplevel->events.request_fullscreen,
		&toplevel->request_fullscreen);
}

static void xdg_popup_commit(struct wl_listener *listener, void *data)
{
	struct klest_popup *popup = wl_container_of(listener, popup, commit);
	(void)data;

	if (popup->xdg_popup->base->initial_commit) {
		wlr_xdg_surface_schedule_configure(popup->xdg_popup->base);
	}
}

static void xdg_popup_destroy(struct wl_listener *listener, void *data)
{
	struct klest_popup *popup = wl_container_of(listener, popup, destroy);
	(void)data;

	wl_list_remove(&popup->commit.link);
	wl_list_remove(&popup->destroy.link);
	free(popup);
}

static void server_new_xdg_popup(struct wl_listener *listener, void *data)
{
	struct wlr_xdg_popup *xdg_popup = data;
	(void)listener;

	/* The parent is either another xdg surface or a layer surface; both store
	 * their scene tree in the wlr object's `data` field. */
	struct wlr_scene_tree *parent_tree = NULL;
	struct wlr_xdg_surface *xdg_parent =
		wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
	if (xdg_parent != NULL) {
		parent_tree = xdg_parent->data;
	} else {
		struct wlr_layer_surface_v1 *layer_parent =
			wlr_layer_surface_v1_try_from_wlr_surface(xdg_popup->parent);
		if (layer_parent != NULL) {
			parent_tree = layer_parent->data;
		}
	}
	if (parent_tree == NULL) {
		return;
	}

	struct klest_popup *popup = calloc(1, sizeof(*popup));
	if (popup == NULL) {
		return;
	}
	popup->xdg_popup = xdg_popup;
	xdg_popup->base->data =
		wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);

	popup->commit.notify = xdg_popup_commit;
	wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);
	popup->destroy.notify = xdg_popup_destroy;
	wl_signal_add(&xdg_popup->events.destroy, &popup->destroy);
}

void klest_views_init(struct klest_server *server)
{
	wl_list_init(&server->toplevels);

	server->xdg_shell = wlr_xdg_shell_create(server->display, 3);

	server->new_xdg_toplevel.notify = server_new_xdg_toplevel;
	wl_signal_add(&server->xdg_shell->events.new_toplevel,
		&server->new_xdg_toplevel);
	server->new_xdg_popup.notify = server_new_xdg_popup;
	wl_signal_add(&server->xdg_shell->events.new_popup, &server->new_xdg_popup);
}

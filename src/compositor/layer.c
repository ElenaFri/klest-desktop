/*
 * Klest — wlr-layer-shell support (ADR 0009).
 *
 * Lets standalone Wayland clients (backgrounds, panels, launchers) anchor
 * surfaces to the screen edges without being part of the compositor. The
 * compositor only places them in the right scene layer and reserves the
 * exclusive zones they ask for; everything else is the client's business.
 */

#include "server.h"

#include <stdlib.h>

#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>

struct klest_layer_surface {
	struct wl_list link; /* klest_server.layer_surfaces */
	struct klest_server *server;
	struct wlr_layer_surface_v1 *layer_surface;
	struct wlr_scene_layer_surface_v1 *scene;
	struct klest_output *output;
	enum zwlr_layer_shell_v1_layer layer; /* which scene tree it currently sits in */

	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener commit;
	struct wl_listener destroy;
};

static struct wlr_scene_tree *layer_tree_for(struct klest_server *server,
	enum zwlr_layer_shell_v1_layer layer)
{
	switch (layer) {
	case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
		return server->layers.background;
	case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
		return server->layers.bottom;
	case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
		return server->layers.top;
	case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
		return server->layers.overlay;
	}
	return server->layers.background;
}

void klest_arrange_output_layers(struct klest_output *output)
{
	struct klest_server *server = output->server;

	struct wlr_box full_area;
	wlr_output_layout_get_box(server->output_layout, output->wlr_output,
		&full_area);
	struct wlr_box usable_area = full_area;

	/* Reserve exclusive zones first (two passes), from the overlay layer down
	 * to the background, so higher layers claim their space before lower ones. */
	for (int exclusive = 1; exclusive >= 0; exclusive--) {
		for (int layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
				layer >= ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND; layer--) {
			struct klest_layer_surface *surface;
			wl_list_for_each(surface, &server->layer_surfaces, link) {
				struct wlr_layer_surface_v1 *wlr_surface =
					surface->layer_surface;
				if (surface->output != output || !wlr_surface->initialized) {
					continue;
				}
				if ((int)wlr_surface->current.layer != layer) {
					continue;
				}
				bool wants_exclusive =
					wlr_surface->current.exclusive_zone > 0;
				if (wants_exclusive != (exclusive == 1)) {
					continue;
				}
				wlr_scene_layer_surface_v1_configure(surface->scene,
					&full_area, &usable_area);
			}
		}
	}

	output->usable_area = usable_area;
}

static void layer_surface_map(struct wl_listener *listener, void *data)
{
	struct klest_layer_surface *surface =
		wl_container_of(listener, surface, map);
	(void)data;
	klest_arrange_output_layers(surface->output);
}

static void layer_surface_unmap(struct wl_listener *listener, void *data)
{
	struct klest_layer_surface *surface =
		wl_container_of(listener, surface, unmap);
	(void)data;
	klest_arrange_output_layers(surface->output);
}

static void layer_surface_commit(struct wl_listener *listener, void *data)
{
	struct klest_layer_surface *surface =
		wl_container_of(listener, surface, commit);
	(void)data;

	struct wlr_layer_surface_v1 *wlr_surface = surface->layer_surface;

	/* Follow the client if it moved itself to another layer. */
	if (wlr_surface->current.layer != surface->layer) {
		surface->layer = wlr_surface->current.layer;
		wlr_scene_node_reparent(&surface->scene->tree->node,
			layer_tree_for(surface->server, surface->layer));
	}

	klest_arrange_output_layers(surface->output);
}

static void layer_surface_destroy(struct wl_listener *listener, void *data)
{
	struct klest_layer_surface *surface =
		wl_container_of(listener, surface, destroy);
	(void)data;

	struct klest_output *output = surface->output;

	wl_list_remove(&surface->map.link);
	wl_list_remove(&surface->unmap.link);
	wl_list_remove(&surface->commit.link);
	wl_list_remove(&surface->destroy.link);
	wl_list_remove(&surface->link);
	free(surface);

	/* The output may itself be going away; only re-arrange if it is alive. */
	if (output != NULL && output->wlr_output != NULL) {
		klest_arrange_output_layers(output);
	}
}

static struct klest_output *pick_output(struct klest_server *server,
	struct wlr_layer_surface_v1 *wlr_surface)
{
	if (wlr_surface->output != NULL) {
		return wlr_surface->output->data;
	}
	/* The client left the choice to us: use the first available output. */
	if (wl_list_empty(&server->outputs)) {
		return NULL;
	}
	struct klest_output *output =
		wl_container_of(server->outputs.next, output, link);
	wlr_surface->output = output->wlr_output;
	return output;
}

static void server_new_layer_surface(struct wl_listener *listener, void *data)
{
	struct klest_server *server =
		wl_container_of(listener, server, new_layer_surface);
	struct wlr_layer_surface_v1 *wlr_surface = data;

	struct klest_output *output = pick_output(server, wlr_surface);
	if (output == NULL) {
		wlr_log(WLR_ERROR, "no output for layer surface '%s'; closing it",
			wlr_surface->namespace != NULL ? wlr_surface->namespace : "");
		wlr_layer_surface_v1_destroy(wlr_surface);
		return;
	}

	struct klest_layer_surface *surface = calloc(1, sizeof(*surface));
	if (surface == NULL) {
		wlr_layer_surface_v1_destroy(wlr_surface);
		return;
	}
	surface->server = server;
	surface->layer_surface = wlr_surface;
	surface->output = output;
	surface->layer = wlr_surface->pending.layer;
	surface->scene = wlr_scene_layer_surface_v1_create(
		layer_tree_for(server, surface->layer), wlr_surface);
	if (surface->scene == NULL) {
		free(surface);
		wlr_layer_surface_v1_destroy(wlr_surface);
		return;
	}
	/* Let popup parenting find our scene tree (see view.c). */
	wlr_surface->data = surface->scene->tree;

	surface->map.notify = layer_surface_map;
	wl_signal_add(&wlr_surface->surface->events.map, &surface->map);
	surface->unmap.notify = layer_surface_unmap;
	wl_signal_add(&wlr_surface->surface->events.unmap, &surface->unmap);
	surface->commit.notify = layer_surface_commit;
	wl_signal_add(&wlr_surface->surface->events.commit, &surface->commit);
	surface->destroy.notify = layer_surface_destroy;
	wl_signal_add(&wlr_surface->events.destroy, &surface->destroy);

	wl_list_insert(&server->layer_surfaces, &surface->link);
}

void klest_layers_output_destroyed(struct klest_server *server,
	struct wlr_output *wlr_output)
{
	struct klest_layer_surface *surface;
	struct klest_layer_surface *tmp;
	wl_list_for_each_safe(surface, tmp, &server->layer_surfaces, link) {
		if (surface->output != NULL &&
				surface->output->wlr_output == wlr_output) {
			/* Drop the dangling output pointer before the destroy handler
			 * runs, so it does not try to re-arrange a dead output. */
			surface->output = NULL;
			wlr_layer_surface_v1_destroy(surface->layer_surface);
		}
	}
}

void klest_layers_init(struct klest_server *server)
{
	wl_list_init(&server->layer_surfaces);

	server->layer_shell = wlr_layer_shell_v1_create(server->display, 4);
	server->new_layer_surface.notify = server_new_layer_surface;
	wl_signal_add(&server->layer_shell->events.new_surface,
		&server->new_layer_surface);
}

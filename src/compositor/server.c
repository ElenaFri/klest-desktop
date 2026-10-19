/*
 * Klest — Wayland compositor server (ADR 0002, 0004).
 *
 * Brings up the display, backend, renderer and scene, then wires in the window
 * (xdg-shell), layer-shell and input subsystems. Desktop backgrounds are
 * standalone layer-shell clients (ADR 0009).
 */

#define _POSIX_C_SOURCE 200809L

#include "server.h"

#include <signal.h>
#include <stdlib.h>
#include <time.h>

#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/util/log.h>



static int terminate_on_signal(int signal_number, void *data)
{
	(void)signal_number;
	struct klest_server *server = data;
	wl_display_terminate(server->display);
	return 0;
}

static void remove_termination_signals(struct klest_server *server)
{
	for (size_t i = 0; i < 2; i++) {
		if (server->termination_signals[i] != NULL) {
			wl_event_source_remove(server->termination_signals[i]);
			server->termination_signals[i] = NULL;
		}
	}
}

static void output_frame(struct wl_listener *listener, void *data)
{
	struct klest_output *output = wl_container_of(listener, output, frame);
	(void)data;

	struct wlr_scene_output *scene_output =
		wlr_scene_get_scene_output(output->server->scene, output->wlr_output);

	wlr_scene_output_commit(scene_output, NULL);

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	wlr_scene_output_send_frame_done(scene_output, &now);
}

static void output_request_state(struct wl_listener *listener, void *data)
{
	struct klest_output *output = wl_container_of(listener, output, request_state);
	const struct wlr_output_event_request_state *event = data;

	wlr_output_commit_state(output->wlr_output, event->state);
	klest_arrange_output_layers(output);
}

static void output_destroy(struct wl_listener *listener, void *data)
{
	struct klest_output *output = wl_container_of(listener, output, destroy);
	(void)data;

	klest_layers_output_destroyed(output->server, output->wlr_output);
	wl_list_remove(&output->frame.link);
	wl_list_remove(&output->request_state.link);
	wl_list_remove(&output->destroy.link);
	wl_list_remove(&output->link);
	free(output);
}

static void server_new_output(struct wl_listener *listener, void *data)
{
	struct klest_server *server = wl_container_of(listener, server, new_output);
	struct wlr_output *wlr_output = data;

	if (!wlr_output_init_render(wlr_output, server->allocator, server->renderer)) {
		wlr_log(WLR_ERROR, "failed to initialize output rendering");
		return;
	}

	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, true);

	struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
	if (mode != NULL) {
		wlr_output_state_set_mode(&state, mode);
	}

	bool committed = wlr_output_commit_state(wlr_output, &state);
	wlr_output_state_finish(&state);
	if (!committed) {
		wlr_log(WLR_ERROR, "failed to enable output");
		return;
	}

	struct klest_output *output = calloc(1, sizeof(*output));
	if (output == NULL) {
		wlr_log(WLR_ERROR, "failed to allocate output");
		return;
	}
	output->server = server;
	output->wlr_output = wlr_output;
	wlr_output->data = output;

	output->frame.notify = output_frame;
	wl_signal_add(&wlr_output->events.frame, &output->frame);
	output->request_state.notify = output_request_state;
	wl_signal_add(&wlr_output->events.request_state, &output->request_state);
	output->destroy.notify = output_destroy;
	wl_signal_add(&wlr_output->events.destroy, &output->destroy);

	wl_list_insert(&server->outputs, &output->link);

	struct wlr_output_layout_output *layout_output =
		wlr_output_layout_add_auto(server->output_layout, wlr_output);
	struct wlr_scene_output *scene_output =
		wlr_scene_output_create(server->scene, wlr_output);
	wlr_scene_output_layout_add_output(server->scene_layout, layout_output,
		scene_output);

	klest_arrange_output_layers(output);
}

bool klest_server_init(struct klest_server *server)
{
	wlr_log_init(WLR_INFO, NULL);

	server->display = wl_display_create();
	if (server->display == NULL) {
		wlr_log(WLR_ERROR, "failed to create Wayland display");
		return false;
	}

	struct wl_event_loop *loop = wl_display_get_event_loop(server->display);
	server->termination_signals[0] = wl_event_loop_add_signal(loop, SIGINT,
		terminate_on_signal, server);
	server->termination_signals[1] = wl_event_loop_add_signal(loop, SIGTERM,
		terminate_on_signal, server);
	if (server->termination_signals[0] == NULL ||
			server->termination_signals[1] == NULL) {
		wlr_log(WLR_ERROR, "failed to register termination signals");
		goto error_display;
	}

	server->backend = wlr_backend_autocreate(loop, NULL);
	if (server->backend == NULL) {
		wlr_log(WLR_ERROR, "failed to create backend");
		goto error_display;
	}

	server->renderer = wlr_renderer_autocreate(server->backend);
	if (server->renderer == NULL) {
		wlr_log(WLR_ERROR, "failed to create renderer");
		goto error_display;
	}
	if (!wlr_renderer_init_wl_display(server->renderer, server->display)) {
		wlr_log(WLR_ERROR, "failed to initialize renderer for Wayland display");
		goto error_display;
	}

	server->allocator = wlr_allocator_autocreate(server->backend, server->renderer);
	if (server->allocator == NULL) {
		wlr_log(WLR_ERROR, "failed to create allocator");
		goto error_display;
	}

	wlr_compositor_create(server->display, 5, server->renderer);
	wlr_subcompositor_create(server->display);
	wlr_data_device_manager_create(server->display);

	server->output_layout = wlr_output_layout_create(server->display);
	wl_list_init(&server->outputs);

	server->scene = wlr_scene_create();
	server->scene_layout =
		wlr_scene_attach_output_layout(server->scene, server->output_layout);

	/* Fixed z-order, bottom to top: background < bottom < windows < top <
	 * overlay (ADR 0009). */
	server->layers.background = wlr_scene_tree_create(&server->scene->tree);
	server->layers.bottom = wlr_scene_tree_create(&server->scene->tree);
	server->toplevel_tree = wlr_scene_tree_create(&server->scene->tree);
	server->layers.top = wlr_scene_tree_create(&server->scene->tree);
	server->layers.overlay = wlr_scene_tree_create(&server->scene->tree);

	server->new_output.notify = server_new_output;
	wl_signal_add(&server->backend->events.new_output, &server->new_output);

	klest_views_init(server);
	klest_layers_init(server);
	klest_input_init(server);

	return true;

error_display:
	remove_termination_signals(server);
	if (server->allocator != NULL) {
		wlr_allocator_destroy(server->allocator);
		server->allocator = NULL;
	}
	if (server->renderer != NULL) {
		wlr_renderer_destroy(server->renderer);
		server->renderer = NULL;
	}
	if (server->backend != NULL) {
		wlr_backend_destroy(server->backend);
		server->backend = NULL;
	}
	wl_display_destroy(server->display);
	server->display = NULL;
	return false;
}

bool klest_server_run(struct klest_server *server)
{
	const char *socket = wl_display_add_socket_auto(server->display);
	if (socket == NULL) {
		wlr_log(WLR_ERROR, "failed to create Wayland socket");
		return false;
	}

	if (!wlr_backend_start(server->backend)) {
		wlr_log(WLR_ERROR, "failed to start backend");
		return false;
	}

	setenv("WAYLAND_DISPLAY", socket, true);
	wlr_log(WLR_INFO, "Klest running on WAYLAND_DISPLAY=%s", socket);

	wl_display_run(server->display);
	return true;
}

void klest_server_finish(struct klest_server *server)
{
	if (server->display == NULL) {
		return;
	}
	remove_termination_signals(server);
	wl_display_destroy_clients(server->display);

	wlr_scene_node_destroy(&server->scene->tree.node);
	wlr_xcursor_manager_destroy(server->cursor_mgr);
	wlr_cursor_destroy(server->cursor);
	wlr_allocator_destroy(server->allocator);
	wlr_renderer_destroy(server->renderer);
	wlr_backend_destroy(server->backend);
	wl_display_destroy(server->display);
	server->display = NULL;
}

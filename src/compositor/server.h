/* Klest — Wayland compositor server (ADR 0002, 0004). */

#ifndef KLEST_COMPOSITOR_SERVER_H
#define KLEST_COMPOSITOR_SERVER_H

#include <wayland-server-core.h>

struct klest_server {
	struct wl_display *display;
	struct wlr_backend *backend;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;

	struct wlr_output_layout *output_layout;
	struct wlr_scene *scene;
	struct wlr_scene_output_layout *scene_layout;

	struct wl_list outputs; /* klest_output.link */
	struct wl_listener new_output;
};

/* Sets up the display, backend, renderer and scene. Returns false on failure. */
bool klest_server_init(struct klest_server *server);

/* Adds a Wayland socket, starts the backend and runs the event loop until the
 * display terminates. Returns false if startup fails. */
bool klest_server_run(struct klest_server *server);

/* Tears down the server and releases its resources. */
void klest_server_finish(struct klest_server *server);

#endif /* KLEST_COMPOSITOR_SERVER_H */

/* Klest — Wayland compositor server (ADR 0002, 0004). */

#ifndef KLEST_COMPOSITOR_SERVER_H
#define KLEST_COMPOSITOR_SERVER_H

#include <stdbool.h>
#include <stdint.h>

#include <wayland-server-core.h>
#include <wlr/util/box.h>

enum klest_cursor_mode {
	KLEST_CURSOR_PASSTHROUGH,
	KLEST_CURSOR_MOVE,
	KLEST_CURSOR_RESIZE,
};

struct klest_toplevel;

struct klest_config;

struct wlr_output;
struct wlr_scene_tree;
struct wlr_layer_shell_v1;

/* A single output (monitor). Public so the layer-shell code can reserve its
 * usable area and react to the output being removed. */
struct klest_output {
	struct wl_list link; /* klest_server.outputs */
	struct klest_server *server;
	struct wlr_output *wlr_output;

	/* Area left after layer surfaces reserve exclusive zones (layout coords). */
	struct wlr_box usable_area;

	struct wl_listener frame;
	struct wl_listener request_state;
	struct wl_listener destroy;
};

/* Central compositor state. This struct is intentionally wide: it mirrors the
 * canonical wlroots compositor layout (cf. tinywl), where the server owns the
 * display, backend, renderer, scene, seat and the many wl_listener members that
 * wlroots drives via wl_container_of(). Splitting it into sub-structs would
 * break that offsetof-based dispatch for little real benefit, so the field
 * count is a deliberate, documented exception. */
struct klest_server {
	struct wl_display *display;
	struct wl_event_source *termination_signals[2];
	struct wlr_backend *backend;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;

	struct wlr_output_layout *output_layout;
	struct wlr_scene *scene;
	struct wlr_scene_output_layout *scene_layout;
	struct wl_list outputs; /* klest_output.link */
	struct wl_listener new_output;

	/* Scene layers, created bottom-to-top so their render order is fixed
	 * regardless of when surfaces are added (ADR 0009). Windows live in
	 * toplevel_tree, between the bottom and top layer-shell layers. */
	struct {
		struct wlr_scene_tree *background;
		struct wlr_scene_tree *bottom;
		struct wlr_scene_tree *top;
		struct wlr_scene_tree *overlay;
	} layers;
	struct wlr_scene_tree *toplevel_tree;

	/* Windows (xdg-shell). */
	struct wlr_xdg_shell *xdg_shell;
	struct wl_listener new_xdg_toplevel;
	struct wl_listener new_xdg_popup;
	struct wl_list toplevels; /* klest_toplevel.link, front-to-back */

	/* Desktop components (wlr-layer-shell): panels, backgrounds, etc. */
	struct wlr_layer_shell_v1 *layer_shell;
	struct wl_listener new_layer_surface;
	struct wl_list layer_surfaces; /* klest_layer_surface.link */

	/* Input (seat, keyboard, pointer). */
	struct wlr_seat *seat;
	struct wlr_cursor *cursor;
	struct wlr_xcursor_manager *cursor_mgr;
	struct wl_listener cursor_motion;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener cursor_button;
	struct wl_listener cursor_axis;
	struct wl_listener cursor_frame;
	struct wl_listener new_input;
	struct wl_listener request_cursor;
	struct wl_listener request_set_selection;
	struct wl_list keyboards; /* klest_keyboard.link */

	/* Interactive move/resize grab state. */
	enum klest_cursor_mode cursor_mode;
	struct klest_toplevel *grabbed_toplevel;
	double grab_x;
	double grab_y;
	struct wlr_box grab_geobox;
	uint32_t resize_edges;

	/* Runtime configuration (borrowed; owned by the caller). May be NULL,
	 * in which case built-in defaults apply. */
	const struct klest_config *config;
};

/* Sets up the display, backend, renderer, scene, windows and input. */
bool klest_server_init(struct klest_server *server);

/* Adds a Wayland socket, starts the backend and runs the event loop until the
 * display terminates. Returns false if startup fails. */
bool klest_server_run(struct klest_server *server);

/* Tears down the server and releases its resources. */
void klest_server_finish(struct klest_server *server);

/* Subsystem setup, called by klest_server_init(). */
void klest_views_init(struct klest_server *server);
void klest_input_init(struct klest_server *server);
void klest_layers_init(struct klest_server *server);

/* Recomputes the position and reserved area of every layer surface on the
 * given output. Called when outputs or layer surfaces change. */
void klest_arrange_output_layers(struct klest_output *output);

/* Closes any layer surface bound to an output that is being destroyed. */
void klest_layers_output_destroyed(struct klest_server *server,
	struct wlr_output *wlr_output);

#endif /* KLEST_COMPOSITOR_SERVER_H */

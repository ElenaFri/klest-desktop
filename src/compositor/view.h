/* Klest — xdg-shell window ("toplevel") management (ADR 0004, 0009). */

#ifndef KLEST_COMPOSITOR_VIEW_H
#define KLEST_COMPOSITOR_VIEW_H

#include <stdint.h>

#include <wayland-server-core.h>

#include "server.h"

struct wlr_surface;

struct klest_toplevel {
	struct wl_list link; /* klest_server.toplevels */
	struct klest_server *server;
	struct wlr_xdg_toplevel *xdg_toplevel;
	struct wlr_scene_tree *scene_tree;
	uint32_t workspace;

	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener commit;
	struct wl_listener destroy;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;
};

/* Gives keyboard focus (and raises) the given toplevel. NULL is a no-op. */
void klest_focus_toplevel(struct klest_toplevel *toplevel);

/* Returns the toplevel whose surface is at the given layout coordinates, along
 * with the surface and its surface-local coordinates. NULL if none. */
struct klest_toplevel *klest_toplevel_at(struct klest_server *server,
	double lx, double ly, struct wlr_surface **surface, double *sx, double *sy);

/* Starts an interactive move or resize grab driven by the cursor. */
void klest_begin_interactive(struct klest_toplevel *toplevel,
	enum klest_cursor_mode mode, uint32_t edges);

/* Applies the current cursor position to the active move/resize grab. */
void klest_process_cursor_move(struct klest_server *server);
void klest_process_cursor_resize(struct klest_server *server);

/* Ends any interactive grab. */
void klest_reset_cursor_mode(struct klest_server *server);

#endif /* KLEST_COMPOSITOR_VIEW_H */

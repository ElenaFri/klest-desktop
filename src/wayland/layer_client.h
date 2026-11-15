/* Shared client-side layer-shell setup and surface lifecycle. */
#ifndef KLEST_WAYLAND_LAYER_CLIENT_H
#define KLEST_WAYLAND_LAYER_CLIENT_H

#include <stdbool.h>
#include <stdint.h>

#include <wayland-client.h>

struct zwlr_layer_shell_v1;
struct zwlr_layer_surface_v1;

struct klest_layer_client {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct zwlr_layer_shell_v1 *shell;
	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *layer_surface;
	uint32_t width;
	uint32_t height;
	bool configured;
	bool closed;
	bool opaque; /* when set, present() marks the whole surface opaque */
};

/* Layer-surface placement request, passed to klest_layer_client_create_surface.
 * Width/height 0 means "let the compositor decide" for anchored edges. */
struct klest_layer_surface_spec {
	uint32_t layer;              /* ZWLR_LAYER_SHELL_V1_LAYER_* */
	const char *namespace;        /* compositor-side identifier */
	uint32_t anchors;            /* ZWLR_LAYER_SURFACE_V1_ANCHOR_* mask */
	uint32_t width;
	uint32_t height;
	int32_t exclusive_zone;      /* -1 = follow the compositor default */
	bool accepts_pointer_input;   /* false = click-through surface */
	bool keyboard_interactive;   /* request keyboard focus on map */
};

bool klest_layer_client_connect(struct klest_layer_client *client);
bool klest_layer_client_create_surface(struct klest_layer_client *client,
	const struct klest_layer_surface_spec *spec);
bool klest_layer_client_wait_configure(struct klest_layer_client *client);
/* Declares the surface fully opaque (or not), letting the compositor skip
 * compositing whatever sits behind it. Only call this for surfaces that fill
 * their buffer with opaque pixels. */
void klest_layer_client_set_opaque(struct klest_layer_client *client,
	bool opaque);
bool klest_layer_client_present(struct klest_layer_client *client,
	struct wl_buffer *buffer, uint32_t width, uint32_t height);
bool klest_layer_client_unmap(struct klest_layer_client *client);
void klest_layer_client_finish(struct klest_layer_client *client);

#endif /* KLEST_WAYLAND_LAYER_CLIENT_H */

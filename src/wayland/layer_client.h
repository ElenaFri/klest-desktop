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
};

bool klest_layer_client_connect(struct klest_layer_client *client);
bool klest_layer_client_create_surface(struct klest_layer_client *client,
	uint32_t layer, const char *namespace, uint32_t anchors,
	uint32_t width, uint32_t height, int32_t exclusive_zone,
	bool accepts_pointer_input, bool keyboard_interactive);
bool klest_layer_client_wait_configure(struct klest_layer_client *client);
bool klest_layer_client_present(struct klest_layer_client *client,
	struct wl_buffer *buffer, uint32_t width, uint32_t height);
bool klest_layer_client_unmap(struct klest_layer_client *client);
void klest_layer_client_finish(struct klest_layer_client *client);

#endif /* KLEST_WAYLAND_LAYER_CLIENT_H */

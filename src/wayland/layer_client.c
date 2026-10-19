/* Shared Wayland connection and wlr-layer-shell surface lifecycle. */
#include "wayland/layer_client.h"

#include <stdio.h>
#include <string.h>

#include <wayland-client-protocol.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

static uint32_t min_version(uint32_t advertised, uint32_t supported)
{
	return advertised < supported ? advertised : supported;
}

static void registry_global(void *data, struct wl_registry *registry,
	uint32_t name, const char *interface, uint32_t version)
{
	struct klest_layer_client *client = data;
	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		client->compositor = wl_registry_bind(registry, name,
			&wl_compositor_interface, min_version(version, 4));
	} else if (strcmp(interface, wl_shm_interface.name) == 0) {
		client->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	} else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
		client->shell = wl_registry_bind(registry, name,
			&zwlr_layer_shell_v1_interface, min_version(version, 4));
	}
}

static void registry_global_remove(void *data,
	struct wl_registry *registry, uint32_t name)
{
	(void)data;
	(void)registry;
	(void)name;
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_global,
	.global_remove = registry_global_remove,
};

static void layer_surface_configure(void *data,
	struct zwlr_layer_surface_v1 *layer_surface,
	uint32_t serial, uint32_t width, uint32_t height)
{
	struct klest_layer_client *client = data;
	zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
	client->width = width;
	client->height = height;
	client->configured = true;
}

static void layer_surface_closed(void *data,
	struct zwlr_layer_surface_v1 *layer_surface)
{
	struct klest_layer_client *client = data;
	(void)layer_surface;
	client->closed = true;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
	.configure = layer_surface_configure,
	.closed = layer_surface_closed,
};

bool klest_layer_client_connect(struct klest_layer_client *client)
{
	client->display = wl_display_connect(NULL);
	if (client->display == NULL) {
		fprintf(stderr, "klest-wayland: cannot connect to Wayland display\n");
		return false;
	}
	client->registry = wl_display_get_registry(client->display);
	if (client->registry == NULL || wl_registry_add_listener(client->registry,
			&registry_listener, client) != 0 ||
			wl_display_roundtrip(client->display) < 0) {
		fprintf(stderr, "klest-wayland: registry setup failed\n");
		return false;
	}
	if (client->compositor == NULL || client->shm == NULL ||
			client->shell == NULL) {
		fprintf(stderr, "klest-wayland: required Wayland protocol missing\n");
		return false;
	}
	return true;
}

bool klest_layer_client_create_surface(struct klest_layer_client *client,
	uint32_t layer, const char *namespace, uint32_t anchors,
	uint32_t width, uint32_t height, int32_t exclusive_zone,
	bool accepts_pointer_input)
{
	client->surface = wl_compositor_create_surface(client->compositor);
	if (client->surface == NULL) {
		fprintf(stderr, "klest-wayland: cannot create surface\n");
		return false;
	}
	if (!accepts_pointer_input) {
		struct wl_region *empty_input =
			wl_compositor_create_region(client->compositor);
		if (empty_input == NULL) {
			fprintf(stderr, "klest-wayland: cannot create input region\n");
			return false;
		}
		wl_surface_set_input_region(client->surface, empty_input);
		wl_region_destroy(empty_input);
	}

	client->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
		client->shell, client->surface, NULL, layer, namespace);
	if (client->layer_surface == NULL || zwlr_layer_surface_v1_add_listener(
			client->layer_surface, &layer_surface_listener, client) != 0) {
		fprintf(stderr, "klest-wayland: cannot create layer surface\n");
		return false;
	}
	zwlr_layer_surface_v1_set_anchor(client->layer_surface, anchors);
	zwlr_layer_surface_v1_set_size(client->layer_surface, width, height);
	zwlr_layer_surface_v1_set_exclusive_zone(client->layer_surface,
		exclusive_zone);
	wl_surface_commit(client->surface);
	return true;
}

bool klest_layer_client_wait_configure(struct klest_layer_client *client)
{
	for (int attempt = 0; attempt < 5 && !client->configured; attempt++) {
		if (wl_display_roundtrip(client->display) < 0) {
			fprintf(stderr, "klest-wayland: configure roundtrip failed\n");
			return false;
		}
	}
	if (!client->configured || client->width == 0 || client->height == 0) {
		fprintf(stderr, "klest-wayland: compositor sent no usable configure\n");
		return false;
	}
	return true;
}

bool klest_layer_client_present(struct klest_layer_client *client,
	struct wl_buffer *buffer, uint32_t width, uint32_t height)
{
	if (buffer == NULL || client->surface == NULL || width == 0 || height == 0 ||
			width > INT32_MAX || height > INT32_MAX) {
		fprintf(stderr, "klest-wayland: invalid surface buffer\n");
		return false;
	}
	wl_surface_attach(client->surface, buffer, 0, 0);
	wl_surface_damage(client->surface, 0, 0, (int32_t)width, (int32_t)height);
	wl_surface_commit(client->surface);
	if (wl_display_roundtrip(client->display) < 0) {
		fprintf(stderr, "klest-wayland: surface commit failed\n");
		return false;
	}
	return true;
}

bool klest_layer_client_unmap(struct klest_layer_client *client)
{
	if (client->surface == NULL) {
		return true;
	}
	wl_surface_attach(client->surface, NULL, 0, 0);
	wl_surface_commit(client->surface);
	if (wl_display_roundtrip(client->display) < 0) {
		fprintf(stderr, "klest-wayland: surface unmap failed\n");
		return false;
	}
	return true;
}

void klest_layer_client_finish(struct klest_layer_client *client)
{
	if (client->layer_surface != NULL) {
		zwlr_layer_surface_v1_destroy(client->layer_surface);
	}
	if (client->surface != NULL) {
		wl_surface_destroy(client->surface);
	}
	if (client->shell != NULL) {
		zwlr_layer_shell_v1_destroy(client->shell);
	}
	if (client->shm != NULL) {
		wl_shm_destroy(client->shm);
	}
	if (client->compositor != NULL) {
		wl_compositor_destroy(client->compositor);
	}
	if (client->registry != NULL) {
		wl_registry_destroy(client->registry);
	}
	if (client->display != NULL) {
		(void)wl_display_flush(client->display);
		wl_display_disconnect(client->display);
	}
	*client = (struct klest_layer_client){0};
}

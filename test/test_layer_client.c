/* Small wlr-layer-shell client for the headless compositor integration test.
 *
 * Reproduces what a background component does: create a surface, turn it into a
 * background-layer surface anchored to all edges, wait for the compositor's
 * configure, map a buffer, then tear everything down cleanly. */
#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <wayland-client.h>
#include <wayland-client-protocol.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

struct test_client {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct zwlr_layer_shell_v1 *layer_shell;
	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *layer_surface;
	struct wl_buffer *buffer;
	int32_t width;
	int32_t height;
	bool configured;
};

static void layer_surface_configure(void *data,
	struct zwlr_layer_surface_v1 *layer_surface,
	uint32_t serial, uint32_t width, uint32_t height)
{
	struct test_client *client = data;
	zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
	client->width = (width > 0) ? (int32_t)width : 32;
	client->height = (height > 0) ? (int32_t)height : 32;
	client->configured = true;
}

static void layer_surface_closed(void *data,
	struct zwlr_layer_surface_v1 *layer_surface) // NOSONAR: protocol callback ABI requires this type.
{
	(void)data;
	(void)layer_surface;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
	.configure = layer_surface_configure,
	.closed = layer_surface_closed,
};

static uint32_t min_version(uint32_t advertised, uint32_t supported)
{
	return advertised < supported ? advertised : supported;
}

static void registry_global(void *data, struct wl_registry *registry,
	uint32_t name, const char *interface, uint32_t version)
{
	struct test_client *client = data;
	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		client->compositor = wl_registry_bind(registry, name,
			&wl_compositor_interface, min_version(version, 4));
	} else if (strcmp(interface, wl_shm_interface.name) == 0) {
		client->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	} else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
		client->layer_shell = wl_registry_bind(registry, name,
			&zwlr_layer_shell_v1_interface, min_version(version, 4));
	}
}

static void registry_global_remove(void *data,
	struct wl_registry *registry, // NOSONAR: protocol callback ABI requires this type.
	uint32_t name)
{
	(void)data;
	(void)registry;
	(void)name;
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_global,
	.global_remove = registry_global_remove,
};

static struct wl_buffer *create_test_buffer(struct wl_shm *shm,
	int32_t width, int32_t height)
{
	const int32_t stride = width * 4;
	const int32_t size = stride * height;
	const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
	if (runtime_dir == NULL) {
		fprintf(stderr, "layer client: XDG_RUNTIME_DIR is unset\n");
		return NULL;
	}
	char path[4096];
	int path_len = snprintf(path, sizeof(path), "%s/klest-shm-XXXXXX",
		runtime_dir);
	if (path_len < 0 || (size_t)path_len >= sizeof(path)) {
		fprintf(stderr, "layer client: SHM path too long\n");
		return NULL;
	}
	int fd = mkstemp(path);
	if (fd < 0) {
		perror("mkstemp");
		return NULL;
	}
	(void)unlink(path);
	if (ftruncate(fd, size) != 0) {
		perror("ftruncate");
		close(fd);
		return NULL;
	}

	struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
	close(fd);
	if (pool == NULL) {
		return NULL;
	}
	struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height,
		stride, WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);
	return buffer;
}

static void destroy_client(struct test_client *client)
{
	if (client->layer_surface != NULL) {
		zwlr_layer_surface_v1_destroy(client->layer_surface);
	}
	if (client->surface != NULL) {
		wl_surface_destroy(client->surface);
	}
	if (client->buffer != NULL) {
		wl_buffer_destroy(client->buffer);
	}
	if (client->layer_shell != NULL) {
		zwlr_layer_shell_v1_destroy(client->layer_shell);
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
		(void)wl_display_roundtrip(client->display);
		wl_display_disconnect(client->display);
	}
}

static bool connect_client(struct test_client *client)
{
	client->display = wl_display_connect(NULL);
	if (client->display == NULL) {
		fprintf(stderr, "layer client: cannot connect to compositor\n");
		return false;
	}
	client->registry = wl_display_get_registry(client->display);
	if (client->registry == NULL || wl_registry_add_listener(client->registry,
			&registry_listener, client) != 0 ||
			wl_display_roundtrip(client->display) < 0) {
		fprintf(stderr, "layer client: registry roundtrip failed\n");
		return false;
	}
	if (client->compositor == NULL || client->shm == NULL ||
			client->layer_shell == NULL) {
		fprintf(stderr, "layer client: required Wayland global missing\n");
		return false;
	}
	return true;
}

static bool wait_for_configure(struct test_client *client)
{
	for (int attempt = 0; attempt < 5 && !client->configured; attempt++) {
		if (wl_display_roundtrip(client->display) < 0) {
			fprintf(stderr, "layer client: configure roundtrip failed\n");
			return false;
		}
	}
	if (!client->configured) {
		fprintf(stderr, "layer client: compositor sent no configure\n");
		return false;
	}
	return true;
}

static bool exercise_layer_surface(struct test_client *client)
{
	client->surface = wl_compositor_create_surface(client->compositor);
	if (client->surface == NULL) {
		fprintf(stderr, "layer client: cannot create surface\n");
		return false;
	}
	client->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
		client->layer_shell, client->surface, NULL,
		ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND, "klest-test");
	if (client->layer_surface == NULL || zwlr_layer_surface_v1_add_listener(
			client->layer_surface, &layer_surface_listener, client) != 0) {
		fprintf(stderr, "layer client: cannot create layer surface\n");
		return false;
	}

	/* Background semantics: fill the output, below everything else. */
	zwlr_layer_surface_v1_set_anchor(client->layer_surface,
		ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
		ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
		ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
		ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
	zwlr_layer_surface_v1_set_exclusive_zone(client->layer_surface, -1);
	zwlr_layer_surface_v1_set_size(client->layer_surface, 0, 0);

	wl_surface_commit(client->surface);
	if (!wait_for_configure(client)) {
		return false;
	}

	client->buffer = create_test_buffer(client->shm, client->width,
		client->height);
	if (client->buffer == NULL) {
		fprintf(stderr, "layer client: cannot create SHM buffer\n");
		return false;
	}
	wl_surface_attach(client->surface, client->buffer, 0, 0);
	wl_surface_damage(client->surface, 0, 0, client->width, client->height);
	wl_surface_commit(client->surface);
	if (wl_display_roundtrip(client->display) < 0) {
		fprintf(stderr, "layer client: map roundtrip failed\n");
		return false;
	}

	wl_surface_attach(client->surface, NULL, 0, 0);
	wl_surface_commit(client->surface);
	if (wl_display_roundtrip(client->display) < 0) {
		fprintf(stderr, "layer client: unmap roundtrip failed\n");
		return false;
	}
	return true;
}

int main(void)
{
	struct test_client client = {0};
	bool ok = connect_client(&client) && exercise_layer_surface(&client);
	destroy_client(&client);
	return ok ? 0 : 1;
}

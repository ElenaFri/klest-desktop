/* Small xdg-shell client used by the headless compositor integration test. */
#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <wayland-client.h>
#include <wayland-client-protocol.h>

#include "xdg-shell-client-protocol.h"

struct test_client {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct xdg_wm_base *wm_base;
	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *toplevel;
	struct wl_buffer *buffer;
	bool configured;
};

static void wm_base_ping(void *data, struct xdg_wm_base *wm_base,
	uint32_t serial)
{
	(void)data;
	xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
	.ping = wm_base_ping,
};

static void surface_configure(void *data, struct xdg_surface *xdg_surface,
	uint32_t serial)
{
	struct test_client *client = data;
	xdg_surface_ack_configure(xdg_surface, serial);
	client->configured = true;
}

static const struct xdg_surface_listener xdg_surface_listener = {
	.configure = surface_configure,
};

static void toplevel_configure(void *data,
	struct xdg_toplevel *toplevel, // NOSONAR: protocol callback ABI requires this type.
	int32_t width, int32_t height,
	struct wl_array *states) // NOSONAR: protocol callback ABI requires this type.
{
	(void)data;
	(void)toplevel;
	(void)width;
	(void)height;
	(void)states;
}

static void toplevel_close(void *data,
	struct xdg_toplevel *toplevel) // NOSONAR: protocol callback ABI requires this type.
{
	(void)data;
	(void)toplevel;
}

static const struct xdg_toplevel_listener toplevel_listener = {
	.configure = toplevel_configure,
	.close = toplevel_close,
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
		client->shm = wl_registry_bind(registry, name,
			&wl_shm_interface, 1);
	} else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
		client->wm_base = wl_registry_bind(registry, name,
			&xdg_wm_base_interface, min_version(version, 3));
		if (client->wm_base != NULL) {
			xdg_wm_base_add_listener(client->wm_base,
				&wm_base_listener, client);
		}
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

static struct wl_buffer *create_test_buffer(struct wl_shm *shm)
{
	const int width = 32;
	const int height = 32;
	const int stride = width * 4;
	const int size = stride * height;
	const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
	if (runtime_dir == NULL) {
		fprintf(stderr, "integration client: XDG_RUNTIME_DIR is unset\n");
		return NULL;
	}
	char path[4096];
	int path_len = snprintf(path, sizeof(path), "%s/klest-shm-XXXXXX",
		runtime_dir);
	if (path_len < 0 || (size_t)path_len >= sizeof(path)) {
		fprintf(stderr, "integration client: SHM path too long\n");
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
	if (client->toplevel != NULL) {
		xdg_toplevel_destroy(client->toplevel);
	}
	if (client->xdg_surface != NULL) {
		xdg_surface_destroy(client->xdg_surface);
	}
	if (client->surface != NULL) {
		wl_surface_destroy(client->surface);
	}
	if (client->buffer != NULL) {
		wl_buffer_destroy(client->buffer);
	}
	if (client->wm_base != NULL) {
		xdg_wm_base_destroy(client->wm_base);
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
		fprintf(stderr, "integration client: cannot connect to compositor\n");
		return false;
	}
	client->registry = wl_display_get_registry(client->display);
	if (client->registry == NULL || wl_registry_add_listener(client->registry,
			&registry_listener, client) != 0 ||
			wl_display_roundtrip(client->display) < 0) {
		fprintf(stderr, "integration client: registry roundtrip failed\n");
		return false;
	}
	if (client->compositor == NULL || client->shm == NULL ||
			client->wm_base == NULL) {
		fprintf(stderr, "integration client: required Wayland global missing\n");
		return false;
	}
	return true;
}

static bool wait_for_configure(struct test_client *client)
{
	for (int attempt = 0; attempt < 5 && !client->configured; attempt++) {
		if (wl_display_roundtrip(client->display) < 0) {
			fprintf(stderr, "integration client: configure roundtrip failed\n");
			return false;
		}
	}
	if (!client->configured) {
		fprintf(stderr, "integration client: compositor sent no configure\n");
		return false;
	}
	return true;
}

static bool exercise_toplevel(struct test_client *client)
{
	client->surface = wl_compositor_create_surface(client->compositor);
	if (client->surface == NULL) {
		fprintf(stderr, "integration client: cannot create surface\n");
		return false;
	}
	client->xdg_surface = xdg_wm_base_get_xdg_surface(client->wm_base,
		client->surface);
	if (client->xdg_surface == NULL || xdg_surface_add_listener(
			client->xdg_surface, &xdg_surface_listener, client) != 0) {
		fprintf(stderr, "integration client: cannot create xdg surface\n");
		return false;
	}
	client->toplevel = xdg_surface_get_toplevel(client->xdg_surface);
	if (client->toplevel == NULL || xdg_toplevel_add_listener(client->toplevel,
			&toplevel_listener, client) != 0) {
		fprintf(stderr, "integration client: cannot create xdg toplevel\n");
		return false;
	}

	xdg_toplevel_set_title(client->toplevel, "Klest integration test");
	wl_surface_commit(client->surface);
	if (!wait_for_configure(client)) {
		return false;
	}

	client->buffer = create_test_buffer(client->shm);
	if (client->buffer == NULL) {
		fprintf(stderr, "integration client: cannot create SHM buffer\n");
		return false;
	}
	wl_surface_attach(client->surface, client->buffer, 0, 0);
	wl_surface_damage(client->surface, 0, 0, 32, 32);
	wl_surface_commit(client->surface);
	if (wl_display_roundtrip(client->display) < 0) {
		fprintf(stderr, "integration client: map roundtrip failed\n");
		return false;
	}

	wl_surface_attach(client->surface, NULL, 0, 0);
	wl_surface_commit(client->surface);
	if (wl_display_roundtrip(client->display) < 0) {
		fprintf(stderr, "integration client: unmap roundtrip failed\n");
		return false;
	}
	return true;
}

int main(void)
{
	struct test_client client = {0};
	bool ok = connect_client(&client) && exercise_toplevel(&client);
	destroy_client(&client);
	return ok ? 0 : 1;
}

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
#include "klest-workspace-v1-client-protocol.h"
#include "../src/wayland/shm.h"

struct test_client {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct xdg_wm_base *wm_base;
	struct klest_workspace_manager_v1 *workspace_manager;
	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *toplevel;
	struct wl_buffer *buffer;
	uint32_t workspace_count;
	uint32_t active_workspace;
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

static void workspace_state(void *data,
	struct klest_workspace_manager_v1 *manager, // NOSONAR: protocol callback ABI requires this type.
	uint32_t count, uint32_t active)
{
	struct test_client *client = data;
	(void)manager;
	client->workspace_count = count;
	client->active_workspace = active;
}

static const struct klest_workspace_manager_v1_listener workspace_listener = {
	.state = workspace_state,
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
	} else if (strcmp(interface,
			klest_workspace_manager_v1_interface.name) == 0) {
		client->workspace_manager = wl_registry_bind(registry, name,
			&klest_workspace_manager_v1_interface, 1);
		if (client->workspace_manager != NULL) {
			klest_workspace_manager_v1_add_listener(
				client->workspace_manager, &workspace_listener, client);
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


static void destroy_client(struct test_client *client)
{
	if (client->workspace_manager != NULL) {
		klest_workspace_manager_v1_destroy(client->workspace_manager);
	}
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
			client->wm_base == NULL || client->workspace_manager == NULL) {
		fprintf(stderr, "integration client: required Wayland global missing\n");
		return false;
	}
	return true;
}

/* Checks the workspace global reports a sane state and honours activate(). */
static bool exercise_workspaces(struct test_client *client)
{
	if (wl_display_roundtrip(client->display) < 0) {
		fprintf(stderr, "integration client: workspace state roundtrip failed\n");
		return false;
	}
	if (client->workspace_count == 0 || client->active_workspace != 0) {
		fprintf(stderr, "integration client: unexpected initial workspace state\n");
		return false;
	}
	klest_workspace_manager_v1_activate(client->workspace_manager, 1);
	if (wl_display_roundtrip(client->display) < 0) {
		fprintf(stderr, "integration client: workspace activate roundtrip failed\n");
		return false;
	}
	if (client->active_workspace != 1) {
		fprintf(stderr, "integration client: activate did not switch workspace\n");
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

	client->buffer = klest_shm_buffer_create(client->shm, 32, 32,
			0xff404040U);
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
	bool ok = connect_client(&client) && exercise_toplevel(&client) &&
		exercise_workspaces(&client);
	destroy_client(&client);
	return ok ? 0 : 1;
}

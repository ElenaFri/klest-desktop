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
	uint32_t window_id;
	bool window_seen;
	bool window_closed;
	bool window_active;
	bool window_minimized;
	bool window_maximized;
	bool toplevel_activated;
	bool configured;
	bool popup_configured;
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
	struct test_client *client = data;
	(void)toplevel;
	(void)width;
	(void)height;
	/* Track the activated state: in the keyboard-less headless test it is the
	 * observable proof that switching workspaces reactivates the window. */
	client->toplevel_activated = false;
	const uint32_t *state;
	wl_array_for_each(state, states) {
		if (*state == XDG_TOPLEVEL_STATE_ACTIVATED) {
			client->toplevel_activated = true;
		}
	}
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

static void popup_surface_configure(void *data,
	struct xdg_surface *xdg_surface, uint32_t serial)
{
	struct test_client *client = data;
	xdg_surface_ack_configure(xdg_surface, serial);
	client->popup_configured = true;
}

static const struct xdg_surface_listener popup_surface_listener = {
	.configure = popup_surface_configure,
};

static void popup_handle_configure(void *data,
	struct xdg_popup *popup, // NOSONAR: protocol callback ABI requires this type.
	int32_t x, int32_t y, int32_t width, int32_t height)
{
	(void)data;
	(void)popup;
	(void)x;
	(void)y;
	(void)width;
	(void)height;
}

static void popup_handle_done(void *data,
	struct xdg_popup *popup) // NOSONAR: protocol callback ABI requires this type.
{
	(void)data;
	(void)popup;
}

static const struct xdg_popup_listener popup_listener = {
	.configure = popup_handle_configure,
	.popup_done = popup_handle_done,
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

static void workspace_window(void *data, // NOSONAR: protocol callback ABI requires this arity.
	struct klest_workspace_manager_v1 *manager, // NOSONAR: protocol callback ABI requires this type.
	uint32_t id, uint32_t workspace, const char *title, uint32_t active,
	uint32_t minimized, uint32_t maximized)
{
	struct test_client *client = data;
	(void)manager;
	(void)workspace;
	(void)title;
	client->window_seen = true;
	client->window_id = id;
	client->window_active = active != 0;
	client->window_minimized = minimized != 0;
	client->window_maximized = maximized != 0;
}

static void workspace_window_closed(void *data,
	struct klest_workspace_manager_v1 *manager, // NOSONAR: protocol callback ABI requires this type.
	uint32_t id)
{
	struct test_client *client = data;
	(void)manager;
	(void)id;
	client->window_closed = true;
}

static const struct klest_workspace_manager_v1_listener workspace_listener = {
	.state = workspace_state,
	.window = workspace_window,
	.window_closed = workspace_window_closed,
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
			&klest_workspace_manager_v1_interface, min_version(version, 2));
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

/* Checks the workspace global reports a sane state and honours activate(),
 * including the focus side effects for windows left on the old workspace. */
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

	/* Switching away must defocus the window that stays behind. */
	client->window_active = true;
	klest_workspace_manager_v1_activate(client->workspace_manager, 1);
	if (wl_display_roundtrip(client->display) < 0) {
		fprintf(stderr, "integration client: workspace activate roundtrip failed\n");
		return false;
	}
	if (client->active_workspace != 1) {
		fprintf(stderr, "integration client: activate did not switch workspace\n");
		return false;
	}
	if (client->window_active) {
		fprintf(stderr, "integration client: window kept focus on the "
			"inactive workspace\n");
		return false;
	}

	/* Switching back must reactivate the window. The keyboard-less headless
	 * compositor never sets seat focus, so the xdg activated state is the
	 * observable contract here. */
	client->toplevel_activated = false;
	klest_workspace_manager_v1_activate(client->workspace_manager, 0);
	if (wl_display_roundtrip(client->display) < 0 ||
			client->active_workspace != 0) {
		fprintf(stderr, "integration client: activate back did not switch\n");
		return false;
	}
	if (!client->toplevel_activated) {
		fprintf(stderr, "integration client: window was not reactivated "
			"on return\n");
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
	return true;
}

/* Exercises minimize, restore and maximize over the workspace protocol. */
static bool exercise_window_management(struct test_client *client)
{
	if (wl_display_roundtrip(client->display) < 0) {
		fprintf(stderr, "integration client: window state roundtrip failed\n");
		return false;
	}
	if (!client->window_seen) {
		fprintf(stderr, "integration client: no window announced\n");
		return false;
	}

	klest_workspace_manager_v1_minimize_toplevel(client->workspace_manager,
		client->window_id);
	if (wl_display_roundtrip(client->display) < 0 || !client->window_minimized) {
		fprintf(stderr, "integration client: minimize did not take effect\n");
		return false;
	}

	klest_workspace_manager_v1_activate_toplevel(client->workspace_manager,
		client->window_id);
	if (wl_display_roundtrip(client->display) < 0 || client->window_minimized) {
		fprintf(stderr, "integration client: restore did not take effect\n");
		return false;
	}

	klest_workspace_manager_v1_toggle_maximize_toplevel(
		client->workspace_manager, client->window_id);
	if (wl_display_roundtrip(client->display) < 0 || !client->window_maximized) {
		fprintf(stderr, "integration client: maximize did not take effect\n");
		return false;
	}
	return true;
}

/* Creates a transient popup parented to the toplevel: the compositor must find
 * the parent's scene tree, reparent the popup under it and send a configure. */
static bool exercise_popup(struct test_client *client)
{
	struct xdg_positioner *positioner =
		xdg_wm_base_create_positioner(client->wm_base);
	struct wl_surface *surface =
		wl_compositor_create_surface(client->compositor);
	struct xdg_surface *xdg_surface = surface != NULL ?
		xdg_wm_base_get_xdg_surface(client->wm_base, surface) : NULL;
	struct xdg_popup *popup = NULL;
	bool ok = positioner != NULL && xdg_surface != NULL &&
		xdg_surface_add_listener(xdg_surface, &popup_surface_listener,
			client) == 0;
	if (ok) {
		xdg_positioner_set_size(positioner, 16, 16);
		xdg_positioner_set_anchor_rect(positioner, 0, 0, 16, 16);
		popup = xdg_surface_get_popup(xdg_surface, client->xdg_surface,
			positioner);
		ok = popup != NULL &&
			xdg_popup_add_listener(popup, &popup_listener, client) == 0;
	}
	if (ok) {
		client->popup_configured = false;
		wl_surface_commit(surface);
		for (int attempt = 0; attempt < 5 && !client->popup_configured;
				attempt++) {
			if (wl_display_roundtrip(client->display) < 0) {
				ok = false;
				break;
			}
		}
		ok = ok && client->popup_configured;
		if (!ok) {
			fprintf(stderr, "integration client: popup never configured\n");
		}
	}

	if (popup != NULL) {
		xdg_popup_destroy(popup);
	}
	if (xdg_surface != NULL) {
		xdg_surface_destroy(xdg_surface);
	}
	if (surface != NULL) {
		wl_surface_destroy(surface);
	}
	if (positioner != NULL) {
		xdg_positioner_destroy(positioner);
	}
	(void)wl_display_roundtrip(client->display);
	return ok;
}

/* Drives the client-initiated xdg-toplevel requests. The compositor mirrors
 * maximize/minimize back over the workspace protocol, which is the observable
 * contract checked here; fullscreen only has to be handled without error. */
static bool exercise_client_requests(struct test_client *client)
{
	/* The window-management stage left the window maximized. */
	xdg_toplevel_unset_maximized(client->toplevel);
	if (wl_display_roundtrip(client->display) < 0 || client->window_maximized) {
		fprintf(stderr, "integration client: client unmaximize had no effect\n");
		return false;
	}

	xdg_toplevel_set_maximized(client->toplevel);
	if (wl_display_roundtrip(client->display) < 0 || !client->window_maximized) {
		fprintf(stderr, "integration client: client maximize had no effect\n");
		return false;
	}

	xdg_toplevel_set_fullscreen(client->toplevel, NULL);
	xdg_toplevel_unset_fullscreen(client->toplevel);
	if (wl_display_roundtrip(client->display) < 0) {
		fprintf(stderr, "integration client: fullscreen roundtrip failed\n");
		return false;
	}

	xdg_toplevel_set_minimized(client->toplevel);
	if (wl_display_roundtrip(client->display) < 0 || !client->window_minimized) {
		fprintf(stderr, "integration client: client minimize had no effect\n");
		return false;
	}
	return true;
}

/* Exercises defensive rejections that have no happy-path trigger: the shared
 * SHM allocator must reject bad arguments, and the workspace protocol must
 * ignore requests for ids that do not exist. */
static bool exercise_edge_cases(struct test_client *client)
{
	uint32_t pixels[4] = {0};
	if (klest_shm_buffer_create(NULL, 8, 8, 0) != NULL ||
			klest_shm_buffer_create(client->shm, 0, 8, 0) != NULL ||
			klest_shm_buffer_create(client->shm, 8, 0xffffffffU, 0) != NULL) {
		fprintf(stderr, "integration client: SHM allocator accepted bad args\n");
		return false;
	}
	if (klest_shm_buffer_create_from_data(NULL, 2, 2, 8, pixels) != NULL ||
			klest_shm_buffer_create_from_data(client->shm, 2, 2, 4, pixels) != NULL) {
		fprintf(stderr, "integration client: SHM image allocator accepted bad "
			"args\n");
		return false;
	}

	/* With valid dimensions and a live wl_shm, the allocator must still fail
	 * gracefully (NULL, no crash) when the backing temp file cannot be made.
	 * The Wayland connection is already established, so poking XDG_RUNTIME_DIR
	 * only affects the SHM temp-file path, not the live display. */
	char *saved_runtime = getenv("XDG_RUNTIME_DIR");
	char *saved_copy = saved_runtime != NULL ? strdup(saved_runtime) : NULL;
	char long_dir[4200];
	memset(long_dir, 'a', sizeof(long_dir) - 1);
	long_dir[sizeof(long_dir) - 1] = '\0';
	bool shm_env_ok =
		(unsetenv("XDG_RUNTIME_DIR") == 0 &&
			klest_shm_buffer_create(client->shm, 4, 4, 0) == NULL) &&
		(setenv("XDG_RUNTIME_DIR", long_dir, 1) == 0 &&
			klest_shm_buffer_create(client->shm, 4, 4, 0) == NULL) &&
		(setenv("XDG_RUNTIME_DIR", "/klest-nonexistent-runtime-dir", 1) == 0 &&
			klest_shm_buffer_create(client->shm, 4, 4, 0) == NULL);
	if (saved_copy != NULL) {
		(void)setenv("XDG_RUNTIME_DIR", saved_copy, 1);
		free(saved_copy);
	} else {
		(void)unsetenv("XDG_RUNTIME_DIR");
	}
	if (!shm_env_ok) {
		fprintf(stderr, "integration client: SHM allocator did not reject an "
			"unusable XDG_RUNTIME_DIR\n");
		return false;
	}

	/* An unknown toplevel id must be a silent no-op, not a crash. */
	klest_workspace_manager_v1_minimize_toplevel(client->workspace_manager,
		0xdeadbeefU);
	klest_workspace_manager_v1_activate_toplevel(client->workspace_manager,
		0xdeadbeefU);
	klest_workspace_manager_v1_toggle_maximize_toplevel(
		client->workspace_manager, 0xdeadbeefU);
	if (wl_display_roundtrip(client->display) < 0) {
		fprintf(stderr, "integration client: unknown-id roundtrip failed\n");
		return false;
	}
	return true;
}

static bool unmap_toplevel(struct test_client *client)
{
	wl_surface_attach(client->surface, NULL, 0, 0);
	wl_surface_commit(client->surface);
	if (wl_display_roundtrip(client->display) < 0) {
		fprintf(stderr, "integration client: unmap roundtrip failed\n");
		return false;
	}
	if (!client->window_closed) {
		fprintf(stderr, "integration client: no window_closed event\n");
		return false;
	}
	return true;
}

int main(void)
{
	struct test_client client = {0};
	bool ok = connect_client(&client) && exercise_toplevel(&client) &&
		exercise_popup(&client) &&
		exercise_window_management(&client) &&
		exercise_workspaces(&client) &&
		exercise_client_requests(&client) && exercise_edge_cases(&client) &&
		unmap_toplevel(&client);
	destroy_client(&client);
	return ok ? 0 : 1;
}

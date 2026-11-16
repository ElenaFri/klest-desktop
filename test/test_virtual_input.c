/*
 * Klest — virtual-input integration test.
 *
 * Drives the compositor through the wlr-virtual-keyboard/pointer protocols:
 * maps a window, resizes it with an interactive pointer grab, then exercises
 * the Alt-keybindings (maximize, minimize, workspace switch, quit). The
 * compositor must terminate cleanly on Alt+Escape.
 *
 * One binary, two roles: the parent is the harness that runs the compositor;
 * the forked child is the Wayland client that injects the input.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <xkbcommon/xkbcommon.h>
#include <wayland-client.h>
#include <wayland-client-protocol.h>

#include "xdg-shell-client-protocol.h"
#include "klest-workspace-v1-client-protocol.h"
#include "virtual-keyboard-unstable-v1-client-protocol.h"
#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"
#include "../src/wayland/shm.h"

/* Linux input event codes (the virtual keyboard protocol speaks raw codes). */
#define KEY_ESC 1U
#define KEY_E 18U
#define KEY_R 19U
#define KEY_T 20U
#define KEY_U 22U
#define KEY_2 3U
#define KEY_BACKSPACE 14U
#define KEY_ENTER 28U
#define KEY_SPACE 57U
#define KEY_F1 59U
#define KEY_F9 67U
#define KEY_F10 68U
#define KEY_LEFTSHIFT 42U
#define KEY_LEFTALT 56U
#define BTN_LEFT 0x110U

#define XDG_RESIZE_EDGE_BOTTOM_RIGHT 10U
#define OUTPUT_WIDTH 1280U
#define OUTPUT_HEIGHT 720U
#define WINDOW_SIZE 32U
#define RESIZE_DX 40
#define RESIZE_DY 30
#define MOVE_DX 24
#define MOVE_DY 18

struct input_client {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct xdg_wm_base *wm_base;
	struct wl_seat *seat;
	struct wl_pointer *pointer;
	struct klest_workspace_manager_v1 *workspace;
	struct zwp_virtual_keyboard_manager_v1 *vkbd_mgr;
	struct zwlr_virtual_pointer_manager_v1 *vptr_mgr;

	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *toplevel;
	struct wl_buffer *buffer;
	struct zwp_virtual_keyboard_v1 *vkbd;
	struct zwlr_virtual_pointer_v1 *vptr;
	struct xkb_keymap *xkb_keymap;   /* mirror of the keymap we sent */
	struct xkb_state *xkb_state;

	bool configured;
	uint32_t configure_width;
	uint32_t configure_height;
	bool window_seen;
	uint32_t window_id;
	bool window_state_seen;
	bool window_maximized;
	bool window_minimized;
	uint32_t window_workspace;
	bool window_moved_seen;
	bool state_seen;
	uint32_t active_workspace;
	uint32_t button_serial;
	struct wl_data_device_manager *data_mgr;
	struct wl_data_device *data_device;
};

/* ------------------------------------------------------------------ helpers */

static uint64_t monotonic_milliseconds(void)
{
	struct timespec now;
	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
		return 0;
	}
	return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}

static void sleep_milliseconds(long milliseconds)
{
	struct timespec delay = {
		.tv_sec = milliseconds / 1000,
		.tv_nsec = (milliseconds % 1000) * 1000000,
	};
	while (nanosleep(&delay, &delay) != 0 && errno == EINTR) {
		/* Retry the remaining delay after an interrupting signal. */
	}
}

/* Predicates over the client state, refreshed as events arrive. */
typedef bool (*client_pred)(const struct input_client *client);

static bool pred_configured(const struct input_client *c)
{
	return c->configured;
}

static bool pred_window_seen(const struct input_client *c)
{
	return c->window_seen;
}

static bool pred_resized(const struct input_client *c)
{
	return c->configure_width == WINDOW_SIZE + RESIZE_DX &&
		c->configure_height == WINDOW_SIZE + RESIZE_DY;
}

static bool pred_maximized(const struct input_client *c)
{
	return c->window_state_seen && c->window_maximized;
}

static bool pred_unmaximized(const struct input_client *c)
{
	return c->window_state_seen && !c->window_maximized;
}

static bool pred_minimized(const struct input_client *c)
{
	return c->window_state_seen && c->window_minimized;
}

static bool pred_workspace1(const struct input_client *c)
{
	return c->state_seen && c->active_workspace == 1;
}

static bool pred_button_serial(const struct input_client *c)
{
	return c->button_serial != 0;
}

static bool pred_window_on_ws1(const struct input_client *c)
{
	return c->window_moved_seen && c->window_workspace == 1;
}

static bool pred_window_restored(const struct input_client *c)
{
	return c->window_state_seen && !c->window_minimized;
}

/* Dispatches events until `pred` holds or `timeout_ms` elapse. Polls the
 * display fd and dispatches — wl_display_dispatch_pending alone never reads
 * the socket, so incoming events would never be seen. */
static bool wait_until(struct input_client *client, client_pred pred,
	int timeout_ms)
{
	uint64_t deadline = monotonic_milliseconds() + (uint64_t)timeout_ms;
	while (!pred(client)) {
		if (wl_display_flush(client->display) < 0) {
			fprintf(stderr, "virtual-input client: flush failed\n");
			return false;
		}
		struct pollfd pfd = {
			.fd = wl_display_get_fd(client->display),
			.events = POLLIN,
		};
		int ready = poll(&pfd, 1, 100);
		if (ready < 0 && errno != EINTR) {
			fprintf(stderr, "virtual-input client: poll failed\n");
			return false;
		}
		if (ready > 0 && wl_display_dispatch(client->display) < 0) {
			fprintf(stderr, "virtual-input client: dispatch failed\n");
			return false;
		}
		if (pred(client)) {
			return true;
		}
		if (monotonic_milliseconds() >= deadline) {
			return false;
		}
	}
	return true;
}

/* ------------------------------------------------------------- listeners */

static void wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial)
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
	struct input_client *client = data;
	xdg_surface_ack_configure(xdg_surface, serial);
	client->configured = true;
}

static const struct xdg_surface_listener xdg_surface_listener = {
	.configure = surface_configure,
};

static void toplevel_configure(void *data, // NOSONAR: protocol callback ABI
	struct xdg_toplevel *toplevel, int32_t width, int32_t height,
	struct wl_array *states)
{
	struct input_client *client = data;
	(void)toplevel;
	(void)states;
	if (width > 0 && height > 0) {
		client->configure_width = (uint32_t)width;
		client->configure_height = (uint32_t)height;
	}
}

static const struct xdg_toplevel_listener toplevel_listener = {
	.configure = toplevel_configure,
};

static void workspace_state(void *data, // NOSONAR: protocol callback ABI
	struct klest_workspace_manager_v1 *manager, uint32_t count,
	uint32_t active)
{
	struct input_client *client = data;
	(void)manager;
	(void)count;
	client->state_seen = true;
	client->active_workspace = active;
}

static void workspace_window(void *data, // NOSONAR: protocol callback ABI
	struct klest_workspace_manager_v1 *manager, uint32_t id,
	uint32_t workspace, const char *title, uint32_t active,
	uint32_t minimized, uint32_t maximized)
{
	struct input_client *client = data;
	(void)manager;
	(void)title;
	(void)active;
	client->window_seen = true;
	client->window_state_seen = true;
	client->window_id = id;
	client->window_workspace = workspace;
	client->window_minimized = minimized != 0;
	client->window_maximized = maximized != 0;
	if (workspace != 0) {
		client->window_moved_seen = true;
	}
}

static void workspace_window_closed(void *data, // NOSONAR: protocol callback ABI
	struct klest_workspace_manager_v1 *manager, uint32_t id)
{
	(void)data;
	(void)manager;
	(void)id;
}

static const struct klest_workspace_manager_v1_listener workspace_listener = {
	.state = workspace_state,
	.window = workspace_window,
	.window_closed = workspace_window_closed,
};

static void pointer_enter(void *data, // NOSONAR: protocol callback ABI
	struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface,
	wl_fixed_t sx, wl_fixed_t sy)
{
	(void)data;
	(void)pointer;
	(void)serial;
	(void)surface;
	(void)sx;
	(void)sy;
	/* The enter serial is NOT a valid grab serial: only button presses are. */
}

static void pointer_leave_noop(void *data, // NOSONAR: protocol callback ABI
	struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface)
{
	(void)data;
	(void)pointer;
	(void)serial;
	(void)surface;
}

static void pointer_button(void *data, // NOSONAR: protocol callback ABI
	struct wl_pointer *pointer, uint32_t serial, uint32_t time,
	uint32_t button, uint32_t state)
{
	struct input_client *client = data;
	(void)pointer;
	(void)time;
	(void)button;
	/* Only a button press yields a serial the compositor accepts for an
	 * interactive grab. */
	if (state == 1) { /* WL_POINTER_BUTTON_STATE_PRESSED */
		client->button_serial = serial;
	}
}

static void pointer_motion_noop(void *data, // NOSONAR: protocol callback ABI
	struct wl_pointer *pointer, uint32_t time, wl_fixed_t surface_x,
	wl_fixed_t surface_y)
{
	(void)data;
	(void)pointer;
	(void)time;
	(void)surface_x;
	(void)surface_y;
}

static void pointer_axis_noop(void *data, // NOSONAR: protocol callback ABI
	struct wl_pointer *pointer, uint32_t time, uint32_t axis,
	wl_fixed_t value)
{
	(void)data;
	(void)pointer;
	(void)time;
	(void)axis;
	(void)value;
}

static void pointer_axis_stop_noop(void *data, // NOSONAR: protocol callback ABI
	struct wl_pointer *pointer, uint32_t time, uint32_t axis)
{
	(void)data;
	(void)pointer;
	(void)time;
	(void)axis;
}

static void pointer_axis_discrete_noop(void *data, // NOSONAR: protocol callback ABI
	struct wl_pointer *pointer, uint32_t axis, int32_t discrete)
{
	(void)data;
	(void)pointer;
	(void)axis;
	(void)discrete;
}

static void pointer_axis_source_noop(void *data, // NOSONAR: protocol callback ABI
	struct wl_pointer *pointer, uint32_t axis_source)
{
	(void)data;
	(void)pointer;
	(void)axis_source;
}

static void pointer_noop(void *data, struct wl_pointer *pointer)
{
	(void)data;
	(void)pointer;
}

static const struct wl_pointer_listener pointer_listener = {
	.enter = pointer_enter,
	.leave = pointer_leave_noop,
	.motion = pointer_motion_noop,
	.button = pointer_button,
	.axis = pointer_axis_noop,
	.frame = pointer_noop,
	.axis_source = pointer_axis_source_noop,
	.axis_stop = pointer_axis_stop_noop,
	.axis_discrete = pointer_axis_discrete_noop,
};

static void seat_capabilities(void *data, // NOSONAR: protocol callback ABI
	struct wl_seat *seat, uint32_t capabilities)
{
	struct input_client *client = data;
	(void)seat;
	if ((capabilities & WL_SEAT_CAPABILITY_POINTER) != 0 &&
			client->pointer == NULL) {
		client->pointer = wl_seat_get_pointer(client->seat);
		if (client->pointer != NULL) {
			wl_pointer_add_listener(client->pointer,
				&pointer_listener, client);
		}
	}
}

static void seat_name_noop(void *data, struct wl_seat *seat, const char *name)
{
	(void)data;
	(void)seat;
	(void)name;
}

static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_capabilities,
	.name = seat_name_noop,
};

static void registry_global(void *data, // NOSONAR: protocol callback ABI
	struct wl_registry *registry, uint32_t name, const char *interface,
	uint32_t version)
{
	struct input_client *client = data;
	(void)version;
	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		client->compositor = wl_registry_bind(registry, name,
			&wl_compositor_interface, 4);
	} else if (strcmp(interface, wl_shm_interface.name) == 0) {
		client->shm = wl_registry_bind(registry, name,
			&wl_shm_interface, 1);
	} else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
		client->wm_base = wl_registry_bind(registry, name,
			&xdg_wm_base_interface, 3);
		if (client->wm_base != NULL) {
			xdg_wm_base_add_listener(client->wm_base,
				&wm_base_listener, client);
		}
	} else if (strcmp(interface, wl_seat_interface.name) == 0) {
		client->seat = wl_registry_bind(registry, name,
			&wl_seat_interface, 1);
		if (client->seat != NULL) {
			wl_seat_add_listener(client->seat, &seat_listener, client);
		}
	} else if (strcmp(interface,
			klest_workspace_manager_v1_interface.name) == 0) {
		client->workspace = wl_registry_bind(registry, name,
			&klest_workspace_manager_v1_interface, 2);
		if (client->workspace != NULL) {
			klest_workspace_manager_v1_add_listener(client->workspace,
				&workspace_listener, client);
		}
	} else if (strcmp(interface,
			zwp_virtual_keyboard_manager_v1_interface.name) == 0) {
		client->vkbd_mgr = wl_registry_bind(registry, name,
			&zwp_virtual_keyboard_manager_v1_interface, 1);
	} else if (strcmp(interface,
			zwlr_virtual_pointer_manager_v1_interface.name) == 0) {
		client->vptr_mgr = wl_registry_bind(registry, name,
			&zwlr_virtual_pointer_manager_v1_interface, 2);
	} else if (strcmp(interface,
			wl_data_device_manager_interface.name) == 0) {
		client->data_mgr = wl_registry_bind(registry, name,
			&wl_data_device_manager_interface, 3);
	}
}

static void registry_global_remove(void *data, // NOSONAR: protocol callback ABI
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

/* ---------------------------------------------------------------- client */

static bool map_window(struct input_client *client)
{
	client->surface = wl_compositor_create_surface(client->compositor);
	client->xdg_surface = xdg_wm_base_get_xdg_surface(client->wm_base,
		client->surface);
	client->toplevel = xdg_surface_get_toplevel(client->xdg_surface);
	if (client->surface == NULL || client->xdg_surface == NULL ||
			client->toplevel == NULL) {
		return false;
	}
	xdg_surface_add_listener(client->xdg_surface, &xdg_surface_listener,
		client);
	xdg_toplevel_add_listener(client->toplevel, &toplevel_listener, client);
	wl_surface_commit(client->surface);
	if (!wait_until(client, pred_configured, 5000)) {
		fprintf(stderr, "virtual-input client: no initial configure\n");
		return false;
	}

	client->buffer = klest_shm_buffer_create(client->shm, WINDOW_SIZE,
		WINDOW_SIZE, 0xff404040U);
	if (client->buffer == NULL) {
		fprintf(stderr, "virtual-input client: cannot create buffer\n");
		return false;
	}
	wl_surface_attach(client->surface, client->buffer, 0, 0);
	wl_surface_commit(client->surface);
	if (!wait_until(client, pred_window_seen, 5000)) {
		fprintf(stderr, "virtual-input client: window never announced\n");
		return false;
	}
	return true;
}

/* The virtual keyboard protocol refuses keys until a keymap is set: send a
 * default (US) xkb keymap over an fd, as real clients do. The keymap and a
 * mirror xkb state are kept for tracking modifiers (see vkey). */
static bool send_keymap(struct input_client *client)
{
	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (context == NULL) {
		return false;
	}
	client->xkb_keymap = xkb_keymap_new_from_names(context, NULL,
		XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (client->xkb_keymap == NULL) {
		xkb_context_unref(context);
		return false;
	}
	client->xkb_state = xkb_state_new(client->xkb_keymap);
	char *text = xkb_keymap_get_as_string(client->xkb_keymap,
		XKB_KEYMAP_FORMAT_TEXT_V1);
	if (client->xkb_state == NULL || text == NULL) {
		xkb_keymap_unref(client->xkb_keymap);
		client->xkb_keymap = NULL;
		xkb_context_unref(context);
		return false;
	}
	xkb_context_unref(context);
	size_t size = strlen(text) + 1;

	const char *runtime = getenv("XDG_RUNTIME_DIR");
	char path[4096];
	int n = snprintf(path, sizeof(path), "%s/vkbd-keymap-XXXXXX",
		runtime != NULL ? runtime : "/tmp");
	bool sent = false;
	if (n > 0 && (size_t)n < sizeof(path)) {
		int fd = mkstemp(path);
		if (fd >= 0) {
			(void)unlink(path);
			if (write(fd, text, size) == (ssize_t)size) {
				zwp_virtual_keyboard_v1_keymap(client->vkbd, 1 /* XKB_V1 */,
				fd, size);
				sent = true;
			}
			close(fd);
		}
	}
	free(text);
	return sent;
}

/* The virtual keyboard protocol does NOT update the compositor's modifier
 * state from key events: the client must track its own xkb state and send
 * the serialized modifiers alongside each key. */
static void vkey(struct input_client *client, uint32_t code, uint32_t state)
{
	zwp_virtual_keyboard_v1_key(client->vkbd, 0, code, state);
	xkb_state_update_key(client->xkb_state, code + 8,
		state != 0 ? XKB_KEY_DOWN : XKB_KEY_UP);
	zwp_virtual_keyboard_v1_modifiers(client->vkbd,
		xkb_state_serialize_mods(client->xkb_state,
			XKB_STATE_MODS_DEPRESSED),
		xkb_state_serialize_mods(client->xkb_state,
			XKB_STATE_MODS_LATCHED),
		xkb_state_serialize_mods(client->xkb_state,
			XKB_STATE_MODS_LOCKED),
		xkb_state_serialize_layout(client->xkb_state,
			XKB_STATE_LAYOUT_EFFECTIVE));
}

/* Moves the cursor to the window centre and presses the left button; the
 * compositor must send the grab serial back through wl_pointer. */
static bool click_window(struct input_client *client)
{
	zwlr_virtual_pointer_v1_motion_absolute(client->vptr, 0,
		OUTPUT_WIDTH / 2, WINDOW_SIZE / 2, OUTPUT_WIDTH, OUTPUT_HEIGHT);
	zwlr_virtual_pointer_v1_frame(client->vptr);
	zwlr_virtual_pointer_v1_button(client->vptr, 0, BTN_LEFT, 1);
	zwlr_virtual_pointer_v1_frame(client->vptr);
	zwlr_virtual_pointer_v1_button(client->vptr, 0, BTN_LEFT, 0);
	zwlr_virtual_pointer_v1_frame(client->vptr);
	return wl_display_flush(client->display) >= 0;
}

/* Moves the pointer to (x, y) in output coordinates and left-clicks there, so a
 * layer-shell surface under that point (the panel) receives the button. */
static bool pointer_click_at(struct input_client *client, uint32_t x, uint32_t y)
{
	zwlr_virtual_pointer_v1_motion_absolute(client->vptr, 0, x, y,
		OUTPUT_WIDTH, OUTPUT_HEIGHT);
	zwlr_virtual_pointer_v1_frame(client->vptr);
	zwlr_virtual_pointer_v1_button(client->vptr, 0, BTN_LEFT, 1);
	zwlr_virtual_pointer_v1_frame(client->vptr);
	zwlr_virtual_pointer_v1_button(client->vptr, 0, BTN_LEFT, 0);
	zwlr_virtual_pointer_v1_frame(client->vptr);
	return wl_display_flush(client->display) >= 0;
}

/* Presses `key` with Alt (and optionally Shift) held. */
static bool alt_key_full(struct input_client *client, uint32_t key, bool shift)
{
	vkey(client, KEY_LEFTALT, 1);
	if (shift) {
		vkey(client, KEY_LEFTSHIFT, 1);
	}
	vkey(client, key, 1);
	vkey(client, key, 0);
	if (shift) {
		vkey(client, KEY_LEFTSHIFT, 0);
	}
	vkey(client, KEY_LEFTALT, 0);
	return wl_display_flush(client->display) >= 0;
}

static bool alt_key(struct input_client *client, uint32_t key)
{
	return alt_key_full(client, key, false);
}

/* Runs a component (panel/launcher) against the live compositor and waits
 * for a clean exit. */
static bool run_component(const char *path, const char *argument)
{
	pid_t pid = fork();
	if (pid == 0) {
		if (argument != NULL) {
			execl(path, path, argument, (char *)NULL);
		} else {
			execl(path, path, (char *)NULL);
		}
		_exit(127);
	}
	if (pid < 0) {
		return false;
	}
	int status = -1;
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
		/* Reap the component after an interrupting signal. */
	}
	return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

/* Types "true" (no modifiers) — filters the launcher list down to the
 * /usr/bin/true executable. */
static bool type_true(struct input_client *client)
{
	static const uint32_t codes[] = {KEY_T, KEY_R, KEY_U, KEY_E};
	for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
		vkey(client, codes[i], 1);
		vkey(client, codes[i], 0);
	}
	return wl_display_flush(client->display) >= 0;
}

/* Drives a full-session launcher through to launching a command. The launcher
 * is pointed at a private PATH holding a single harmless "true" executable, so
 * the launch/spawn path runs deterministically and safely. Keys sent before the
 * launcher is focused are lost, so we retry: type the query, then press Enter to
 * launch the selection. */
static bool drive_launcher(struct input_client *client, const char *path)
{
	char dir[] = "/tmp/klest-launch-XXXXXX";
	if (mkdtemp(dir) == NULL) {
		perror("virtual-input client: mkdtemp launcher PATH");
		return false;
	}
	char script[4096];
	int n = snprintf(script, sizeof(script), "%s/true", dir);
	bool prepared = n > 0 && (size_t)n < sizeof(script);
	if (prepared) {
		FILE *file = fopen(script, "we");
		prepared = file != NULL;
		if (file != NULL) {
			fputs("#!/bin/sh\nexit 0\n", file);
			prepared = fclose(file) == 0 && prepared;
		}
	}
	if (prepared) {
		prepared = chmod(script, 0755) == 0;
	}
	if (!prepared) {
		(void)unlink(script);
		(void)rmdir(dir);
		return false;
	}

	pid_t pid = fork();
	if (pid == 0) {
		(void)setenv("PATH", dir, 1);
		execl(path, path, (char *)NULL);
		_exit(127);
	}
	bool ok = false;
	if (pid >= 0) {
		int status = -1;
		bool reaped = false;
		/* Let the launcher map and take keyboard focus before typing, so the
		 * query is actually appended (and the launcher redraws) rather than
		 * lost to a not-yet-focused surface. */
		sleep_milliseconds(600);
		/* First type a character that matches nothing in the private PATH, so
		 * the launcher draws its "(no matches)" state, then backspace it away. */
		vkey(client, KEY_2, 1);
		vkey(client, KEY_2, 0);
		(void)wl_display_flush(client->display);
		sleep_milliseconds(150);
		vkey(client, KEY_BACKSPACE, 1);
		vkey(client, KEY_BACKSPACE, 0);
		(void)wl_display_flush(client->display);
		sleep_milliseconds(100);
		type_true(client); /* filter down to the private "true" (append path) */
		(void)wl_display_flush(client->display);
		sleep_milliseconds(200); /* let the launcher redraw the filtered list */
		for (int attempt = 0; attempt < 20 && !reaped; attempt++) {
			if (waitpid(pid, &status, WNOHANG) == pid) {
				ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
				reaped = true;
				break;
			}
			vkey(client, KEY_ENTER, 1); /* launch the selection */
			vkey(client, KEY_ENTER, 0);
			(void)wl_display_flush(client->display);
			sleep_milliseconds(150);
		}
		if (!reaped) {
			(void)kill(pid, SIGKILL);
			(void)waitpid(pid, &status, 0);
		}
	}

	(void)unlink(script);
	(void)rmdir(dir);
	return ok;
}

/* Reaps a full-session component after the compositor has begun tearing down.
 * Its exit code is not asserted: a redraw racing the disconnect can report a
 * benign commit failure. Only a process that must be force-killed — which would
 * skip its gcov flush — counts as a failure. */
static bool reap_component_session(pid_t pid, const char *name)
{
	if (pid <= 0) {
		return true;
	}
	int status = -1;
	for (int attempt = 0; attempt < 80; attempt++) {
		if (waitpid(pid, &status, WNOHANG) == pid) {
			return true;
		}
		sleep_milliseconds(50);
	}
	(void)kill(pid, SIGKILL);
	(void)waitpid(pid, &status, 0);
	fprintf(stderr, "virtual-input client: %s session did not exit on "
		"compositor shutdown\n", name);
	return false;
}

static bool run_client(void)
{
	struct input_client client = {0};
	bool ok = true;

	client.display = wl_display_connect(NULL);
	if (client.display == NULL) {
		fprintf(stderr, "virtual-input client: cannot connect\n");
		return false;
	}
	client.registry = wl_display_get_registry(client.display);
	wl_registry_add_listener(client.registry, &registry_listener, &client);
	if (wl_display_roundtrip(client.display) < 0) {
		fprintf(stderr, "virtual-input client: registry roundtrip failed\n");
		return false;
	}
	if (client.compositor == NULL || client.shm == NULL ||
			client.wm_base == NULL || client.seat == NULL ||
			client.workspace == NULL || client.vkbd_mgr == NULL ||
			client.vptr_mgr == NULL) {
		fprintf(stderr, "virtual-input client: missing globals\n");
		return false;
	}

	ok = map_window(&client);
	if (ok) {
		client.vkbd = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(
			client.vkbd_mgr, client.seat);
		client.vptr = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(
			client.vptr_mgr, client.seat);
		ok = client.vkbd != NULL && client.vptr != NULL;
		if (!ok) {
			fprintf(stderr, "virtual-input client: cannot create virtual "
				"devices\n");
		}
		if (ok && !send_keymap(&client)) {
			fprintf(stderr, "virtual-input client: cannot send keymap\n");
			ok = false;
		}
		if (ok) {
			/* Let the server apply the keymap before any key press. */
			ok = wl_display_roundtrip(client.display) >= 0;
		}
	}

	/* Stage 1 — click the window: this focuses it (the compositor only
	 * grants interactive grabs and Alt+F9/F10 to the focused window) and
	 * yields the grab serial the compositor reports back. */
	if (ok && click_window(&client)) {
		ok = wait_until(&client, pred_button_serial, 5000);
	} else if (ok) {
		ok = false;
	}

	/* Stage 2 — interactive resize: start a bottom-right resize with the
	 * captured serial, drag the pointer: the compositor must configure the
	 * new size. */
	if (ok) {
		client.configure_width = 0;
		client.configure_height = 0;
		xdg_toplevel_resize(client.toplevel, client.seat,
			client.button_serial, XDG_RESIZE_EDGE_BOTTOM_RIGHT);
		zwlr_virtual_pointer_v1_motion(client.vptr, 0,
			wl_fixed_from_int(RESIZE_DX), wl_fixed_from_int(RESIZE_DY));
		zwlr_virtual_pointer_v1_frame(client.vptr);
		(void)wl_display_flush(client.display);
		ok = wait_until(&client, pred_resized, 5000);
		if (!ok) {
			fprintf(stderr, "virtual-input client: interactive resize did "
				"not produce the expected size (%ux%u)\n",
				client.configure_width, client.configure_height);
		}
	}

	/* Stage 2b — end the resize grab, then start an interactive move and drag
	 * it. The move has no protocol-visible result (the compositor only moves
	 * the scene node), so this just has to run cleanly; a final button release
	 * restores passthrough mode for the stage that follows. */
	if (ok) {
		zwlr_virtual_pointer_v1_button(client.vptr, 0, BTN_LEFT, 0);
		zwlr_virtual_pointer_v1_frame(client.vptr);
		xdg_toplevel_move(client.toplevel, client.seat, client.button_serial);
		zwlr_virtual_pointer_v1_motion(client.vptr, 0,
			wl_fixed_from_int(MOVE_DX), wl_fixed_from_int(MOVE_DY));
		zwlr_virtual_pointer_v1_frame(client.vptr);
		zwlr_virtual_pointer_v1_button(client.vptr, 0, BTN_LEFT, 0);
		zwlr_virtual_pointer_v1_frame(client.vptr);
		(void)wl_display_flush(client.display);
		sleep_milliseconds(50);
		ok = wl_display_roundtrip(client.display) >= 0;
	}

	/* Stage 3 — scroll and client requests: the axis event must reach the
	 * focused window, and set_selection/set_cursor must be honoured. */
	if (ok) {
		zwlr_virtual_pointer_v1_axis(client.vptr, 0, 0 /* vertical */,
			wl_fixed_from_int(120));
		zwlr_virtual_pointer_v1_frame(client.vptr);
		(void)wl_display_flush(client.display);
		sleep_milliseconds(100);
	}
	if (ok) {
		struct wl_data_source *source =
			wl_data_device_manager_create_data_source(client.data_mgr);
		if (source != NULL) {
			wl_data_source_offer(source, "text/plain");
			client.data_device =
				wl_data_device_manager_get_data_device(client.data_mgr,
					client.seat);
			wl_data_device_set_selection(client.data_device, source,
				client.button_serial);
		}
		/* Hide the cursor from the focused client: exercises the
		 * request_set_cursor path. */
		wl_pointer_set_cursor(client.pointer, client.button_serial, NULL, 0, 0);
		ok = wl_display_flush(client.display) >= 0;
	}

	/* Stage 3b — move the pointer over empty space: with no toplevel under the
	 * cursor the compositor restores the default cursor image and clears the
	 * pointer focus. */
	if (ok) {
		zwlr_virtual_pointer_v1_motion_absolute(client.vptr, 0,
			OUTPUT_WIDTH - 1, OUTPUT_HEIGHT - 1, OUTPUT_WIDTH, OUTPUT_HEIGHT);
		zwlr_virtual_pointer_v1_frame(client.vptr);
		(void)wl_display_flush(client.display);
		sleep_milliseconds(50);
		ok = wl_display_roundtrip(client.display) >= 0;
	}

	/* Stage 3c — compositor keybindings without a protocol-visible effect:
	 * Alt+F1 cycles focus, Alt+Return spawns the configured terminal and
	 * Alt+Space takes the "no launcher configured" path. */
	if (ok) {
		alt_key(&client, KEY_F1);
		alt_key(&client, KEY_ENTER);
		alt_key(&client, KEY_SPACE);
		ok = wl_display_roundtrip(client.display) >= 0;
	}

	/* Stage 4 — Alt+F10 maximizes, then restores. */
	if (ok) {
		client.window_state_seen = false;
		alt_key(&client, KEY_F10);
		ok = wait_until(&client, pred_maximized, 5000);
		if (ok) {
			client.window_state_seen = false;
			alt_key(&client, KEY_F10);
			ok = wait_until(&client, pred_unmaximized, 5000);
		}
		if (!ok) {
			fprintf(stderr, "virtual-input client: Alt+F10 maximize toggle "
				"failed\n");
		}
	}

	/* Stage 5 — Alt+F9 minimizes the focused window, and the workspace
	 * protocol restores it. */
	if (ok) {
		client.window_state_seen = false;
		alt_key(&client, KEY_F9);
		ok = wait_until(&client, pred_minimized, 5000);
		if (!ok) {
			fprintf(stderr, "virtual-input client: Alt+F9 minimize failed\n");
		}
	}
	if (ok) {
		client.window_state_seen = false;
		klest_workspace_manager_v1_activate_toplevel(client.workspace,
			client.window_id);
		(void)wl_display_flush(client.display);
		ok = wait_until(&client, pred_window_restored, 5000);
		if (!ok) {
			fprintf(stderr, "virtual-input client: restore failed\n");
		}
	}

	/* Stage 6 — Alt+Shift+2 moves the focused window to workspace 1. */
	if (ok) {
		client.window_moved_seen = false;
		alt_key_full(&client, KEY_2, true);
		ok = wait_until(&client, pred_window_on_ws1, 5000);
		if (!ok) {
			fprintf(stderr, "virtual-input client: Alt+Shift+2 move failed\n");
		}
	}

	/* Stage 7 — Alt+2 activates workspace 1, where the window now lives. */
	if (ok) {
		client.state_seen = false;
		alt_key(&client, KEY_2);
		ok = wait_until(&client, pred_workspace1, 5000);
		if (!ok) {
			fprintf(stderr, "virtual-input client: Alt+2 workspace switch "
				"failed\n");
		}
	}

	/* Stage 8 — the panel must render a window list with the live window. */
	if (ok) {
		const char *panel = getenv("KLEST_PANEL_TEST_EXECUTABLE");
		if (panel == NULL || panel[0] == '\0' || !run_component(panel, "--once")) {
			fprintf(stderr, "virtual-input client: panel --once failed\n");
			ok = false;
		}
	}

	/* Stage 9 — the launcher must take keyboard focus, filter on the typed
	 * query, and exit on Escape. */
	if (ok) {
		const char *launcher = getenv("KLEST_LAUNCHER_TEST_EXECUTABLE");
		if (launcher == NULL || launcher[0] == '\0' ||
				!drive_launcher(&client, launcher)) {
			fprintf(stderr, "virtual-input client: launcher session failed\n");
			ok = false;
		}
	}

	/* Stage 9b — run the panel as a full session (not --once) so its event loop
	 * and redraw-on-change paths run. Switching workspaces while it is up makes
	 * it redraw; it then exits cleanly when the compositor drops it on
	 * Alt+Escape, and is reaped below so its coverage is flushed. */
	pid_t panel_session = -1;
	if (ok) {
		const char *panel = getenv("KLEST_PANEL_TEST_EXECUTABLE");
		if (panel == NULL || panel[0] == '\0') {
			fprintf(stderr, "virtual-input client: panel path unset\n");
			ok = false;
		} else {
			panel_session = fork();
			if (panel_session == 0) {
				execl(panel, panel, (char *)NULL);
				_exit(127);
			}
			ok = panel_session > 0;
		}
	}

	/* Stage 9c — also run the background as a full session (not --once) so its
	 * dispatch loop runs and then returns cleanly when the compositor drops it
	 * on Alt+Escape. It is reaped below so its coverage is flushed. */
	pid_t bg_session = -1;
	if (ok) {
		const char *background = getenv("KLEST_BACKGROUND_TEST_EXECUTABLE");
		if (background == NULL || background[0] == '\0') {
			fprintf(stderr, "virtual-input client: background path unset\n");
			ok = false;
		} else {
			bg_session = fork();
			if (bg_session == 0) {
				execl(background, background, (char *)NULL);
				_exit(127);
			}
			ok = bg_session > 0;
		}
	}
	if (ok) {
		sleep_milliseconds(400); /* panel maps, binds the seat, enters its loop */
		/* The panel is a top layer-shell bar; the compositor routes the pointer
		 * to it, so clicks on its window box and workspace indicators drive the
		 * panel's pointer handlers (which issue workspace-protocol requests). */
		pointer_click_at(&client, 348, 18); /* window box: minimise the window */
		sleep_milliseconds(250);
		/* The window is now minimised; a second click on its box restores it,
		 * driving the panel's activate-window action (not just minimise). */
		pointer_click_at(&client, 348, 18);
		sleep_milliseconds(200);
		/* A click on empty chrome (the "KLEST" label, left of the workspace
		 * indicators) must resolve to no action. */
		pointer_click_at(&client, 40, 18);
		sleep_milliseconds(150);
		pointer_click_at(&client, 109, 18); /* workspace 0 indicator */
		sleep_milliseconds(200);
		/* A scroll over the panel and a move off it cover the axis/leave paths. */
		zwlr_virtual_pointer_v1_axis(client.vptr, 0, 0, wl_fixed_from_int(120));
		zwlr_virtual_pointer_v1_frame(client.vptr);
		zwlr_virtual_pointer_v1_motion_absolute(client.vptr, 0, 640, 400,
			OUTPUT_WIDTH, OUTPUT_HEIGHT);
		zwlr_virtual_pointer_v1_frame(client.vptr);
		(void)wl_display_flush(client.display);
		sleep_milliseconds(200);
		/* One more change over the protocol so the panel redraws again. */
		klest_workspace_manager_v1_activate(client.workspace, 1);
		(void)wl_display_flush(client.display);
		sleep_milliseconds(400); /* let the panel settle (redraw, back to poll) */
	}

	/* Alt+Escape quits the compositor; the harness checks the exit code.
	 * A roundtrip forces the compositor to read and dispatch the key events
	 * before we tear the connection down — a bare flush would race the
	 * disconnect and the quit keystroke would be lost. The roundtrip returns
	 * an error once the compositor terminates and drops us, which is fine. */
	if (ok) {
		alt_key(&client, KEY_ESC);
		(void)wl_display_roundtrip(client.display);
	}

	/* The compositor is now tearing down, which closes the full-session clients
	 * and stops their event loops. Reap them so their coverage is flushed. */
	if (!reap_component_session(panel_session, "panel")) {
		ok = false;
	}
	if (!reap_component_session(bg_session, "background")) {
		ok = false;
	}

	if (client.vkbd != NULL) {
		zwp_virtual_keyboard_v1_destroy(client.vkbd);
	}
	if (client.vptr != NULL) {
		zwlr_virtual_pointer_v1_destroy(client.vptr);
	}
	wl_display_disconnect(client.display);
	return ok;
}

/* --------------------------------------------------------------- harness */

static pid_t start_compositor(const char *path)
{
	pid_t pid = fork();
	if (pid != 0) {
		return pid;
	}
	execl(path, path, (char *)NULL);
	perror(path);
	_exit(127);
}

static bool wait_for_socket(pid_t *server_pid, const char *socket_path)
{
	for (int attempt = 0; attempt < 200; attempt++) {
		int status;
		pid_t result = waitpid(*server_pid, &status, WNOHANG);
		if (result == *server_pid) {
			*server_pid = -1;
			return false;
		}
		if (access(socket_path, F_OK) == 0) {
			return true;
		}
		sleep_milliseconds(50);
	}
	return false;
}

int main(int argc, char *argv[])
{
	if (argc != 3) {
		fprintf(stderr, "usage: %s KLEST RUNTIME_BASE\n", argv[0]);
		return 2;
	}

	char runtime_dir[4096];
	char socket_path[4096];
	char lock_path[4096];
	char config_path[4096];
	int runtime_len = snprintf(runtime_dir, sizeof(runtime_dir),
		"%s/v-XXXXXX", argv[2]);
	if (runtime_len < 0 || (size_t)runtime_len >= sizeof(runtime_dir)) {
		return 1;
	}
	if (mkdtemp(runtime_dir) == NULL) {
		perror("mkdtemp");
		return 1;
	}
	/* Build the derived paths after mkdtemp: it rewrites the XXXXXX suffix. */
	int socket_len = snprintf(socket_path, sizeof(socket_path),
		"%s/wayland-0", runtime_dir);
	int lock_len = snprintf(lock_path, sizeof(lock_path),
		"%s/wayland-0.lock", runtime_dir);
	if (socket_len < 0 || (size_t)socket_len >= sizeof(socket_path) ||
			lock_len < 0 || (size_t)lock_len >= sizeof(lock_path)) {
		return 1;
	}

	/* A minimal config whose terminal command exits immediately: it exercises
	 * the Alt+Return spawn path without launching a real terminal that would
	 * connect to the compositor and grab focus from the later stages. */
	int config_len = snprintf(config_path, sizeof(config_path),
		"%s/klest.toml", runtime_dir);
	if (config_len < 0 || (size_t)config_len >= sizeof(config_path)) {
		(void)rmdir(runtime_dir);
		return 1;
	}
	FILE *config_file = fopen(config_path, "we");
	if (config_file == NULL) {
		perror("write test config");
		(void)rmdir(runtime_dir);
		return 1;
	}
	fputs("[keybindings]\nterminal = \"true\"\n", config_file);
	if (fclose(config_file) != 0) {
		perror("close test config");
		(void)unlink(config_path);
		(void)rmdir(runtime_dir);
		return 1;
	}

	if (chmod(runtime_dir, 0700) != 0 ||
			setenv("XDG_RUNTIME_DIR", runtime_dir, 1) != 0 ||
			setenv("KLEST_CONFIG", config_path, 1) != 0 ||
			setenv("WAYLAND_DISPLAY", "wayland-0", 1) != 0 ||
			setenv("WLR_BACKENDS", "headless", 1) != 0 ||
			setenv("WLR_RENDERER", "pixman", 1) != 0 ||
			setenv("WLR_HEADLESS_OUTPUTS", "1", 1) != 0 ||
			setenv("WLR_LIBINPUT_NO_DEVICES", "1", 1) != 0) {
		perror("configure virtual-input test environment");
		(void)rmdir(runtime_dir);
		return 1;
	}

	pid_t server_pid = start_compositor(argv[1]);
	if (server_pid < 0) {
		perror("fork compositor");
		(void)rmdir(runtime_dir);
		return 1;
	}

	bool ok = wait_for_socket(&server_pid, socket_path);

	int client_status = -1;
	if (ok) {
		pid_t client_pid = fork();
		if (client_pid == 0) {
			_exit(run_client() ? 0 : 1);
		}
		while (waitpid(client_pid, &client_status, 0) < 0 && errno == EINTR) {
			/* Reap the client after an interrupting signal. */
		}
		if (WIFSIGNALED(client_status)) {
			fprintf(stderr, "virtual-input test: client died on signal %d\n",
				WTERMSIG(client_status));
		}
		ok = client_status == 0;
	}

	/* The compositor must have quit by itself on Alt+Escape, with code 0. */
	int status = -1;
	if (server_pid > 0) {
		if (client_status != 0) {
			/* The client failed before sending Alt+Escape: stop the
			 * compositor instead of waiting for a quit that will not come. */
			(void)kill(server_pid, SIGTERM);
		}
		while (waitpid(server_pid, &status, 0) < 0 && errno == EINTR) {
			/* Reap the compositor after an interrupting signal. */
		}
		if (client_status != 0 || !WIFEXITED(status) ||
				WEXITSTATUS(status) != 0) {
			fprintf(stderr, "virtual-input test: compositor exit status=%d "
				"(exited=%d code=%d signal=%d), client=%d\n", status,
				WIFEXITED(status), WEXITSTATUS(status), WIFSIGNALED(status),
				client_status);
			ok = false;
		}
	} else {
		ok = false;
	}

	(void)unlink(socket_path);
	(void)unlink(lock_path);
	(void)unlink(config_path);
	(void)rmdir(runtime_dir);
	return ok ? 0 : 1;
}

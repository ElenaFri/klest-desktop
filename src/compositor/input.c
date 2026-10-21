/*
 * Klest — input handling: seat, keyboards and pointer/cursor.
 *
 * Forwards input to focused clients, drives interactive move/resize, and
 * handles a small set of compositor keybindings (Alt + key).
 */

#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stdlib.h>

#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

#include <wlr/backend.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/util/log.h>

#include <xkbcommon/xkbcommon.h>

#include "server.h"
#include "startup.h"
#include "view.h"
#include "workspace.h"
#include "config/config.h"

struct klest_keyboard {
	struct wl_list link; /* klest_server.keyboards */
	struct klest_server *server;
	struct wlr_keyboard *wlr_keyboard;

	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
};

static void process_cursor_motion(struct klest_server *server, uint32_t time)
{
	if (server->cursor_mode == KLEST_CURSOR_MOVE) {
		klest_process_cursor_move(server);
		return;
	}
	if (server->cursor_mode == KLEST_CURSOR_RESIZE) {
		klest_process_cursor_resize(server);
		return;
	}

	double sx;
	double sy;
	struct wlr_surface *surface = NULL;
	struct klest_toplevel *toplevel = klest_toplevel_at(server,
		server->cursor->x, server->cursor->y, &surface, &sx, &sy);

	if (toplevel == NULL) {
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
	}

	struct wlr_seat *seat = server->seat;
	if (surface != NULL) {
		wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
		wlr_seat_pointer_notify_motion(seat, time, sx, sy);
	} else {
		wlr_seat_pointer_clear_focus(seat);
	}
}

static void cursor_motion(struct wl_listener *listener, void *data)
{
	struct klest_server *server = wl_container_of(listener, server, cursor_motion);
	struct wlr_pointer_motion_event *event = data;

	wlr_cursor_move(server->cursor, &event->pointer->base,
		event->delta_x, event->delta_y);
	process_cursor_motion(server, event->time_msec);
}

static void cursor_motion_absolute(struct wl_listener *listener, void *data)
{
	struct klest_server *server =
		wl_container_of(listener, server, cursor_motion_absolute);
	struct wlr_pointer_motion_absolute_event *event = data;

	wlr_cursor_warp_absolute(server->cursor, &event->pointer->base,
		event->x, event->y);
	process_cursor_motion(server, event->time_msec);
}

static void cursor_button(struct wl_listener *listener, void *data)
{
	struct klest_server *server = wl_container_of(listener, server, cursor_button);
	struct wlr_pointer_button_event *event = data;

	wlr_seat_pointer_notify_button(server->seat, event->time_msec,
		event->button, event->state);

	if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
		klest_reset_cursor_mode(server);
		return;
	}

	double sx;
	double sy;
	struct wlr_surface *surface = NULL;
	struct klest_toplevel *toplevel = klest_toplevel_at(server,
		server->cursor->x, server->cursor->y, &surface, &sx, &sy);
	klest_focus_toplevel(toplevel);
}

static void cursor_axis(struct wl_listener *listener, void *data)
{
	struct klest_server *server = wl_container_of(listener, server, cursor_axis);
	struct wlr_pointer_axis_event *event = data;

	wlr_seat_pointer_notify_axis(server->seat, event->time_msec,
		event->orientation, event->delta, event->delta_discrete,
		event->source, event->relative_direction);
}

static void cursor_frame(struct wl_listener *listener, void *data)
{
	struct klest_server *server = wl_container_of(listener, server, cursor_frame);
	(void)data;
	wlr_seat_pointer_notify_frame(server->seat);
}

static bool handle_workspace_key(struct klest_server *server,
	struct wlr_keyboard *keyboard, uint32_t keycode, bool move)
{
	static const char *const key_names[KLEST_WORKSPACE_COUNT] = {
		"AE01", "AE02", "AE03", "AE04",
	};
	const char *name = xkb_keymap_key_get_name(keyboard->keymap, keycode);
	if (name == NULL) {
		return false;
	}
	for (uint32_t i = 0; i < KLEST_WORKSPACE_COUNT; i++) {
		if (strcmp(name, key_names[i]) == 0) {
			if (move) {
				klest_workspace_move_focused(server, i);
			} else {
				klest_workspace_activate(server, i);
			}
			return true;
		}
	}
	return false;
}

/* Spawns a configured command, logging when the binding is left unset. */
static void spawn_configured(const char *cmd, const char *key)
{
	if (cmd == NULL || cmd[0] == '\0') {
		wlr_log(WLR_INFO, "no command configured ([keybindings] %s)", key);
		return;
	}
	klest_startup_exec(cmd);
}

/* Compositor keybindings. Returns true if the key was consumed. */
static bool handle_keybinding(struct klest_server *server, xkb_keysym_t sym)
{
	const struct klest_config *cfg = server->config;
	switch (sym) {
	case XKB_KEY_Escape:
		wl_display_terminate(server->display);
		return true;
	case XKB_KEY_Return:
		spawn_configured(cfg != NULL ? cfg->terminal : NULL, "terminal");
		return true;
	case XKB_KEY_space:
		spawn_configured(cfg != NULL ? cfg->launcher : NULL, "launcher");
		return true;
	case XKB_KEY_F9:
		klest_workspace_minimize_focused(server);
		return true;
	case XKB_KEY_F10:
		klest_workspace_toggle_maximize_focused(server);
		return true;
	case XKB_KEY_F1: {
		struct wlr_surface *focused_surface =
			server->seat->keyboard_state.focused_surface;
		if (focused_surface != NULL) {
			focused_surface = wlr_surface_get_root_surface(focused_surface);
		}
		struct klest_toplevel *toplevel;
		wl_list_for_each(toplevel, &server->toplevels, link) {
			if (toplevel->workspace == server->workspaces.active &&
					!toplevel->minimized &&
					toplevel->xdg_toplevel->base->surface != focused_surface) {
				klest_focus_toplevel(toplevel);
				return true;
			}
		}
		return true;
	}
	default:
		return false;
	}
}

static void keyboard_handle_modifiers(struct wl_listener *listener, void *data)
{
	struct klest_keyboard *keyboard =
		wl_container_of(listener, keyboard, modifiers);
	(void)data;

	wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);
	wlr_seat_keyboard_notify_modifiers(keyboard->server->seat,
		&keyboard->wlr_keyboard->modifiers);
}

static void keyboard_handle_key(struct wl_listener *listener, void *data)
{
	struct klest_keyboard *keyboard = wl_container_of(listener, keyboard, key);
	struct klest_server *server = keyboard->server;
	struct wlr_keyboard_key_event *event = data;

	/* libinput keycode -> xkbcommon keycode. */
	uint32_t keycode = event->keycode + 8;
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(
		keyboard->wlr_keyboard->xkb_state, keycode, &syms);

	bool handled = false;
	uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);
	if ((modifiers & WLR_MODIFIER_ALT) &&
			event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		bool move = (modifiers & WLR_MODIFIER_SHIFT) != 0;
		handled = handle_workspace_key(server, keyboard->wlr_keyboard,
			keycode, move);
		for (int i = 0; i < nsyms; i++) {
			handled = handle_keybinding(server, syms[i]) || handled;
		}
	}

	if (!handled) {
		wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);
		wlr_seat_keyboard_notify_key(server->seat, event->time_msec,
			event->keycode, event->state);
	}
}

static void keyboard_handle_destroy(struct wl_listener *listener, void *data)
{
	struct klest_keyboard *keyboard =
		wl_container_of(listener, keyboard, destroy);
	(void)data;

	wl_list_remove(&keyboard->modifiers.link);
	wl_list_remove(&keyboard->key.link);
	wl_list_remove(&keyboard->destroy.link);
	wl_list_remove(&keyboard->link);
	free(keyboard);
}

static void server_new_keyboard(struct klest_server *server,
	struct wlr_input_device *device)
{
	struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);

	struct klest_keyboard *keyboard = calloc(1, sizeof(*keyboard));
	if (keyboard == NULL) {
		return;
	}
	keyboard->server = server;
	keyboard->wlr_keyboard = wlr_keyboard;

	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (context == NULL) {
		wlr_log(WLR_ERROR, "failed to create XKB context");
		free(keyboard);
		return;
	}
	struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, NULL,
		XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (keymap == NULL) {
		wlr_log(WLR_ERROR, "failed to create XKB keymap");
		xkb_context_unref(context);
		free(keyboard);
		return;
	}
	bool keymap_set = wlr_keyboard_set_keymap(wlr_keyboard, keymap);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);
	if (!keymap_set) {
		wlr_log(WLR_ERROR, "failed to set keyboard keymap");
		free(keyboard);
		return;
	}

	int32_t rate = 25;
	int32_t delay = 600;
	if (server->config != NULL) {
		rate = server->config->keyboard_repeat_rate;
		delay = server->config->keyboard_repeat_delay;
	}
	wlr_keyboard_set_repeat_info(wlr_keyboard, rate, delay);

	keyboard->modifiers.notify = keyboard_handle_modifiers;
	wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);
	keyboard->key.notify = keyboard_handle_key;
	wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);
	keyboard->destroy.notify = keyboard_handle_destroy;
	wl_signal_add(&device->events.destroy, &keyboard->destroy);

	wlr_seat_set_keyboard(server->seat, wlr_keyboard);
	wl_list_insert(&server->keyboards, &keyboard->link);
}

static void server_new_pointer(struct klest_server *server,
	struct wlr_input_device *device)
{
	wlr_cursor_attach_input_device(server->cursor, device);
}

static void server_new_input(struct wl_listener *listener, void *data)
{
	struct klest_server *server = wl_container_of(listener, server, new_input);
	struct wlr_input_device *device = data;

	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		server_new_keyboard(server, device);
		break;
	case WLR_INPUT_DEVICE_POINTER:
		server_new_pointer(server, device);
		break;
	default:
		break;
	}

	uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&server->keyboards)) {
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	}
	wlr_seat_set_capabilities(server->seat, caps);
}

static void seat_request_cursor(struct wl_listener *listener, void *data)
{
	struct klest_server *server =
		wl_container_of(listener, server, request_cursor);
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	struct wlr_seat_client *focused_client =
		server->seat->pointer_state.focused_client;

	/* Only honour the request if it comes from the focused client. */
	if (focused_client == event->seat_client) {
		wlr_cursor_set_surface(server->cursor, event->surface,
			event->hotspot_x, event->hotspot_y);
	}
}

static void seat_request_set_selection(struct wl_listener *listener, void *data)
{
	struct klest_server *server =
		wl_container_of(listener, server, request_set_selection);
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(server->seat, event->source, event->serial);
}

void klest_input_init(struct klest_server *server)
{
	wl_list_init(&server->keyboards);
	server->cursor_mode = KLEST_CURSOR_PASSTHROUGH;

	/* Reap spawned children automatically so they don't become zombies. */
	signal(SIGCHLD, SIG_IGN);

	server->cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(server->cursor, server->output_layout);
	server->cursor_mgr = wlr_xcursor_manager_create(NULL, 24);

	server->cursor_motion.notify = cursor_motion;
	wl_signal_add(&server->cursor->events.motion, &server->cursor_motion);
	server->cursor_motion_absolute.notify = cursor_motion_absolute;
	wl_signal_add(&server->cursor->events.motion_absolute,
		&server->cursor_motion_absolute);
	server->cursor_button.notify = cursor_button;
	wl_signal_add(&server->cursor->events.button, &server->cursor_button);
	server->cursor_axis.notify = cursor_axis;
	wl_signal_add(&server->cursor->events.axis, &server->cursor_axis);
	server->cursor_frame.notify = cursor_frame;
	wl_signal_add(&server->cursor->events.frame, &server->cursor_frame);

	server->new_input.notify = server_new_input;
	wl_signal_add(&server->backend->events.new_input, &server->new_input);

	server->seat = wlr_seat_create(server->display, "seat0");
	server->request_cursor.notify = seat_request_cursor;
	wl_signal_add(&server->seat->events.request_set_cursor,
		&server->request_cursor);
	server->request_set_selection.notify = seat_request_set_selection;
	wl_signal_add(&server->seat->events.request_set_selection,
		&server->request_set_selection);
}

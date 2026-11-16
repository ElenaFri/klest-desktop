/*
 * Klest — keyboard-driven application launcher (ADR 0009).
 *
 * A full-screen overlay layer-shell client. It lists the executables found in
 * $PATH, filters them as you type, and launches the selected command on Enter.
 * Escape cancels. Rendering uses the shared bitmap font; no toolkit dependency.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <wayland-client.h>
#include <wayland-client-protocol.h>

#include <xkbcommon/xkbcommon.h>

#include "klest-launcher-filter.h"
#include "wayland/font.h"
#include "wayland/layer_client.h"
#include "wayland/shm.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#define LAUNCHER_INPUT_SCALE 3U
#define LAUNCHER_LIST_SCALE 2U
#define LAUNCHER_EDGE 20U  /* left/right inset of boxes, in pixels */
#define LAUNCHER_TEXT_X 40U
#define LAUNCHER_TOP 48U
#define LAUNCHER_ROW_PAD 10U

#define LAUNCHER_BG 0xff14141eU
#define LAUNCHER_BOX 0xff20222eU
#define LAUNCHER_PROMPT 0xffe8eaf5U
#define LAUNCHER_ITEM 0xffc8cad5U
#define LAUNCHER_SEL_BG 0xff5c8fe8U
#define LAUNCHER_SEL_FG 0xff14141eU

struct launcher {
	struct klest_layer_client layer;
	struct wl_registry *registry;
	struct wl_seat *seat;
	struct wl_keyboard *keyboard;
	struct xkb_context *xkb_context;
	struct xkb_keymap *xkb_keymap;
	struct xkb_state *xkb_state;
	struct wl_buffer *buffer;
	uint32_t mapped_width;
	uint32_t mapped_height;

	struct launcher_model model;
	bool needs_redraw;
};

/* -------------------------------------------------------------------- drawing */

static void draw_matches(struct launcher *launcher,
	const struct klest_canvas *canvas, uint32_t list_top)
{
	struct launcher_model *model = &launcher->model;
	uint32_t row_h = KLEST_FONT_GLYPH * LAUNCHER_LIST_SCALE + LAUNCHER_ROW_PAD;
	uint32_t room = canvas->height > list_top + LAUNCHER_TOP ?
		canvas->height - list_top - LAUNCHER_TOP : row_h;
	uint32_t visible = room / row_h;
	if (visible == 0) {
		visible = 1;
	}
	size_t first_visible = klest_launcher_scroll(model, visible);
	for (uint32_t row = 0; row < visible; row++) {
		size_t index = first_visible + row;
		if (index >= model->match_count) {
			break;
		}
		uint32_t y = list_top + row * row_h;
		bool selected = index == model->selected;
		if (selected && canvas->width > 2 * LAUNCHER_EDGE) {
			klest_canvas_fill(canvas, LAUNCHER_EDGE, y > 4U ? y - 4U : 0U,
				canvas->width - 2 * LAUNCHER_EDGE, row_h, LAUNCHER_SEL_BG);
		}
		klest_font_draw(canvas, model->matches[index], LAUNCHER_TEXT_X, y,
			LAUNCHER_LIST_SCALE, selected ? LAUNCHER_SEL_FG : LAUNCHER_ITEM);
	}
}

static bool draw_launcher(struct launcher *launcher)
{
	uint32_t width = launcher->layer.width;
	uint32_t height = launcher->layer.height;
	if (width == 0 || height == 0 || width > (uint32_t)(INT32_MAX / 4) ||
			height > (uint32_t)(INT32_MAX / ((int32_t)width * 4))) {
		fprintf(stderr, "klest-launcher: invalid output dimensions\n");
		return false;
	}
	size_t pixel_count = (size_t)width * height;
	uint32_t *pixels = malloc(pixel_count * sizeof(*pixels));
	if (pixels == NULL) {
		fprintf(stderr, "klest-launcher: cannot allocate drawing buffer\n");
		return false;
	}
	struct klest_canvas canvas = {.pixels = pixels, .width = width,
		.height = height};
	klest_canvas_clear(&canvas, LAUNCHER_BG);

	if (width > 2 * LAUNCHER_EDGE) {
		klest_canvas_fill(&canvas, LAUNCHER_EDGE, LAUNCHER_TOP - 12U,
			width - 2 * LAUNCHER_EDGE,
			KLEST_FONT_GLYPH * LAUNCHER_INPUT_SCALE + 24U, LAUNCHER_BOX);
	}
	char line[160];
	snprintf(line, sizeof(line), "> %s", launcher->model.query);
	klest_font_draw(&canvas, line, LAUNCHER_TEXT_X, LAUNCHER_TOP,
		LAUNCHER_INPUT_SCALE, LAUNCHER_PROMPT);

	uint32_t list_top = LAUNCHER_TOP + KLEST_FONT_GLYPH * LAUNCHER_INPUT_SCALE +
		40U;
	if (launcher->model.match_count == 0) {
		klest_font_draw(&canvas, "(no matches)", LAUNCHER_TEXT_X, list_top,
			LAUNCHER_LIST_SCALE, LAUNCHER_ITEM);
	} else {
		draw_matches(launcher, &canvas, list_top);
	}

	struct wl_buffer *buffer = klest_shm_buffer_create_from_data(
		launcher->layer.shm, width, height, (int32_t)(width * 4U), pixels);
	free(pixels);
	if (buffer == NULL) {
		return false;
	}
	if (!klest_layer_client_present(&launcher->layer, buffer, width, height)) {
		wl_buffer_destroy(buffer);
		return false;
	}
	struct wl_buffer *old_buffer = launcher->buffer;
	launcher->buffer = buffer;
	launcher->mapped_width = width;
	launcher->mapped_height = height;
	if (old_buffer != NULL) {
		wl_buffer_destroy(old_buffer);
	}
	return true;
}

/* ------------------------------------------------------------------- keyboard */

static void keyboard_handle_keymap(void *data,
	struct wl_keyboard *keyboard, // NOSONAR: Wayland callback ABI.
	uint32_t format, int32_t fd, uint32_t size)
{
	struct launcher *launcher = data;
	(void)keyboard;
	if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
		close(fd);
		return;
	}
	char *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (map == MAP_FAILED) {
		return;
	}
	struct xkb_keymap *keymap = xkb_keymap_new_from_string(
		launcher->xkb_context, map, XKB_KEYMAP_FORMAT_TEXT_V1,
		XKB_KEYMAP_COMPILE_NO_FLAGS);
	munmap(map, size);
	if (keymap == NULL) {
		return;
	}
	struct xkb_state *state = xkb_state_new(keymap);
	if (state == NULL) {
		xkb_keymap_unref(keymap);
		return;
	}
	if (launcher->xkb_state != NULL) {
		xkb_state_unref(launcher->xkb_state);
	}
	if (launcher->xkb_keymap != NULL) {
		xkb_keymap_unref(launcher->xkb_keymap);
	}
	launcher->xkb_keymap = keymap;
	launcher->xkb_state = state;
}

static void keyboard_handle_key(void *data,
	struct wl_keyboard *keyboard, // NOSONAR: Wayland callback ABI.
	uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
{
	struct launcher *launcher = data;
	(void)keyboard;
	(void)serial;
	(void)time;
	if (state != WL_KEYBOARD_KEY_STATE_PRESSED ||
			launcher->xkb_state == NULL) {
		return;
	}
	xkb_keycode_t keycode = key + 8;
	xkb_keysym_t sym = xkb_state_key_get_one_sym(launcher->xkb_state, keycode);
	if (klest_launcher_navigate(&launcher->model, sym)) {
		launcher->needs_redraw = true;
		return;
	}
	char buffer[8];
	int n = xkb_state_key_get_utf8(launcher->xkb_state, keycode, buffer,
		sizeof(buffer));
	if (n == 1 && (unsigned char)buffer[0] >= 0x20 &&
			(unsigned char)buffer[0] < 0x7f) {
		klest_launcher_append(&launcher->model, buffer[0]);
		launcher->needs_redraw = true;
	}
}

static void keyboard_handle_modifiers(void *data,
	struct wl_keyboard *keyboard, // NOSONAR: Wayland callback ABI.
	uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched,
	uint32_t mods_locked, uint32_t group)
{
	struct launcher *launcher = data;
	(void)keyboard;
	(void)serial;
	if (launcher->xkb_state != NULL) {
		xkb_state_update_mask(launcher->xkb_state, mods_depressed,
			mods_latched, mods_locked, 0, 0, group);
	}
}

static void keyboard_handle_enter(void *data,
	struct wl_keyboard *keyboard, // NOSONAR: Wayland callback ABI.
	uint32_t serial,
	struct wl_surface *surface, // NOSONAR: Wayland callback ABI.
	struct wl_array *keys) // NOSONAR: Wayland callback ABI.
{
	(void)data;
	(void)keyboard;
	(void)serial;
	(void)surface;
	(void)keys;
}

static void keyboard_handle_leave(void *data,
	struct wl_keyboard *keyboard, // NOSONAR: Wayland callback ABI.
	uint32_t serial,
	struct wl_surface *surface) // NOSONAR: Wayland callback ABI.
{
	(void)data;
	(void)keyboard;
	(void)serial;
	(void)surface;
}

static void keyboard_handle_repeat_info(void *data,
	struct wl_keyboard *keyboard, // NOSONAR: Wayland callback ABI.
	int32_t rate, int32_t delay)
{
	(void)data;
	(void)keyboard;
	(void)rate;
	(void)delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
	.keymap = keyboard_handle_keymap,
	.enter = keyboard_handle_enter,
	.leave = keyboard_handle_leave,
	.key = keyboard_handle_key,
	.modifiers = keyboard_handle_modifiers,
	.repeat_info = keyboard_handle_repeat_info,
};

static void seat_handle_capabilities(void *data, struct wl_seat *seat,
	uint32_t capabilities)
{
	struct launcher *launcher = data;
	bool has_keyboard = (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0;
	if (has_keyboard && launcher->keyboard == NULL) {
		launcher->keyboard = wl_seat_get_keyboard(seat);
		if (launcher->keyboard != NULL) {
			wl_keyboard_add_listener(launcher->keyboard, &keyboard_listener,
				launcher);
		}
	} else if (!has_keyboard && launcher->keyboard != NULL) {
		wl_keyboard_destroy(launcher->keyboard);
		launcher->keyboard = NULL;
	}
}

static void seat_handle_name(void *data,
	struct wl_seat *seat, // NOSONAR: Wayland callback ABI.
	const char *name)
{
	(void)data;
	(void)seat;
	(void)name;
}

static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_handle_capabilities,
	.name = seat_handle_name,
};

static void registry_global(void *data, struct wl_registry *registry,
	uint32_t name, const char *interface, uint32_t version)
{
	struct launcher *launcher = data;
	(void)version;
	if (strcmp(interface, wl_seat_interface.name) == 0) {
		launcher->seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
		if (launcher->seat != NULL) {
			wl_seat_add_listener(launcher->seat, &seat_listener, launcher);
		}
	}
}

static void registry_global_remove(void *data,
	struct wl_registry *registry, // NOSONAR: Wayland callback ABI.
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

/* Binds the seat and keyboard. A missing seat is not fatal (e.g. --once). */
static bool launcher_bind_seat(struct launcher *launcher)
{
	launcher->registry = wl_display_get_registry(launcher->layer.display);
	if (launcher->registry == NULL ||
			wl_registry_add_listener(launcher->registry, &registry_listener,
				launcher) != 0 ||
			wl_display_roundtrip(launcher->layer.display) < 0) {
		fprintf(stderr, "klest-launcher: registry setup failed\n");
		return false;
	}
	/* A second roundtrip delivers seat capabilities (and the keymap). */
	if (wl_display_roundtrip(launcher->layer.display) < 0) {
		return false;
	}
	return true;
}

/* ----------------------------------------------------------------- lifecycle */

static bool run_launcher(struct launcher *launcher, bool once)
{
	if (!draw_launcher(launcher)) {
		return false;
	}
	if (once) {
		return klest_layer_client_unmap(&launcher->layer);
	}

	int display_fd = wl_display_get_fd(launcher->layer.display);
	while (launcher->model.running && !launcher->layer.closed) {
		/* Drain every pending redraw before blocking again. draw_launcher()
		 * runs a Wayland roundtrip that itself dispatches queued events, so a
		 * fast keystroke can arrive mid-redraw; servicing redraws in a loop
		 * here keeps the newest query on screen instead of stalling it until
		 * the next event wakes poll() — which otherwise feels like a freeze. */
		while (launcher->needs_redraw) {
			launcher->needs_redraw = false;
			if (!draw_launcher(launcher)) {
				return false;
			}
		}

		struct pollfd descriptor = {.fd = display_fd, .events = POLLIN};
		int result;
		do {
			result = poll(&descriptor, 1, -1);
		} while (result < 0 && errno == EINTR);
		if (result < 0) {
			perror("klest-launcher: poll");
			return false;
		}
		if (result > 0 && wl_display_dispatch(launcher->layer.display) < 0) {
			return true;
		}
	}
	return true;
}

static void launcher_finish(struct launcher *launcher)
{
	if (launcher->keyboard != NULL) {
		wl_keyboard_destroy(launcher->keyboard);
	}
	if (launcher->seat != NULL) {
		wl_seat_destroy(launcher->seat);
	}
	if (launcher->registry != NULL) {
		wl_registry_destroy(launcher->registry);
	}
	if (launcher->buffer != NULL) {
		wl_buffer_destroy(launcher->buffer);
	}
	if (launcher->xkb_state != NULL) {
		xkb_state_unref(launcher->xkb_state);
	}
	if (launcher->xkb_keymap != NULL) {
		xkb_keymap_unref(launcher->xkb_keymap);
	}
	if (launcher->xkb_context != NULL) {
		xkb_context_unref(launcher->xkb_context);
	}
	klest_layer_client_finish(&launcher->layer);
	klest_commands_free(&launcher->model.commands);
	free(launcher->model.matches);
}

/* Launches a command detached from the launcher, via the shell. */
static void spawn(const char *command)
{
	pid_t pid = fork();
	if (pid == 0) {
		setsid();
		execl("/bin/sh", "/bin/sh", "-c", command, (char *)NULL);
		_exit(127);
	} else if (pid < 0) {
		fprintf(stderr, "klest-launcher: failed to launch '%s'\n", command);
	}
}

static int usage(const char *program)
{
	fprintf(stderr, "Usage: %s [--once]\n", program);
	return 2;
}

int main(int argc, char *argv[])
{
	bool once = false;
	if (argc == 2 && strcmp(argv[1], "--once") == 0) {
		once = true;
	} else if (argc != 1) {
		return usage(argv[0]);
	}

	struct launcher launcher = {0};
	launcher.model.running = true;
	launcher.xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (launcher.xkb_context == NULL) {
		fprintf(stderr, "klest-launcher: cannot create XKB context\n");
		return 1;
	}

	klest_commands_scan_path(&launcher.model.commands);
	klest_commands_finalize(&launcher.model.commands);
	size_t slots = launcher.model.commands.count > 0 ?
		launcher.model.commands.count : 1;
	launcher.model.matches = malloc(slots * sizeof(*launcher.model.matches));
	if (launcher.model.matches == NULL) {
		fprintf(stderr, "klest-launcher: out of memory\n");
		launcher_finish(&launcher);
		return 1;
	}
	klest_launcher_filter(&launcher.model);

	struct klest_layer_client *layer = &launcher.layer;
	const struct klest_layer_surface_spec spec = {
		.layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
		.namespace = "klest-launcher",
		.anchors = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT,
		.keyboard_interactive = true,
	};
	bool ok = klest_layer_client_connect(layer) &&
		launcher_bind_seat(&launcher) &&
		klest_layer_client_create_surface(layer, &spec) &&
		klest_layer_client_wait_configure(layer);
	klest_layer_client_set_opaque(layer, true);
	if (ok) {
		ok = run_launcher(&launcher, once);
	}

	char *launch = launcher.model.launch;
	launcher.model.launch = NULL;
	launcher_finish(&launcher);
	if (ok && launch != NULL) {
		spawn(launch);
	}
	free(launch);
	return ok ? 0 : 1;
}
